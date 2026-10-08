#include "common/vst3/Module.hpp"
#include "Dsp.hpp"
#include "Editor.hpp"
namespace just {
namespace {
Engine* createEngine(){return new limiter::LimiterEngine;}
const SimpleControlBinding bindings[]={
    {"Input Gain",{limiter::input},1,true,nullptr},
    {"Output Gain",{limiter::output},1,true,nullptr}
};
const ParamID dynamics[]={limiter::release,limiter::lookahead,limiter::transient,limiter::ceiling,limiter::trim};
const ParamID configuration[]={limiter::algorithmVersion,limiter::mode,limiter::guide};
const AdvancedGroup groups[]={{"Dynamics",dynamics,std::size(dynamics)},{"Mode and reference",configuration,std::size(configuration)}};
StatusSnapshot status(const SoundState& state) {
 StatusSnapshot result;
 result.mode=limiter::physical(state,limiter::algorithmVersion)>=.5?(limiter::physical(state,limiter::mode)==0?"Transparent insurance | Sample peak":"Transparent insurance | Reconstructed TP"):(limiter::physical(state,limiter::mode)==0?"Legacy compatibility | Live Sample Peak":"Legacy compatibility | Mix TP prototype");
 result.bypassProtectionExit=limiter::physical(state,limiter::bypass)>0;
 result.advancedCustom=hiddenParametersCustom(limiter::registry,state,bindings,std::size(bindings));return result;
}
const ModuleDefinition definition=[] {ModuleDefinition result{limiter::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),limiter::createEditor,status};result.minimumEditorWidth=880;result.minimumEditorHeight=600;return result;}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
