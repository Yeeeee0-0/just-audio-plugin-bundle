#include "TestModule.hpp"
namespace just::test {Trace trace;}
namespace just {
namespace {
constexpr ParameterSpec specs[]={
    {"test.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
    {"test.a",1,"A","",0,1,0,Mapping::linear,0,true,"all",Transition::continuous,0},
    {"test.b",2,"B","",0,1,0,Mapping::linear,0,true,"all",Transition::continuous,0},
    {"test.mode",3,"Mode","",0,2,0,Mapping::linear,2,true,"all",Transition::discrete,0}
};
class TraceEngine final:public Engine {
    PassthroughEngine inner;
public:
    bool prepare(const PrepareSpec& s) override{++test::trace.prepareCalls;return inner.prepare(s);}
    void reset(ResetReason r) noexcept override{++test::trace.resetCalls;inner.reset(r);}
    void applyTargets(const SoundState& s,std::int32_t offset) noexcept override {
        ++test::trace.applyCalls;test::trace.offset=offset;
        std::copy_n(s.targets.begin(),4,test::trace.targets.begin());
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override{test::trace.context=c;test::trace.sidechainChannels=b.sidechainChannels;inner.process(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override{test::trace.context=c;test::trace.sidechainChannels=b.sidechainChannels;inner.process(b,c);}
    void endBlock() noexcept override{inner.endBlock();}
    std::uint32_t latencySamples() const noexcept override{return 0;}
    Tail tailSamples() const noexcept override{return {};}
    bool readTelemetry(Telemetry& t) noexcept override{
        if(!inner.readTelemetry(t))return false;
        if(test::trace.extendedTelemetry){t.validFields|=telemetryGainReduction|telemetryEffectiveRate|telemetrySidechain;t.gainReductionDb=6;t.effectiveRateHz=2.25;t.flags=telemetryFlagSidechainMissing;}
        return true;
    }
};
Engine* createEngine(){return new TraceEngine;}
const ModuleDefinition definition{{specs,std::size(specs)},createEngine,nullptr,0,nullptr,0,nullptr,nullptr,{true,true,true,true}};
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
