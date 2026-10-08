#pragma once
#include "common/vst3/Module.hpp"
namespace just::tremolo {
// Semantic allocations; never generate IDs from UI/table order.
enum ID : ParamID {
    bypass=0, inputGain=0x1000, outputGain=0x1001, depth=0x1100, mix=0x1101,
    rateHz=0x1200, sync=0x1201, division=0x1202, shape=0x1300,
    phase=0x1301, stereoPhase=0x1302, duty=0x1303, edgeMs=0x1304, timeMode=0x1400
};
inline constexpr const char* switches[]={"Off","On"};
inline constexpr const char* shapes[]={"Sine","Triangle","Square","Saw Up","Saw Down"};
inline constexpr const char* timeModes[]={"Free","Transport","On Play"};
inline constexpr const char* divisions[]={
    "1/64","1/32","1/16","1/8","1/4","1/2","1/1","1 bar","2 bars","4 bars",
    "1/64 D","1/32 D","1/16 D","1/8 D","1/4 D","1/2 D","1/1 D","1 bar D","2 bars D","4 bars D",
    "1/64 T","1/32 T","1/16 T","1/8 T","1/4 T","1/2 T","1/1 T","1 bar T","2 bars T","4 bars T"
};
inline constexpr ParameterSpec parameters[]={
    {"trem.bypass",bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,switches},
    {"trem.inputGain",inputGain,"Input Gain","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"trem.outputGain",outputGain,"Output Gain","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"trem.depth",depth,"Depth","%",0,100,50,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"trem.mix",mix,"Mix","%",0,100,100,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"trem.rateHz",rateHz,"Rate","Hz",0.05,40,4,Mapping::logarithmic,0,true,"free",Transition::continuous,20},
    {"trem.sync",sync,"Sync","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,switches},
    {"trem.division",division,"Division","",0,29,3,Mapping::linear,29,true,"sync",Transition::discrete,0,divisions},
    {"trem.shape",shape,"Waveform","",0,4,0,Mapping::linear,4,true,"all",Transition::discrete,10,shapes},
    {"trem.phase",phase,"Phase","deg",0,360,0,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"trem.stereoPhase",stereoPhase,"Stereo Phase","deg",0,180,0,Mapping::linear,0,true,"stereo",Transition::continuous,5},
    {"trem.duty",duty,"Duty","%",5,95,50,Mapping::linear,0,true,"square",Transition::continuous,5},
    {"trem.edgeMs",edgeMs,"Edge","ms",0.2,50,3,Mapping::linear,0,true,"square/saw",Transition::continuous,5},
    {"trem.timeMode",timeMode,"Time Mode","",0,2,0,Mapping::linear,2,true,"all",Transition::discrete,0,timeModes}
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline const ParameterSpec& spec(ParamID id) noexcept {return parameters[registry.index(id)];}
inline double physicalValue(const ParameterSpec& p,double normalized) noexcept {
    // Keep exact declared Init values (not tiny FMA round-off near 0dB).
    return normalized==p.toNormalized(p.initial)?p.initial:p.toPhysical(normalized);
}
inline double physical(const SoundState& state,ParamID id) noexcept {
    auto index=registry.index(id);return physicalValue(parameters[index],state.targets[index]);
}
inline constexpr SimpleControlBinding simpleControls[]={
    {"Frequency / Rate",{rateHz,sync,division},3,true,nullptr},
    {"Amount (dB)",{depth},1,true,nullptr},
    {"Stereo Separation (deg)",{stereoPhase},1,true,nullptr}
};
inline constexpr ParamID waveformGroup[]={shape,phase,stereoPhase,duty,edgeMs};
inline constexpr ParamID timingGroup[]={rateHz,sync,division,timeMode};
inline constexpr ParamID levelGroup[]={depth,mix,inputGain,outputGain};
inline constexpr AdvancedGroup advancedGroups[]={
    {"Waveform",waveformGroup,std::size(waveformGroup)},
    {"Timing",timingGroup,std::size(timingGroup)},
    {"Levels",levelGroup,std::size(levelGroup)}
};
}
