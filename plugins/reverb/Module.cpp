#include "common/vst3/Module.hpp"
#include "ReverbEngine.hpp"
#include "Editor.hpp"
#include "Presets.hpp"
#include "SimpleMacros.hpp"
#include "SimpleLayout.hpp"
#include "common/ui/VisualAssets.hpp"
namespace just {
namespace {
using namespace reverb;
Engine* createEngine(){return new ReverbEngine;}
const SimpleControlBinding bindings[]={
    {"Brightness",{HighCut,HighDecay},2,true,nullptr},
    {"Character",{ModDepth,ModRate,Diffusion},3,true,nullptr},
    {"Distance",{EarlyLate,Diffusion},2,true,nullptr},
    {"Space",{Decay,Size},2,true,nullptr},
    {"Decay Rate",{LowDecay,MidDecay,HighDecay},3,true,nullptr},
    {"Stereo Width",{Width},1,true,nullptr},
    {"Mix",{Mix},1,true,nullptr}
};
const ParamID space[]={Style,Size,Decay,Predelay,Sync,Division,EarlyLate,Diffusion};
const ParamID decay[]={LowDecay,MidDecay,HighDecay,LowXover,HighXover};
const ParamID modulation[]={ModRate,ModDepth};
const ParamID wetEQ[]={WetHP,HighCut,Bell1Freq,Bell1Gain,Bell1Q,Bell1On,Bell2Freq,Bell2Gain,Bell2Q,Bell2On};
const ParamID dynamics[]={DuckAmount,DuckThreshold,DuckAttack,DuckRelease};
const ParamID output[]={Width,WetLevel,Mix,InputTrim,OutputTrim,Freeze};
const AdvancedGroup groups[]={
    {"Space",space,std::size(space)},{"Frequency decay",decay,std::size(decay)},
    {"Modulation",modulation,std::size(modulation)},{"Wet EQ",wetEQ,std::size(wetEQ)},
    {"Ducking",dynamics,std::size(dynamics)},{"Output and Freeze",output,std::size(output)}
};
StatusSnapshot status(const SoundState& s) {
    StatusSnapshot snapshot;snapshot.freeze=physical(s,Freeze)>=.5;
    const int style=int(physical(s,Style));const bool sync=physical(s,Sync)>=.5;
    constexpr const char* names[]={"Room","Hall","Plate"};
    constexpr const char* syncNames[]={"Room · Host Sync","Hall · Host Sync","Plate · Host Sync"};
    snapshot.mode=sync?syncNames[style]:names[style];
    return snapshot;
}
const ModuleDefinition definition{registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),createEditor,status,{true,true,true,false},nullptr,nullptr,factoryPresets,std::size(factoryPresets),nullptr,nullptr,880,SimpleLayout::fit(880).height+ShellGeometry::header+ShellGeometry::footer};
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
