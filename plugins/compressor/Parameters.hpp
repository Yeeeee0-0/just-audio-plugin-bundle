#pragma once
#include "common/parameters/Parameters.hpp"
#include "common/state/State.hpp"
namespace just::module {
inline constexpr const char* labels104[]={"Clean","Punch"};
inline constexpr const char* labels105[]={"Peak","RMS","Blend"};
inline constexpr const char* labels113[]={"Internal","External"};
inline constexpr const char* labels115[]={"Off","On"};
inline constexpr const char* labels117[]={"Off","On"};
inline constexpr ParameterSpec parameters[]={
    {"compressor.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,nullptr},
    {"compressor.threshold_db",100,"Threshold","dBFS",-60,0,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.ratio",101,"Ratio",":1",1,20,2,Mapping::logarithmic,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.attack_ms",102,"Attack","ms",0.05,200,10,Mapping::logarithmic,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.release_ms",103,"Release","ms",10,2000,100,Mapping::logarithmic,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.style",104,"Style","enum",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,labels104},
    {"compressor.detector",105,"Detector","enum",0,2,1,Mapping::linear,2,true,"all",Transition::discrete,5,labels105},
    {"compressor.rms_blend",106,"Peak/RMS Blend","%",0,100,50,Mapping::linear,0,true,"Blend only; retained in other detector modes",Transition::continuous,5,nullptr},
    {"compressor.knee_db",107,"Knee","dB",0,24,6,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.range_db",108,"Range","dB",0,36,24,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.input_db",109,"Input","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.makeup_db",110,"Makeup","dB",-12,24,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.mix",111,"Mix","%",0,100,100,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"compressor.stereo_link",112,"Stereo Link","%",0,100,100,Mapping::linear,0,true,"stereo only; hidden but retained in mono",Transition::continuous,10,nullptr},
    {"compressor.sc_source",113,"SC Source","enum",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,labels113},
    {"compressor.sc_gain_db",114,"SC Gain","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,5,nullptr},
    {"compressor.sc_hp_enabled",115,"SC HP Enabled","boolean",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,labels115},
    {"compressor.sc_hp_hz",116,"SC HP","Hz",20,2000,80,Mapping::logarithmic,0,true,"when HP enabled; saved while Off",Transition::continuous,5,nullptr},
    {"compressor.sc_lp_enabled",117,"SC LP Enabled","boolean",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,labels117},
    {"compressor.sc_lp_hz",118,"SC LP","Hz",1000,20000,12000,Mapping::logarithmic,0,true,"when LP enabled; saved while Off",Transition::continuous,5,nullptr},
    {"compressor.lookahead_ms",119,"Lookahead","ms",0,10,0,Mapping::linear,0,true,"effective value bounded by prepared Maximum; retain physical value",Transition::continuous,5,nullptr},
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline constexpr std::uint32_t maximumLookaheadConfigurationID=1000;
inline double physical(const SoundState& state,ParamID id) noexcept {
    const auto i=registry.index(id);return i<registry.count?parameters[i].toPhysical(state.targets[i]):0;
}
}
