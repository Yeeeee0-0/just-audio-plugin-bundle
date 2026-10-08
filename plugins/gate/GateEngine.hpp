#pragma once
#include "common/dsp/Engine.hpp"
#include "Parameters.hpp"
#include "GateMath.hpp"
namespace just::gate {
inline double target(const SoundState& state,ParamID id) noexcept {
    const auto i=registry.index(id);return i<parameterCount?parameters[i].toPhysical(state.targets[i]):0;
}
struct Diagnostics {
    double detectorDb=-300,gainDb=0;
    Phase gatePhase=Phase::Closed,duckPhase=Phase::Closed;
    std::uint64_t holdRemaining=0;
    bool sidechainMissing=false,sidechainSilent=false,lookaheadPending=false;
};
class GateEngine final:public Engine {
    PrepareSpec prepared{};bool firstTargets=true,firstAudio=true;
    unsigned smoothingSamples=144,modeSamples=240,finishSamples=24;
    double rmsCoefficient=0,rate=48000;
    std::array<LinearSmoother,parameterCount> smoothers{};
    std::array<double,parameterCount> targets{},values{};
    std::array<GainEnvelope,3> envelopes{};std::array<DetectorFilter,2> filters{};
    std::array<double,2> rmsEnergy{};std::array<LinearSmoother,3> modeWeights{};
    Mode requestedMode=Mode::Expand;Trigger gateTrigger,duckTrigger;
    LinearSmoother bypassMix,sourceMix,hpMix,lpMix,detectorMix,connectedMix;
    bool previousConnected=true;Telemetry current{};SpscQueue<Telemetry,8> queue;
    Diagnostics diagnostics_{};
    template<class Sample> void render(AudioBlock<Sample>,const ProcessContext&) noexcept;
    double value(ParamID id) const noexcept {return values[registry.index(id)];}
    double boundedMultiply(double,double) noexcept;
public:
    GateEngine() noexcept;
    bool prepare(const PrepareSpec&) override;
    void reset(ResetReason) noexcept override;
    void applyTargets(const SoundState&,std::int32_t) noexcept override;
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {queue.push(current);current={};}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override {return {};}
    bool readTelemetry(Telemetry& t) noexcept override {return queue.pop(t);}
    // Audio-owner/test diagnostics only. UI gets no processor/engine pointer.
    Diagnostics diagnostics() const noexcept {return diagnostics_;}
};
}
