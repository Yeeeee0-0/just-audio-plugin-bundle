#include "../Engine.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;
using namespace just::compressor;
static unsigned checks=0;
static void check(bool p,const char* text){++checks;if(!p){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
static void set(SoundState& s,ParamID id,double physical){const auto i=module::registry.index(id);check(i<module::registry.count,"known literal ID");s.targets[i]=module::parameters[i].toNormalized(physical);}
int main() {
    const auto& definition=moduleDefinition();check(definition.valid(),"registered effect module valid");
    check(definition.parameters.count==21 && definition.simpleControlCount==4,"approved registry and four direct controls");
    check(definition.buses.optionalSidechain && !definition.buses.monoToStereo,"mono/stereo plus optional SC only");
    check(module::parameters[0].id==0 && std::strcmp(module::parameters[0].unit,"")==0 &&
        module::parameters[0].stepCount==1 && module::parameters[0].initial==0 && module::parameters[0].smoothingMs==0 && module::parameters[0].enumLabels==nullptr,"frozen Bypass metadata preserved");
    // Literal values reviewed in parameter-proposal.json, independent of the
    // implementation's mapping functions. Together with min/max, 63 goldens.
    constexpr double goldenInit[]={0,1,0.23137821315975918,0.638809459365963,0.4345879896760937,
        0,0.5,0.5,0.25,0.6666666666666666,0.5,0.3333333333333333,1,1,0,0.5,0,
        0.30102999566398114,0,0.829482217661603,0};
    for(std::size_t i=0;i<module::registry.count;++i) {
        const auto& spec=module::parameters[i];
        check(i==0?spec.id==0:spec.id==99+i,"explicit approved integer table");
        check(std::abs(spec.toNormalized(spec.initial)-goldenInit[i])<1e-12 &&
            std::abs(spec.toPhysical(goldenInit[i])-spec.initial)<1e-9,"independently reviewed Init golden mapping");
        check(spec.toNormalized(spec.minimum)==0 && spec.toNormalized(spec.maximum)==1,"endpoint golden mapping");
    }
    SoundState state=initialState({},module::registry);
    set(state,100,-18);set(state,101,4);set(state,107,0);set(state,105,0);
    auto settings=settingsFromState(state);
    check(std::abs(settings.thresholdDb+18)<1e-12 && std::abs(settings.ratio-4)<1e-12 && settings.detector==Detector::peak,"physical adapter reads approved targets");
    std::unique_ptr<Engine> engine(definition.createEngine());PrepareSpec spec;spec.inputChannels=spec.outputChannels=1;spec.maxBlockSize=4800;
    check(engine->prepare(spec),"factory prepares real compressor");engine->reset(ResetReason::firstActivation);engine->applyTargets(state,0);
    std::array<double,4800> input{},output{};input.fill(0.5);
    AudioBlock<double> audio;audio.inputs[0]=input.data();audio.outputs[0]=output.data();audio.inputChannels=audio.outputChannels=1;audio.samples=4800;
    for(int i=0;i<10;++i)engine->process(audio,{});
    const double inputDb=20*std::log10(0.5),expected=(-18+(inputDb+18)/4)-inputDb;
    check(std::abs(20*std::log10(output.back()/0.5)-expected)<0.001,"injected factory actually compresses audio");
    engine->endBlock();Telemetry t;check(engine->readTelemetry(t) && t.inputPeak==0.5 && t.outputPeak>0,"generic telemetry contract adapted");
    auto* compressor=static_cast<CompressorEngine*>(engine.get());
    set(state,119,7.5);state.configurationCount=1;state.configurations[0]={1000,2};engine->applyTargets(state,0);
    auto pending=compressor->pendingConfiguration();check(pending.pending && pending.requestedLookaheadMs==7.5 && pending.requestedMaximumOrdinal==2 && pending.actualLatencySamples==0,"nonzero prepared request retained but unapplied");
    check(engine->latencySamples()==0 && engine->tailSamples().kind==TailKind::none,"nonzero requests cannot change reported PDC/tail");
    check(std::strstr(definition.status(state).mode,"Lookahead pending")!=nullptr,"pending Lookahead truthful status");
    auto bytes=encodeState(state,module::registry);SoundState restored;
    check(decodeState(bytes.data(),bytes.size(),{},module::registry,restored)==StateResult::ok,"complete sound snapshot roundtrip");
    check(restored.targets==state.targets && restored.configurationCount==1 && restored.configurations[0].value==2,"hidden and nonautomated configuration retained");
    auto p=settingsFromState(restored);check(p.thresholdDb==settings.thresholdDb && p.ratio==settings.ratio,"view-independent physical sound targets");
    set(state,119,0);state.configurations[0].value=0;engine->applyTargets(state,0);check(!compressor->pendingConfiguration().pending,"return to supported configuration");
    std::cout<<"PASS registered module/factory, 63 golden checks, real compression, complete state, retained pending configuration: "<<checks<<" checks\n";
}
