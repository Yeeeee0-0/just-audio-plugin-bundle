#pragma once
#include "common/state/State.hpp"
namespace just::limiter {
enum : ParamID { bypass=0,input=1,ceiling=2,release=3,transient=4,lookahead=5,trim=6,mode=7,guide=8,output=9,algorithmVersion=10 };
inline constexpr const char* modeNames[]={"Live Sample Peak","Mix TP prototype"};
inline constexpr const char* algorithmNames[]={"Legacy compatibility","Transparent insurance"};
inline constexpr ParameterSpec parameters[]={
 {"limiter.bypass",bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5},
 {"limiter.input_db",input,"Input","dB",-24,36,0,Mapping::linear,0,true,"all",Transition::continuous,5},
 {"limiter.ceiling_db",ceiling,"Ceiling","dB",-24,0,0,Mapping::linear,0,true,"all",Transition::continuous,0},
 {"limiter.release_ms",release,"Release","ms",10,2000,200,Mapping::logarithmic,0,true,"all",Transition::continuous,5},
 {"limiter.transient",transient,"Transient","%",0,100,50,Mapping::linear,0,true,"all",Transition::continuous,5},
 {"limiter.lookahead_ms",lookahead,"Lookahead","ms",0,5,2,Mapping::linear,0,true,"Live effective 0..1; Mix effective 0.1..5",Transition::continuous,5},
 {"limiter.comparison_trim_db",trim,"Comparison Trim","dB",-24,0,0,Mapping::linear,0,true,"all",Transition::continuous,5},
 {"limiter.mode",mode,"Processing Mode","",0,1,0,Mapping::linear,1,false,"all",Transition::discrete,10,modeNames},
 {"limiter.loudness_guide_lufs",guide,"Loudness Guide","LUFS",-30,-5,-23,Mapping::linear,0,false,"display only",Transition::continuous,0},
 {"limiter.output_db",output,"Output Gain","dB",-36,0,0,Mapping::linear,0,true,"all",Transition::continuous,5},
 {"limiter.algorithm",algorithmVersion,"Algorithm Version","",0,1,1,Mapping::linear,1,false,"all",Transition::discrete,10,algorithmNames,0.0}
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline double physical(const SoundState& s,ParamID id) noexcept {return parameters[id].toPhysical(s.targets[id]);}
}
