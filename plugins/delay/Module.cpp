#include "common/vst3/Module.hpp"
#include "DelayEngine.hpp"
#include "EditorModel.hpp"
namespace just {
namespace {
Engine* createEngine(){return new delay::DelayEngine;}
const SimpleControlBinding bindings[]={
    {"Time",{delay::timeL,delay::timeR},2,true,nullptr},
    {"Feedback",{delay::feedback},1,true,nullptr},
    {"Mix",{delay::mix},1,true,nullptr}
};
constexpr ParamID timing[]={delay::timeL,delay::timeR,delay::syncL,delay::syncR,delay::noteL,delay::noteR,delay::localTempo,delay::timeChange,delay::slew};
constexpr ParamID routing[]={delay::route,delay::start,delay::crossfeed,delay::feedback,delay::highPass,delay::highCut,delay::freeze};
constexpr ParamID taps[]={delay::tap1Enabled,delay::tap1Level,delay::tap1Pan,delay::tap2Enabled,delay::tap2Level,delay::tap2Pan,delay::tap3Enabled,delay::tap3Time,delay::tap3Level,delay::tap3Pan,delay::tap4Enabled,delay::tap4Time,delay::tap4Level,delay::tap4Pan};
constexpr ParamID color[]={delay::drive,delay::tilt,delay::modRate,delay::modDepth,delay::modPhase};
constexpr ParamID duck[]={delay::duckAmount,delay::duckThreshold,delay::duckAttack,delay::duckRelease};
constexpr ParamID levels[]={delay::input,delay::output,delay::width,delay::wetLevel,delay::mix};
const AdvancedGroup groups[]={
    {"Time & Sync",timing,std::size(timing)},{"Route & Feedback",routing,std::size(routing)},
    {"Taps",taps,std::size(taps)},{"Color & Modulation",color,std::size(color)},
    {"Ducking",duck,std::size(duck)},{"Levels",levels,std::size(levels)}
};
StatusSnapshot status(const SoundState& s) {
    StatusSnapshot result;
    result.mode=delay::value(s,delay::route)==0 && !delay::simpleTimeEditable(s)?"Stereo · Time: Custom":delay::routes[int(delay::value(s,delay::route))];
    result.freeze=delay::value(s,delay::freeze)!=0;
    return result;
}
#ifndef JUST_DELAY_NO_NATIVE_UI
const ModuleDefinition definition{delay::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),delay::createEditorContent,status,{true,true,true,false},nullptr,nullptr,nullptr,0,nullptr,nullptr,640,480};
#else
const ModuleDefinition definition{delay::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),nullptr,status,{true,true,true,false},nullptr,nullptr,nullptr,0,nullptr,nullptr,640,480};
#endif
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
