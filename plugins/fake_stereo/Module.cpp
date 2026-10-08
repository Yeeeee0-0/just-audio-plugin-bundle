#include "common/vst3/Module.hpp"
#include "Parameters.hpp"
#include "Dsp.hpp"
#include "EditorModel.hpp"
namespace just {
namespace {
class StereoEngine final:public Engine {
    stereo::Core core;SpscQueue<Telemetry,8> telemetry;
public:
    bool prepare(const PrepareSpec& p) override {return core.prepare(p);}
    void reset(ResetReason reason) noexcept override {core.reset(reason);}
    void applyTargets(const SoundState& state,std::int32_t) noexcept override {
        auto p=[&](ParamID id){return stereo::spec(id).toPhysical(state.targets[stereo::registry.index(id)]);};
        stereo::Targets t;t.inputDb=p(stereo::Input);t.outputDb=p(stereo::Output);
        t.width=p(stereo::Width);t.existingSide=p(stereo::Existing);t.mix=p(stereo::Mix);
        t.lowHz=p(stereo::Low);t.highHz=p(stereo::High);t.highEnabled=p(stereo::HighEnabled)>=.5;
        t.diffuse=p(stereo::Character)>=.5;t.swap=p(stereo::Swap)>=.5;t.mono=p(stereo::Mono)>=.5;t.bypass=p(stereo::Bypass)>=.5;
        t.fieldWidth=p(stereo::FieldWidth);t.asymmetry=p(stereo::Asymmetry);t.rotation=p(stereo::Rotation);
        t.inputMS=p(stereo::InputMode)>=.5;t.invertLeft=p(stereo::InvertLeft)>=.5;t.invertRight=p(stereo::InvertRight)>=.5;
        core.setTargets(t);
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {core.process(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {core.process(b,c);}
    void endBlock() noexcept override {telemetry.push(core.telemetry);core.telemetry={};}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override {return {TailKind::finite,core.tail()};}
    bool readTelemetry(Telemetry& t) noexcept override {return telemetry.pop(t);}
};
Engine* createEngine(){return new StereoEngine;}
const SimpleControlBinding bindings[]={
    {"Gain",{stereo::Input},1,true,nullptr},
    {"Width",{stereo::FieldWidth},1,true,nullptr},
    {"Asymmetry",{stereo::Asymmetry},1,true,nullptr},
    {"Rotation",{stereo::Rotation},1,true,nullptr}
};
constexpr ParamID legacy[]={stereo::Width,stereo::Existing,stereo::Mix,stereo::Low,stereo::Character},filter[]={stereo::HighEnabled,stereo::High},trim[]={stereo::Output};
const AdvancedGroup groups[]={{"Generated Side",legacy,std::size(legacy)}, {"Generated Side High Cut",filter,std::size(filter)}, {"Output",trim,std::size(trim)}};
bool hiddenCustom(const SoundState& state) {
    for(auto id:{stereo::Width,stereo::Existing,stereo::Mix,stereo::Low,stereo::Character,stereo::HighEnabled,stereo::High,stereo::Output})
        if(state.targets[stereo::registry.index(id)]!=stereo::spec(id).toNormalized(stereo::spec(id).initial))return true;
    return false;
}
StatusSnapshot status(const SoundState& state) {
    StatusSnapshot s;s.mode=state.targets[stereo::registry.index(stereo::Character)]>=.5?"Diffuse":"Tight";
    s.monoCheck=state.targets[stereo::registry.index(stereo::Mono)]>=.5;return s;
}
const ModuleDefinition definition=[] {ModuleDefinition configured{stereo::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),stereo::createEditorContent,status,{true,true,true,false}};
    configured.minimumEditorWidth=880;configured.minimumEditorHeight=620;configured.simpleHasCustom=hiddenCustom;return configured;}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
