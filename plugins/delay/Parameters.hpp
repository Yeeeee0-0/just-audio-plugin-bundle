#pragma once
#include "common/parameters/Parameters.hpp"
#include "common/state/State.hpp"
namespace just::delay {
// Explicit first effect allocation; numbers never depend on UI order or hashes.
enum Id : ParamID {
    bypass=0, timeL=100, timeR=101, syncL=102, syncR=103, noteL=104, noteR=105,
    route=106, start=107, feedback=108, crossfeed=109, highPass=110, highCut=111,
    mix=112, localTempo=113, timeChange=114, slew=115, input=116, output=117,
    drive=118, tilt=119, modRate=120, modDepth=121, modPhase=122,
    duckAmount=123, duckThreshold=124, duckAttack=125, duckRelease=126,
    width=127, wetLevel=128, freeze=129,
    tap3Enabled=130, tap3Time=131, tap3Level=132, tap3Pan=133,
    tap4Enabled=134, tap4Time=135, tap4Level=136, tap4Pan=137,
    tap1Level=138, tap2Level=139, tap1Pan=140, tap2Pan=141,
    tap1Enabled=142, tap2Enabled=143
};
inline constexpr const char* offOn[]={"Off","On"};
inline constexpr const char* routes[]={"Stereo","Dual","Ping-Pong"};
inline constexpr const char* starts[]={"Left","Right"};
inline constexpr const char* changes[]={"Repitch","Smooth"};
// Stable ordinals: seven straight, seven dotted, seven triplet divisions.
inline constexpr const char* notes[]={
    "1/64","1/32","1/16","1/8","1/4","1/2","1/1",
    "1/64 D","1/32 D","1/16 D","1/8 D","1/4 D","1/2 D","1/1 D",
    "1/64 T","1/32 T","1/16 T","1/8 T","1/4 T","1/2 T","1/1 T"
};
inline constexpr ParameterSpec parameters[]={
    {"delay.bypass",bypass,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,10,offOn},
    {"delay.time_l_ms",timeL,"Time L","ms",1,8000,250,Mapping::logarithmic,0,true,"Free L",Transition::continuous,50},
    {"delay.time_r_ms",timeR,"Time R","ms",1,8000,250,Mapping::logarithmic,0,true,"Free R",Transition::continuous,50},
    {"delay.sync_l",syncL,"Sync L","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,50,offOn},
    {"delay.sync_r",syncR,"Sync R","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,50,offOn},
    {"delay.note_l",noteL,"Division L","",0,20,3,Mapping::linear,20,true,"Sync L",Transition::discrete,50,notes},
    {"delay.note_r",noteR,"Division R","",0,20,3,Mapping::linear,20,true,"Sync R",Transition::discrete,50,notes},
    {"delay.route",route,"Route","",0,2,0,Mapping::linear,2,true,"all",Transition::discrete,50,routes},
    {"delay.start",start,"Start","",0,1,0,Mapping::linear,1,true,"Ping-Pong",Transition::discrete,50,starts},
    {"delay.feedback",feedback,"Feedback","%",0,95,35,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.crossfeed",crossfeed,"Crossfeed","%",0,100,0,Mapping::linear,0,true,"Stereo/Dual",Transition::continuous,50},
    {"delay.hp_hz",highPass,"Feedback HP","Hz",20,2000,80,Mapping::logarithmic,0,true,"all",Transition::continuous,20},
    {"delay.lp_hz",highCut,"High Cut","Hz",500,20000,8000,Mapping::logarithmic,0,true,"all",Transition::continuous,20},
    {"delay.mix",mix,"Mix","%",0,100,20,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"delay.local_tempo_bpm",localTempo,"Fallback Tempo","BPM",40,240,120,Mapping::linear,0,true,"no Host Tempo",Transition::continuous,50},
    {"delay.time_change",timeChange,"Time Change","",0,1,1,Mapping::linear,1,true,"all",Transition::discrete,50,changes},
    {"delay.slew_ms",slew,"Slew","ms",10,500,50,Mapping::logarithmic,0,true,"all",Transition::continuous,0},
    {"delay.input_db",input,"Input","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"delay.output_db",output,"Output","dB",-24,12,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"delay.drive_db",drive,"Wet Drive","dB",0,24,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"delay.tilt_db",tilt,"Wet Tilt","dB",-6,6,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.mod_rate_hz",modRate,"Mod Rate","Hz",0.01,10,0.3,Mapping::logarithmic,0,true,"all",Transition::continuous,20},
    {"delay.mod_depth_ms",modDepth,"Mod Depth","ms",0,10,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.mod_phase_deg",modPhase,"Mod Stereo Phase","deg",0,180,90,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.duck_amount_db",duckAmount,"Duck Amount","dB",0,24,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.duck_threshold_db",duckThreshold,"Duck Threshold","dBFS",-60,0,-24,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.duck_attack_ms",duckAttack,"Duck Attack","ms",0.5,100,5,Mapping::logarithmic,0,true,"all",Transition::continuous,20},
    {"delay.duck_release_ms",duckRelease,"Duck Release","ms",20,2000,250,Mapping::logarithmic,0,true,"all",Transition::continuous,20},
    {"delay.width_pct",width,"Wet Width","%",0,150,100,Mapping::linear,0,true,"stereo output",Transition::continuous,20},
    {"delay.wet_level_db",wetLevel,"Wet Level","dB",-120,6,0,Mapping::linear,0,true,"all",Transition::continuous,10},
    {"delay.freeze",freeze,"Freeze","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,50,offOn},
    {"delay.tap03.enabled",tap3Enabled,"Tap 3 Enabled","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,20,offOn},
    {"delay.tap03.time_ms",tap3Time,"Tap 3 Time","ms",1,8000,375,Mapping::logarithmic,0,true,"all",Transition::continuous,50},
    {"delay.tap03.level_db",tap3Level,"Tap 3 Level","dB",-120,0,-12,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap03.pan_pct",tap3Pan,"Tap 3 Pan","%",-100,100,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap04.enabled",tap4Enabled,"Tap 4 Enabled","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,20,offOn},
    {"delay.tap04.time_ms",tap4Time,"Tap 4 Time","ms",1,8000,500,Mapping::logarithmic,0,true,"all",Transition::continuous,50},
    {"delay.tap04.level_db",tap4Level,"Tap 4 Level","dB",-120,0,-12,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap04.pan_pct",tap4Pan,"Tap 4 Pan","%",-100,100,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap01.level_db",tap1Level,"Tap 1 Level","dB",-120,0,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap02.level_db",tap2Level,"Tap 2 Level","dB",-120,0,0,Mapping::linear,0,true,"all",Transition::continuous,20},
    {"delay.tap01.pan_pct",tap1Pan,"Tap 1 Pan","%",-100,100,-100,Mapping::linear,0,true,"stereo output",Transition::continuous,20},
    {"delay.tap02.pan_pct",tap2Pan,"Tap 2 Pan","%",-100,100,100,Mapping::linear,0,true,"stereo output",Transition::continuous,20},
    {"delay.tap01.enabled",tap1Enabled,"Tap 1 Enabled","",0,1,1,Mapping::linear,1,true,"all",Transition::discrete,20,offOn},
    {"delay.tap02.enabled",tap2Enabled,"Tap 2 Enabled","",0,1,1,Mapping::linear,1,true,"all",Transition::discrete,20,offOn}
};
inline constexpr ParameterRegistry registry{parameters,std::size(parameters)};
inline const ParameterSpec& spec(ParamID id) noexcept {return parameters[registry.index(id)];}
inline double value(const SoundState& s,ParamID id) noexcept {const auto& p=spec(id);double v=s.targets[registry.index(id)];return v==p.toNormalized(p.initial)?p.initial:p.toPhysical(v);}
inline void set(SoundState& s,ParamID id,double v) noexcept {s.targets[registry.index(id)]=spec(id).toNormalized(v);}
inline double noteBeats(int ordinal) noexcept {
    ordinal=std::clamp(ordinal,0,20);
    return (1.0/16.0)*double(1u<<(ordinal%7))*(ordinal/7==1?1.5:ordinal/7==2?2.0/3.0:1.0);
}
inline bool simpleTimeEditable(const SoundState& s) noexcept {
    return value(s,route)!=1 && value(s,syncL)==0 && value(s,syncR)==0 &&
        s.targets[registry.index(timeL)]==s.targets[registry.index(timeR)];
}
}
namespace just::module {inline constexpr auto& parameters=delay::parameters;}
