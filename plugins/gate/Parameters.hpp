#pragma once
#include "common/parameters/Parameters.hpp"
namespace just::gate {
// Explicit semantic ID blocks; independent of UI order.
enum ID : ParamID {
    bypass=0, mode=100, threshold=110, range=111, ratio=112, knee=113,
    hysteresis=120, attack=121, hold=122, release=123,
    detector=130, scSource=131, scGain=132,
    scHPEnabled=133, scHPHz=134, scLPEnabled=135, scLPHz=136,
    input=140, output=141, lookahead=150
};
enum class Mode : unsigned {Gate=0, Expand=1, Duck=2};
enum class Detector : unsigned {Peak=0, RMS=1};
enum class Source : unsigned {Internal=0, External=1};
inline constexpr const char* onOff[]={"Off","On"};
inline constexpr const char* modes[]={"Gate","Expand","Duck"};
inline constexpr const char* detectors[]={"Peak","RMS"};
inline constexpr const char* sources[]={"Internal","External"};
inline constexpr ParameterSpec parameters[]={
    {"gate.bypass",bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,onOff},
    {"gate.mode",mode,"Mode","",0,2,1,Mapping::linear,2,true,"all",Transition::discrete,5,modes},
    {"gate.threshold",threshold,"Threshold","dBFS",-90,0,-80,Mapping::linear,0,true,"all",Transition::continuous,3},
    {"gate.range",range,"Range","dB",0,90,24,Mapping::linear,0,true,"all",Transition::continuous,3},
    {"gate.ratio",ratio,"Ratio",":1",1,20,2,Mapping::logarithmic,0,true,"Expand",Transition::continuous,3},
    {"gate.knee",knee,"Knee","dB",0,24,6,Mapping::linear,0,true,"Expand",Transition::continuous,3},
    {"gate.hysteresis",hysteresis,"Hysteresis","dB",0,12,3,Mapping::linear,0,true,"Gate,Duck",Transition::continuous,3},
    {"gate.attack",attack,"Attack","ms",0.05,200,1,Mapping::logarithmic,0,true,"all",Transition::continuous,3},
    {"gate.hold",hold,"Hold","ms",0,1000,50,Mapping::linear,0,true,"Gate,Duck",Transition::continuous,0},
    {"gate.release",release,"Release","ms",5,2000,100,Mapping::logarithmic,0,true,"all",Transition::continuous,3},
    {"gate.detector",detector,"Detector","",0,1,1,Mapping::linear,1,true,"all",Transition::discrete,0,detectors},
    {"gate.sc_source",scSource,"SC Source","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,3,sources},
    {"gate.sc_gain",scGain,"SC Gain","dB",-24,24,0,Mapping::linear,0,true,"External",Transition::continuous,3},
    {"gate.sc_hp_enabled",scHPEnabled,"SC HP Enabled","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,3,onOff},
    {"gate.sc_hp_hz",scHPHz,"SC HP","Hz",20,2000,80,Mapping::logarithmic,0,true,"all",Transition::continuous,3},
    {"gate.sc_lp_enabled",scLPEnabled,"SC LP Enabled","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,3,onOff},
    {"gate.sc_lp_hz",scLPHz,"SC LP","Hz",1000,20000,12000,Mapping::logarithmic,0,true,"all",Transition::continuous,3},
    {"gate.input",input,"Input","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,3},
    {"gate.output",output,"Output","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,3},
    // Saved absolute ms; effective maximum is 0 pending the shared PDC bridge.
    {"gate.lookahead",lookahead,"Lookahead","ms",0,10,0,Mapping::linear,0,true,"pending-maximum-configuration",Transition::continuous,3}
};
inline constexpr std::size_t parameterCount=std::size(parameters);
inline constexpr ParameterRegistry registry{parameters,parameterCount};
inline const ParameterSpec& spec(ParamID id) noexcept {
    const auto i=registry.index(id);return parameters[i<parameterCount?i:0];
}
}
namespace just::module {inline constexpr auto& parameters=gate::parameters;}
