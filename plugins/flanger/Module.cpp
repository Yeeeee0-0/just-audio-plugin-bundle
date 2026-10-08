#include "common/vst3/Module.hpp"
#include "FlangerEngine.hpp"
#include "EditorContent.hpp"
namespace just {
namespace {
Engine* createEngine(){return new flanger::FlangerEngine;}
const SimpleControlBinding bindings[]={
 {"Rate",{flanger::rateHzID},1,true,""}, {"Depth",{flanger::depthID},1,true,""},
 {"Feedback",{flanger::feedbackID},1,true,""}, {"Mix",{flanger::mixID},1,true,""}
};
constexpr ParamID delayGroup[]={flanger::baseMsID,flanger::modeID};
constexpr ParamID modulationGroup[]={flanger::shapeID,flanger::phaseID,flanger::stereoPhaseID};
constexpr ParamID clockGroup[]={flanger::syncID,flanger::divisionID,flanger::timeModeID};
constexpr ParamID toneGroup[]={flanger::fbLowpassHzID,flanger::wetPolarityID};
constexpr ParamID gainGroup[]={flanger::inputGainID,flanger::outputGainID};
const AdvancedGroup groups[]={
 {"Delay / Manual",delayGroup,std::size(delayGroup)}, {"Modulation",modulationGroup,std::size(modulationGroup)},
 {"Host timing",clockGroup,std::size(clockGroup)}, {"Feedback / Polarity",toneGroup,std::size(toneGroup)}, {"Input / Output",gainGroup,std::size(gainGroup)}
};
StatusSnapshot status(const SoundState& state) {
 StatusSnapshot result;result.advancedCustom=hiddenParametersCustom(flanger::registry,state,bindings,std::size(bindings));
 const bool manual=flanger::physical(state,flanger::modeID)==1,sync=flanger::physical(state,flanger::syncID)==1;
 result.mode=manual?"Manual":sync?"LFO · Sync":"LFO";return result;
}
const ModuleDefinition definition=[] {
    ModuleDefinition configured{flanger::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),flanger::createEditorContent,status};
    configured.minimumEditorWidth=720;
    configured.minimumEditorHeight=420;
    return configured;
}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
