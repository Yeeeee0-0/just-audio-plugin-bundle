#pragma once
#include "common/parameters/Parameters.hpp"
namespace just::stereo {
// First effect mapping approved by dot on 2026-10-02. Do not renumber.
enum ID : ParamID { Bypass=0,Input=10,Output=11,Width=20,Existing=21,Mix=22,
    Low=30,High=31,HighEnabled=32,Character=40,Swap=41,Mono=50,
    FieldWidth=60,Asymmetry=61,Rotation=62,InputMode=63,InvertLeft=64,InvertRight=65 };
inline constexpr const char* switches[]={"Off","On"};
inline constexpr const char* inputModes[]={"L/R","M/S"};
inline constexpr const char* characters[]={"Tight","Diffuse"};
inline constexpr ParameterSpec parameters[]={
    {"stereo.bypass",Bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,switches},
    {"stereo.inputGain",Input,"Input","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"stereo.outputGain",Output,"Output","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"stereo.generatedWidth",Width,"Generated Width","%",0,200,60,Mapping::linear,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.existingSide",Existing,"Existing Side","%",0,200,100,Mapping::linear,0,true,"2->2",Transition::continuous,10},
    {"stereo.mix",Mix,"Mix","%",0,100,100,Mapping::linear,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.lowProtectHz",Low,"Low Protect","Hz",30,500,150,Mapping::logarithmic,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.highCutHz",High,"High Cut","Hz",1000,20000,20000,Mapping::logarithmic,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.highCutEnabled",HighEnabled,"High Cut Enabled","",0,1,0,Mapping::linear,1,true,"1->2/2->2",Transition::discrete,10,switches},
    {"stereo.character",Character,"Character","",0,1,0,Mapping::linear,1,true,"1->2/2->2",Transition::discrete,30,characters},
    {"stereo.swap",Swap,"Swap L/R","",0,1,0,Mapping::linear,1,true,"1->2/2->2",Transition::discrete,5,switches},
    {"stereo.monoCheck",Mono,"Mono Check","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,switches},
    // User approved 0.3 expansion; dot approved explicit append-only mapping.
    {"stereo.fieldWidth",FieldWidth,"Width","×",0,3,1,Mapping::linear,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.asymmetry",Asymmetry,"Asymmetry","°",-90,90,0,Mapping::linear,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.rotation",Rotation,"Rotation","°",-45,45,0,Mapping::linear,0,true,"1->2/2->2",Transition::continuous,10},
    {"stereo.inputMode",InputMode,"Input Mode","",0,1,0,Mapping::linear,1,true,"2->2",Transition::discrete,10,inputModes},
    {"stereo.invertLeft",InvertLeft,"Invert Left","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,5,switches},
    {"stereo.invertRight",InvertRight,"Invert Right","",0,1,0,Mapping::linear,1,true,"2->2",Transition::discrete,5,switches}
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline const ParameterSpec& spec(ParamID id) noexcept {return parameters[registry.index(id)];}
}
