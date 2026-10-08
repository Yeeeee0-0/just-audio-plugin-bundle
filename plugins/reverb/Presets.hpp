#pragma once
#include "Parameters.hpp"
#include "common/state/Preset.hpp"
namespace just::reverb {
struct Preset {
    const char* name;
    const char* category;
    const char* description;
    int style;
    double decay,predelay,cut,mix,size,late,diffusion,low,high,width,modDepth,duck;
    double wetHP=80,modRate=.3,highCrossover=4000;
};
// Original JUST tuning. Competitor manuals inform categories and use cases;
// their preset data, proprietary algorithms and branded names are not copied.
inline constexpr Preset presets[]={
    {"Init","Room","Balanced insert starting point",0,1.2,20,12000,20,100,65,70,1,.65,100,.2,0},
    {"Foley Close Room","Room","Close foley and footsteps",0,.65,18,6500,18,80,50,85,.85,.6,90,0,0,100,.3,3000},
    {"Studio Room","Room","Natural space for instruments and foley",0,.9,12,11000,20,85,58,75,.9,.7,100,.12,0},
    {"Drum Room","Room","Compact, dense percussive ambience",0,.65,9,10000,22,65,50,88,.8,.6,110,.05,0},
    {"Dialogue Tight Room","Room","Short matching ambience with a focused center",0,.45,8,7000,12,65,40,75,.8,.65,65,0,0,140,.3,3000},
    {"Natural Hall","Hall","Clear attacks followed by a spacious tail",1,1.9,30,6000,22,115,70,85,1.2,.55,110,.12,0,80,.35,3000},
    {"Concert Hall","Hall","Warm, broad orchestral space",1,3.5,38,7800,26,140,82,88,1.25,.6,115,.35,0},
    {"Cinematic Hall","Hall","Creative spacious tail for game impacts",1,3.2,40,5500,28,150,85,90,1.15,.45,120,.25,0,100,.35,3000},
    {"Warm Voice Plate","Plate","Separated attack and a darker sustained tail",2,1.8,25,8000,18,120,95,95,1,.5,100,.08,0,160,.3,3000},
    {"Bright Plate","Plate","Dense bright tail for percussion and synths",2,2.2,10,16000,22,100,94,94,.75,1.1,115,.08,0},
    {"Dark Plate","Plate","Smooth darker sustaining ambience",2,3.1,18,5400,24,120,92,92,1,.55,110,.2,0},
    {"Cave Tail","Sound Design","Long diffuse game ambience",1,7.5,75,4500,32,190,90,90,1.4,.45,125,.45,0},
    {"Metal Chamber","Sound Design","Reflective, compact mechanical space",2,1.9,7,13500,23,65,68,52,.8,1.05,105,.03,0},
    {"Short Neutral Plate","Plate","Fast dense tail for UI accents and short hits",2,.8,0,10000,15,100,90,95,.8,.6,100,0,0,100,.3,3000}
};
inline SoundState presetState(std::size_t index,Uid uid={}) noexcept {
    SoundState s=initialState(uid,registry);
    const auto& p=presets[std::min(index,std::size(presets)-1)];
    set(s,Style,p.style);set(s,Decay,p.decay);set(s,Predelay,p.predelay);set(s,HighCut,p.cut);set(s,Mix,p.mix);
    set(s,Size,p.size);set(s,EarlyLate,p.late);set(s,Diffusion,p.diffusion);set(s,LowDecay,p.low);set(s,HighDecay,p.high);
    set(s,Width,p.width);set(s,ModDepth,p.modDepth);set(s,DuckAmount,p.duck);
    set(s,WetHP,p.wetHP);set(s,ModRate,p.modRate);set(s,HighXover,p.highCrossover);
    // Every other hidden parameter is explicitly initialized, including wet EQ,
    // Sync and Freeze. State stores targets and seed, not sounding delay buffers.
    return s;
}
inline int matchingPreset(const SoundState& s) noexcept {
    for(std::size_t p=0;p<std::size(presets);++p) {
        auto candidate=presetState(p,s.plugin);bool equal=true;
        for(std::size_t i=1;i<registry.count;++i)equal&=std::abs(s.targets[i]-candidate.targets[i])<1e-9;
        if(equal)return int(p);
    }
    return -1;
}
template<std::size_t I> inline bool buildFactoryPreset(Uid uid,SoundState& out) {
    out=presetState(I,uid);return true;
}
inline constexpr FactoryPreset factoryPresets[]={
    {"just.reverb.init",presets[0].name,presets[0].category,buildFactoryPreset<0>},
    {"just.reverb.foley-close-room",presets[1].name,presets[1].category,buildFactoryPreset<1>},
    {"just.reverb.studio-room",presets[2].name,presets[2].category,buildFactoryPreset<2>},
    {"just.reverb.drum-room",presets[3].name,presets[3].category,buildFactoryPreset<3>},
    {"just.reverb.dialogue-tight-room",presets[4].name,presets[4].category,buildFactoryPreset<4>},
    {"just.reverb.natural-hall",presets[5].name,presets[5].category,buildFactoryPreset<5>},
    {"just.reverb.concert-hall",presets[6].name,presets[6].category,buildFactoryPreset<6>},
    {"just.reverb.cinematic-hall",presets[7].name,presets[7].category,buildFactoryPreset<7>},
    {"just.reverb.warm-voice-plate",presets[8].name,presets[8].category,buildFactoryPreset<8>},
    {"just.reverb.bright-plate",presets[9].name,presets[9].category,buildFactoryPreset<9>},
    {"just.reverb.dark-plate",presets[10].name,presets[10].category,buildFactoryPreset<10>},
    {"just.reverb.cave-tail",presets[11].name,presets[11].category,buildFactoryPreset<11>},
    {"just.reverb.metal-chamber",presets[12].name,presets[12].category,buildFactoryPreset<12>},
    {"just.reverb.short-neutral-plate",presets[13].name,presets[13].category,buildFactoryPreset<13>}
};
static_assert(std::size(factoryPresets)==std::size(presets));
}
