#pragma once
#include "../state/State.hpp"
#include <string>
namespace just {
// Native VST3 ViewRect units throughout: NSView points on macOS, HWND pixels
// on Windows. Backing/Retina scale is not applied a second time. The legacy
// v1 scale is retained as metadata; v2 renderScale is explicit host-approved zoom.
enum class UiLanguage { chinese,english };
struct EditorViewState {
    int width=720,height=420;double scale=1;bool advanced=false;
    // UI chunk v2 preferences; never parameters or serialized sound state.
    UiLanguage language=UiLanguage::english; // fresh instances and legacy UI chunks without a language
    bool backgroundEnabled=false,reduceMotion=false,lowPerformance=false;
    int backgroundFps=30;
    double renderScale=1; // old serialized scale was metadata; activates only on explicit UI request
    // Runtime presentation only; never serialized. Parameter editing/refresh
    // continues while measurement visuals are paused. Clear local display
    // histories/holds on a new resume generation before consuming fresh data.
    bool visualsPaused=false;
    std::uint64_t visualResumeGeneration=0;
};
inline const char* localized(const EditorViewState& v,const char* zh,const char* en) noexcept {return v.language==UiLanguage::chinese?zh:en;}
struct EditorSizeLimits {
    static constexpr int defaultMinimumWidth=480,defaultMinimumHeight=260;
    static constexpr int maximumWidth=1920,maximumHeight=1200;
    int minimumWidth=defaultMinimumWidth,minimumHeight=defaultMinimumHeight;
    void constrain(int& width,int& height) const noexcept {
        width=std::clamp(width,minimumWidth,maximumWidth);
        height=std::clamp(height,minimumHeight,maximumHeight);
    }
};
struct StatusSnapshot {
    bool advancedCustom=false,sidechainMissing=false,sidechainSilent=false,syncUnavailable=false;
    bool freeze=false,monoCheck=false,bypassProtectionExit=false,qualityPending=false;
    const char* mode="Foundation pass-through";
};
inline std::string statusText(const StatusSnapshot& status,bool bypass) {
    std::string text=status.mode?status.mode:"";
    auto add=[&](bool visible,const char* label){if(visible){if(!text.empty())text+=" · ";text+=label;}};
    add(bypass,"Bypass");add(status.advancedCustom,"Advanced: Custom");
    add(status.sidechainMissing,"Sidechain missing");add(status.sidechainSilent,"Sidechain silent");
    add(status.syncUnavailable,"Sync unavailable");add(status.freeze,"Freeze");
    add(status.monoCheck,"Mono Check");add(status.bypassProtectionExit,"Peak protection exited");
    add(status.qualityPending,"Quality pending");return text;
}
struct SimpleControlBinding {
    const char* label;std::array<ParamID,8> parameters{};std::size_t parameterCount=0;
    bool enabled=false;const char* disabledReason="Effect parameter registry is not approved";
};
struct AdvancedGroup {const char* name;const ParamID* parameters;std::size_t count;};
struct HeaderBar {inline static constexpr const char* tools[]={"Preset","Bypass","Advanced"};};
struct Theme {
    static constexpr std::uint32_t background=0xF4F5F7,text=0x19242C,accent=0x178F98;
};
class EditSink {
public:
    virtual ~EditSink()=default;
    virtual bool beginEdit(ParamID)=0;
    virtual bool performEdit(ParamID,double normalized)=0;
    virtual void endEdit(ParamID)=0;
};
class EditGesture {
    EditSink* sink=nullptr;ParamID id=0;bool active=false;
public:
    EditGesture(EditSink& s,ParamID p):sink(&s),id(p),active(s.beginEdit(p)){}
    EditGesture(const EditGesture&)=delete;
    ~EditGesture(){cancel();}
    bool update(double v){return active && std::isfinite(v) && v>=0 && v<=1 && sink->performEdit(id,v);}
    void cancel(){if(active){sink->endEdit(id);active=false;}}
};
inline bool hiddenParametersCustom(ParameterRegistry r,const SoundState& state,const SimpleControlBinding* bindings,std::size_t count) noexcept {
    for(std::size_t i=0;i<r.count;++i) {
        if(r.specs[i].id==bypassParamID)continue;
        bool visible=false;
        for(std::size_t b=0;b<count;++b)for(std::size_t p=0;p<bindings[b].parameterCount;++p)visible|=bindings[b].parameters[p]==r.specs[i].id;
        if(!visible && state.targets[i]!=r.specs[i].toNormalized(r.specs[i].initial))return true;
    }
    return false;
}
}
