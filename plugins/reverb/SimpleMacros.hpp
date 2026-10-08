#pragma once
#include "Parameters.hpp"
#include "common/ui/Controls.hpp"
#include <limits>

namespace just::reverb {
// UI projections only. These are never registered as new host parameters and
// never stored in sound state. Every displayed value comes from existing IDs.
enum class SimpleMacro : unsigned { brightness, character, distance, space, decayRate, width, mix };
inline constexpr std::size_t simpleMacroCount=7;
// Direct user confirmation on 2026-10-02 restored the concrete seven-macro
// wiring authorization; all writes below still target the legacy registry.
inline constexpr bool macroWiringApproved=true;
struct MacroBinding {
    ParameterSpec display;
    std::array<ParamID,3> ids;
    std::size_t count;
};
inline constexpr MacroBinding simpleMacros[]={
    {{"ui.reverb.brightness",HighCut,"Brightness","%",0,100,0,Mapping::linear}, {HighCut,HighDecay},2},
    {{"ui.reverb.character",ModDepth,"Character","%",0,100,10,Mapping::linear}, {ModDepth,ModRate,Diffusion},3},
    {{"ui.reverb.distance",EarlyLate,"Distance","%",0,100,65,Mapping::linear}, {EarlyLate,Diffusion},2},
    {{"ui.reverb.space",Decay,"Space","s",.1,20,1.2,Mapping::logarithmic}, {Decay,Size},2},
    {{"ui.reverb.decay-rate",MidDecay,"Decay Rate","%",25,200,100,Mapping::logarithmic}, {LowDecay,MidDecay,HighDecay},3},
    {{"ui.reverb.width",Width,"Stereo Width","%",0,150,100,Mapping::linear}, {Width},1},
    {{"ui.reverb.mix",Mix,"Mix","%",0,100,20,Mapping::linear}, {Mix},1},
};
static_assert(std::size(simpleMacros)==simpleMacroCount);
inline const MacroBinding& macroBinding(SimpleMacro macro) noexcept {return simpleMacros[static_cast<unsigned>(macro)];}
inline double macroNormalized(SimpleMacro macro,const SoundState& state) noexcept {
    const auto& binding=macroBinding(macro);
    const auto id=macro==SimpleMacro::decayRate?MidDecay:binding.ids[0];
    return state.targets[registry.index(id)];
}
inline double macroDefault(SimpleMacro macro) noexcept {
    const auto id=macro==SimpleMacro::decayRate?MidDecay:macroBinding(macro).ids[0];
    const auto& parameter=parameters[registry.index(id)];return parameter.toNormalized(parameter.initial);
}
inline ParameterSpec macroDisplaySpec(SimpleMacro macro) noexcept {
    auto spec=macroBinding(macro).display;spec.initial=spec.toPhysical(macroDefault(macro));return spec;
}
inline std::array<double,2> decayRateBounds(const SoundState& anchor) noexcept {
    double minimum=0,maximum=std::numeric_limits<double>::infinity();
    const double mid=physical(anchor,MidDecay);
    for(auto id:{LowDecay,MidDecay,HighDecay}) {
        const auto& spec=parameters[registry.index(id)];const auto original=physical(anchor,id);
        minimum=std::max(minimum,spec.minimum/original);maximum=std::min(maximum,spec.maximum/original);
    }
    return {100*mid*minimum,100*mid*maximum};
}
inline SoundState macroTarget(SimpleMacro macro,double normalized,const SoundState& anchor) noexcept {
    auto state=anchor;
    if(!std::isfinite(normalized))return state;
    normalized=std::clamp(normalized,0.,1.);
    if(normalized==macroNormalized(macro,anchor))return state;
    state.targets[registry.index(macro==SimpleMacro::decayRate?MidDecay:macroBinding(macro).ids[0])]=normalized;
    const double delta=normalized-macroNormalized(macro,anchor);
    if(macro==SimpleMacro::brightness) {
        set(state,HighDecay,physical(anchor,HighDecay)*std::pow(physical(state,HighCut)/physical(anchor,HighCut),.6));
    } else if(macro==SimpleMacro::character) {
        set(state,ModRate,physical(anchor,ModRate)*std::pow(4.,delta));
        set(state,Diffusion,physical(anchor,Diffusion)+40*delta);
    } else if(macro==SimpleMacro::distance) {
        set(state,Diffusion,physical(anchor,Diffusion)+20*delta);
    } else if(macro==SimpleMacro::space) {
        set(state,Size,physical(anchor,Size)*std::pow(physical(state,Decay)/physical(anchor,Decay),.3));
    } else if(macro==SimpleMacro::decayRate) {
        const double target=parameters[registry.index(MidDecay)].toPhysical(normalized);
        const auto bounds=decayRateBounds(anchor);const double mid=physical(anchor,MidDecay);
        // One shared clamp preserves spectral ratios at the cohort boundary.
        // Do not clamp each band independently or multiply Decay a second time.
        const double factor=std::clamp(target/mid,bounds[0]/(100*mid),bounds[1]/(100*mid));
        if(factor==1)return anchor;
        for(auto id:{LowDecay,MidDecay,HighDecay})set(state,id,physical(anchor,id)*factor);
    }
    return state;
}
inline SoundState macroResetTarget(SimpleMacro macro,const SoundState& anchor) noexcept {
    return macroTarget(macro,macroDefault(macro),anchor);
}
// One host gesture per existing ID. A multi-band edit keeps its start-state
// ratios throughout the drag, rather than accumulating rounding or clamping.
class MacroGesture {
    EditorServices services{};
    SoundState anchor{};
    SimpleMacro macro=SimpleMacro::mix;
    std::unique_ptr<MultiParameterGesture> gesture;
    bool active=false;
public:
    ~MacroGesture(){end();}
    bool begin(const EditorServices& source,SimpleMacro control,const SoundState& targets) {
        end();services=source;macro=control;anchor=targets;
        if(!source.beginEdit || !source.performEdit || !source.endEdit)return false;
        const auto& binding=macroBinding(macro);
        gesture=std::make_unique<MultiParameterGesture>(source);
        active=gesture->begin(binding.ids.data(),binding.count);return active;
    }
    bool update(double normalized) {
        if(!active || !std::isfinite(normalized))return false;
        return write(macroTarget(macro,normalized,anchor));
    }
    bool reset(){return active && write(macroResetTarget(macro,anchor));}
    void end() noexcept {if(gesture)gesture->end();active=false;}
private:
    bool write(const SoundState& target) {
        const auto& binding=macroBinding(macro);std::array<double,3> values{};
        for(std::size_t n=0;n<binding.count;++n)values[n]=target.targets[registry.index(binding.ids[n])];
        if(services.readTarget) {
            bool changed=false;for(std::size_t n=0;n<binding.count;++n)changed|=values[n]!=services.readTarget(services.owner,binding.ids[n]);
            if(!changed)return true;
        }
        return gesture->write(values.data(),binding.count);
    }
};
}
