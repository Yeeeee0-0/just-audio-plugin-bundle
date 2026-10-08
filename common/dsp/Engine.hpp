#pragma once
#include "Realtime.hpp"
#include "../runtime/Telemetry.hpp"
#include "../runtime/Analysis.hpp"
#include "../runtime/Audition.hpp"
namespace just {
enum class SampleFormat {float32,float64};
enum class ResetReason {firstActivation,explicitReset,sampleRateChange,seek,offlineStart};
enum class TailKind {none,finite,infinite};
struct Tail {TailKind kind=TailKind::none;std::uint32_t samples=0;};
struct PrepareSpec {
    double sampleRate=48000;std::uint32_t maxBlockSize=2048;
    std::uint32_t inputChannels=2,outputChannels=2,sidechainChannels=0;
    SampleFormat format=SampleFormat::float32;bool offline=false;
    bool valid() const noexcept {
        return std::isfinite(sampleRate) && sampleRate>0 && maxBlockSize>0 &&
            (inputChannels==1 || inputChannels==2) && (outputChannels==1 || outputChannels==2) &&
            sidechainChannels<=2;
    }
};
struct ProcessContext {
    bool tempoValid=false,timeSignatureValid=false,projectSamplesValid=false,ppqValid=false;
    bool playing=false,cycleActive=false,seek=false;
    double bpm=120,ppq=0;std::int64_t projectSamples=0;
    std::int32_t numerator=4,denominator=4;
    std::int32_t blockSampleOffset=0;
    bool transportValid=false; // optional tail; false when no host ProcessContext
    AnalysisTap* analysis=nullptr; // process-call-only, measured samples; never retain
    std::uint64_t analysisSourceSample=0,analysisEpoch=0;
};
template<class Sample> struct AudioBlock {
    std::array<const Sample*,2> inputs{};
    std::array<Sample*,2> outputs{};
    std::uint32_t inputChannels=0,outputChannels=0,samples=0;
    std::uint64_t inputSilenceFlags=0;
    std::array<const Sample*,2> sidechain{};
    std::uint32_t sidechainChannels=0;
    std::uint64_t sidechainSilenceFlags=0;
};
class Engine {
public:
    virtual ~Engine()=default;
    virtual bool prepare(const PrepareSpec&) =0; // setup only; may allocate
    virtual void reset(ResetReason) noexcept=0;
    virtual void applyTargets(const SoundState&,std::int32_t absoluteBlockOffset) noexcept=0;
    virtual void process(AudioBlock<float>,const ProcessContext&) noexcept=0;
    virtual void process(AudioBlock<double>,const ProcessContext&) noexcept=0;
    virtual void endBlock() noexcept {} // optional telemetry publication; no allocation
    virtual std::uint32_t latencySamples() const noexcept=0;
    virtual Tail tailSamples() const noexcept=0;
    virtual bool readTelemetry(Telemetry&) noexcept=0;
    virtual bool setAudition(ParamID,bool) noexcept{return false;}
};
// Explicit zero-latency, no-tail foundation engine, not an effect implementation.
class PassthroughEngine final:public Engine {
    SpscQueue<Telemetry,8> telemetry;Telemetry current{};
    template<class Sample> void render(AudioBlock<Sample> block) noexcept {
        for(std::uint32_t ch=0;ch<block.outputChannels;++ch) {
            if(!block.outputs[ch])continue;
            for(std::uint32_t i=0;i<block.samples;++i) {
                Sample input=ch<block.inputChannels && block.inputs[ch] && !(block.inputSilenceFlags&(1ull<<ch))?block.inputs[ch][i]:Sample(0);
                if(!std::isfinite(input)){input=0;current.invalidInput=true;}
                current.inputPeak=std::max(current.inputPeak,std::abs(double(input)));
                block.outputs[ch][i]=input;
                current.outputPeak=std::max(current.outputPeak,std::abs(double(input)));
            }
        }
    }
public:
    bool prepare(const PrepareSpec& s) override{return s.valid();}
    void reset(ResetReason) noexcept override {current={};}
    void applyTargets(const SoundState&,std::int32_t) noexcept override{}
    void process(AudioBlock<float> b,const ProcessContext&) noexcept override{render(b);}
    void process(AudioBlock<double> b,const ProcessContext&) noexcept override{render(b);}
    void endBlock() noexcept override {telemetry.push(current);current={};}
    std::uint32_t latencySamples() const noexcept override{return 0;}
    Tail tailSamples() const noexcept override{return {};}
    bool readTelemetry(Telemetry& t) noexcept override{return telemetry.pop(t);}
};
}
