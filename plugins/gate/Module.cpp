#include "common/vst3/Module.hpp"
#include "Parameters.hpp"
#include "GateEngine.hpp"
#include "UIModel.hpp"
namespace just {
namespace {
Engine* createEngine(){return new gate::GateEngine;}
const SimpleControlBinding bindings[]={
    {"Threshold",{gate::threshold},1,true,""},
    {"Range",{gate::range},1,true,""},
    {"Attack",{gate::attack},1,true,""},
    {"Release",{gate::release},1,true,""}
};
const ParamID curve[]={gate::mode,gate::threshold,gate::range,gate::ratio,gate::knee};
const ParamID envelope[]={gate::attack,gate::hold,gate::hysteresis,gate::release};
const ParamID detection[]={gate::detector,gate::scSource,gate::scGain,gate::scHPEnabled,gate::scHPHz,gate::scLPEnabled,gate::scLPHz};
const ParamID level[]={gate::input,gate::output};
const ParamID delayed[]={gate::lookahead};
const AdvancedGroup groups[]={{"Mode / Curve",curve,std::size(curve)},{"Envelope",envelope,std::size(envelope)},{"Detector / Sidechain",detection,std::size(detection)},{"Input / Output",level,std::size(level)},{"Lookahead",delayed,std::size(delayed)}};
StatusSnapshot status(const SoundState& state) {
    StatusSnapshot s;s.mode=gate::modes[unsigned(gate::target(state,gate::mode))];
    return s;
}
const ModuleDefinition definition=[] {
    ModuleDefinition configured{gate::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),gate::createEditorContent,status,{true,true,false,true}};
    configured.minimumEditorWidth=720;configured.minimumEditorHeight=420;
    return configured;
}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
