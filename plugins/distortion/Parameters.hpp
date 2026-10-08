#pragma once
#include "common/state/State.hpp"
namespace just::distortion {
// Explicit first-effect IDs approved by dot; receipt is in parameter-map.json.
enum ID : ParamID {
    bypass = 0,
    model = 4096,
    input_db = 4112,
    output_db = 4113,
    drive_db = 4128,
    boost = 4129,
    bias = 4130,
    soft_shape = 4144,
    hard_softness = 4145,
    asym_shape = 4146,
    fold_shape = 4147,
    pre_hp_enabled = 4160,
    pre_hp_hz = 4161,
    pre_tilt_db = 4162,
    post_bass_db = 4176,
    post_treble_db = 4177,
    post_lp_enabled = 4178,
    post_lp_hz = 4179,
    crush_bits = 4192,
    crush_hold_hz = 4193,
    crush_aa = 4194,
    crush_dither = 4195,
    makeup_db = 4208,
    drive_comp = 4209,
    match_gain_db = 4210,
    mix = 4224,
    quality = 4240,
};
inline constexpr const char* offOn[]={"Off","On"};
inline constexpr const char* modelLabels[]={"Clean","Soft","Hard","Asym","Fold","Crush"};
inline constexpr const char* boostLabels[]={"Off","+12 dB"};
inline constexpr const char* ditherLabels[]={"Off","TPDF"};
inline constexpr const char* qualityLabels[]={"1x","2x","4x","8x"};
inline constexpr ParameterSpec parameters[]={
    {"distortion.bypass",bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0,offOn},
    // v0.3 approved new-instance default only. Clean remains ordinal 0 and
    // schema1 restores its saved normalized value without migration.
    {"distortion.model",model,"Model","",0,5,1,Mapping::linear,5,true,"all",Transition::discrete,0,modelLabels},
    {"distortion.input_db",input_db,"Input","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"distortion.output_db",output_db,"Output","dB",-36,12,0,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"distortion.drive_db",drive_db,"Drive","dB",0,36,0,Mapping::linear,0,true,"Soft/Hard/Asym/Fold",Transition::continuous,10,nullptr},
    {"distortion.boost",boost,"Boost","",0,1,0,Mapping::linear,1,true,"Soft/Hard/Asym/Fold",Transition::discrete,10,boostLabels},
    {"distortion.bias",bias,"Bias","",-0.5,0.5,0,Mapping::linear,0,true,"Asym/Fold",Transition::continuous,10,nullptr},
    {"distortion.soft_shape",soft_shape,"Soft Shape","%",0,100,50,Mapping::linear,0,true,"Soft",Transition::continuous,10,nullptr},
    {"distortion.hard_softness",hard_softness,"Hard Softness","%",0,100,50,Mapping::linear,0,true,"Hard",Transition::continuous,10,nullptr},
    {"distortion.asym_shape",asym_shape,"Asym Shape","%",0,100,50,Mapping::linear,0,true,"Asym",Transition::continuous,10,nullptr},
    {"distortion.fold_shape",fold_shape,"Fold Shape","%",0,100,50,Mapping::linear,0,true,"Fold",Transition::continuous,10,nullptr},
    {"distortion.pre_hp_enabled",pre_hp_enabled,"Pre HP Enable","",0,1,0,Mapping::linear,1,true,"wet non-Clean",Transition::discrete,10,offOn},
    {"distortion.pre_hp_hz",pre_hp_hz,"Pre HP","Hz",20,2000,80,Mapping::logarithmic,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.pre_tilt_db",pre_tilt_db,"Pre Tilt","dB",-12,12,0,Mapping::linear,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.post_bass_db",post_bass_db,"Post Bass","dB",-12,12,0,Mapping::linear,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.post_treble_db",post_treble_db,"Post Treble","dB",-12,12,0,Mapping::linear,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.post_lp_enabled",post_lp_enabled,"Post LP Enable","",0,1,0,Mapping::linear,1,true,"wet non-Clean",Transition::discrete,10,offOn},
    {"distortion.post_lp_hz",post_lp_hz,"Post LP","Hz",1000,20000,16000,Mapping::logarithmic,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.crush_bits",crush_bits,"Bits","bit",4,24,12,Mapping::linear,20,true,"Crush",Transition::discrete,0,nullptr},
    {"distortion.crush_hold_hz",crush_hold_hz,"Hold Rate","Hz",500,192000,12000,Mapping::logarithmic,0,true,"Crush",Transition::continuous,0,nullptr},
    {"distortion.crush_aa",crush_aa,"Crush AA","",0,1,0,Mapping::linear,1,true,"Crush",Transition::discrete,10,offOn},
    {"distortion.crush_dither",crush_dither,"Dither","",0,1,0,Mapping::linear,1,true,"Crush",Transition::discrete,0,ditherLabels},
    {"distortion.makeup_db",makeup_db,"Makeup","dB",-24,12,0,Mapping::linear,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.drive_comp",drive_comp,"Drive Compensation","",0,1,0,Mapping::linear,1,true,"wet non-Clean",Transition::discrete,10,offOn},
    {"distortion.match_gain_db",match_gain_db,"Match Gain","dB",-12,12,0,Mapping::linear,0,true,"wet non-Clean",Transition::continuous,10,nullptr},
    {"distortion.mix",mix,"Mix","%",0,100,100,Mapping::linear,0,true,"all",Transition::continuous,10,nullptr},
    {"distortion.quality",quality,"Quality","",0,3,2,Mapping::linear,3,false,"prepared configuration",Transition::preparedConfiguration,0,qualityLabels},
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline double physical(const SoundState& s, ID id) noexcept {const auto i=registry.index(id);const auto& p=parameters[i];return s.targets[i]==p.toNormalized(p.initial)?p.initial:p.toPhysical(s.targets[i]);}
}
