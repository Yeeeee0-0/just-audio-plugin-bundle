#pragma once
#include "common/dsp/Engine.hpp"
#include "Parameters.hpp"
#include <vector>

namespace just::reverb {
// History invalidation is O(1). Storage is allocated exclusively in prepare().
class DelayLine {
    std::vector<double> data;
    std::size_t write=0,filled=0;
public:
    void prepare(std::size_t capacity) {data.assign(capacity+4,0);clear();}
    void clear() noexcept {write=filled=0;}
    double read(double samples) const noexcept;
    void push(double value) noexcept;
};
struct OnePoleShelf {
    double b0=1,b1=0,a1=0,x1=0,y1=0;
    void configure(double low,double high,double cutoff,double sampleRate) noexcept;
    double tick(double x) noexcept {double y=b0*x+b1*x1-a1*y1;if(std::abs(y)<1e-30)y=0;x1=x;y1=y;return y;}
    void clear() noexcept {x1=y1=0;}
};
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void configure(double fs,double frequency,bool highpass) noexcept;
    void bell(double fs,double frequency,double gain,double q) noexcept;
    double tick(double x) noexcept {double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;if(std::abs(z1)<1e-30)z1=0;if(std::abs(z2)<1e-30)z2=0;return std::abs(y)<1e-30?0:y;}
    void clear() noexcept {z1=z2=0;}
};
struct EffectiveValues {
    double predelayMs=20,duckReductionDb=0,wetPeak=0;
    bool syncUnavailable=false,syncClamped=false,protection=false;
};
class ReverbEngine final : public Engine {
    struct Network {
        std::array<DelayLine,8> delay;
        std::array<OnePoleShelf,8> lowShelf,highShelf;
        std::array<DelayLine,4> diffuser;
        std::array<std::size_t,4> diffuserLength{};
        std::array<double,8> length{},phase{};
        std::array<DelayLine,2> early;
        double fs=48000,size=100,recovery=1;
        bool protection=false;
        int style=0;
        void prepare(double);
        void configure(int,double) noexcept;
        void clear() noexcept;
        void damping(double,const std::array<double,std::size(parameters)>&) noexcept;
        std::array<double,2> tick(double,double,const std::array<double,std::size(parameters)>&,double) noexcept;
    };
    PrepareSpec spec{};
    std::array<Network,2> networks;
    std::array<DelayLine,2> predelay;
    std::array<Biquad,2> inputHP,hp,lp,bell1,bell2;
    std::array<LinearSmoother,std::size(parameters)> smoother;
    std::array<double,std::size(parameters)> target{},value{};
    SpscQueue<Telemetry,8> telemetry;
    Telemetry current{};
    EffectiveValues effective{};
    int active=0,requestedStyle=0;
    double requestedSize=100,fade=1,duckEnvelope=0,predelayTarget=-1;
    std::uint32_t coefficientCountdown=0;
    bool prepared=false,targetsInitialized=false;
    template<class Sample> void render(AudioBlock<Sample>,const ProcessContext&) noexcept;
    void updateCoefficients() noexcept;
    void requestNetwork(int,double) noexcept;
public:
    bool prepare(const PrepareSpec&) override;
    void reset(ResetReason) noexcept override;
    void applyTargets(const SoundState&,std::int32_t) noexcept override;
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {telemetry.push(current);current={};}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override;
    bool readTelemetry(Telemetry& t) noexcept override {return telemetry.pop(t);}
    // For module tests, not a processor pointer exposed to the editor.
    EffectiveValues effectiveValues() const noexcept {return effective;}
};
}
