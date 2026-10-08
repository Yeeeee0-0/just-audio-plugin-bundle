#pragma once
#include "common/state/State.hpp"
namespace just::flanger {
inline constexpr ParamID bypassID = 0;
inline constexpr ParamID inputGainID = 1001;
inline constexpr ParamID outputGainID = 1002;
inline constexpr ParamID baseMsID = 2001;
inline constexpr ParamID depthID = 2002;
inline constexpr ParamID rateHzID = 3001;
inline constexpr ParamID syncID = 3002;
inline constexpr ParamID divisionID = 3003;
inline constexpr ParamID shapeID = 3004;
inline constexpr ParamID modeID = 3005;
inline constexpr ParamID phaseID = 3006;
inline constexpr ParamID stereoPhaseID = 3007;
inline constexpr ParamID timeModeID = 3008;
inline constexpr ParamID feedbackID = 4001;
inline constexpr ParamID fbLowpassHzID = 4002;
inline constexpr ParamID wetPolarityID = 4003;
inline constexpr ParamID mixID = 5001;
inline constexpr const char* bypassLabels[] = {"Off","On"};
inline constexpr const char* syncLabels[] = {"Off","On"};
inline constexpr const char* divisionLabels[] = {"1/64","1/64 dotted","1/64 triplet","1/32","1/32 dotted","1/32 triplet","1/16","1/16 dotted","1/16 triplet","1/8","1/8 dotted","1/8 triplet","1/4","1/4 dotted","1/4 triplet","1/2","1/2 dotted","1/2 triplet","1 bar","1 bar dotted","1 bar triplet","2 bars","2 bars dotted","2 bars triplet","4 bars","4 bars dotted","4 bars triplet"};
inline constexpr const char* shapeLabels[] = {"Sine","Triangle"};
inline constexpr const char* modeLabels[] = {"LFO","Manual"};
inline constexpr const char* timeModeLabels[] = {"Free","Transport","On Play"};
inline constexpr const char* wetPolarityLabels[] = {"Normal","Invert"};
inline constexpr ParameterSpec parameters[] = {
    {"flan.bypass",bypassID,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,bypassLabels},
    {"flan.inputGain",inputGainID,"Input","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"flan.outputGain",outputGainID,"Output","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"flan.baseMs",baseMsID,"Base Delay","ms",0.5,10,2,Mapping::linear,0,true,"all",Transition::continuous,20,nullptr},
    {"flan.depth",depthID,"Depth","%",0,100,50,Mapping::linear,0,true,"Manual disables editing",Transition::continuous,20,nullptr},
    {"flan.rateHz",rateHzID,"Rate","Hz",0.01,20,0.25,Mapping::logarithmic,0,true,"Manual disables editing",Transition::continuous,10,nullptr},
    {"flan.sync",syncID,"Sync","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,syncLabels},
    {"flan.division",divisionID,"Division","",0,26,18,Mapping::linear,26,true,"all",Transition::discrete,0,divisionLabels},
    {"flan.shape",shapeID,"Shape","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,10,shapeLabels},
    {"flan.mode",modeID,"Mode","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,10,modeLabels},
    {"flan.phase",phaseID,"Phase","deg",0,360,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"flan.stereoPhase",stereoPhaseID,"Stereo Phase","deg",0,180,0,Mapping::linear,0,true,"all",Transition::continuous,20,nullptr},
    {"flan.timeMode",timeModeID,"Time Mode","",0,2,0,Mapping::linear,2,true,"all",Transition::discrete,10,timeModeLabels},
    {"flan.feedback",feedbackID,"Feedback","%",-95,95,20,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"flan.fbLowpassHz",fbLowpassHzID,"Feedback Lowpass","Hz",500,20000,8000,Mapping::logarithmic,0,true,"all",Transition::continuous,10,nullptr},
    {"flan.wetPolarity",wetPolarityID,"Wet Polarity","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,wetPolarityLabels},
    {"flan.mix",mixID,"Mix","%",0,100,50,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline double physical(const SoundState& state, ParamID id) noexcept {
    const auto i=registry.index(id);return i<registry.count?parameters[i].toPhysical(state.targets[i]):0;
}
}
