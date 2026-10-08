#pragma once
#include "common/vst3/Module.hpp"
#include "common/ui/DisplayFormat.hpp"
#include "GateEngine.hpp"
namespace just::gate {
inline double read(const EditorServices& services,ParamID id) noexcept {
    const auto& p=spec(id);
    const double value=services.readTarget?services.readTarget(services.owner,id):p.toNormalized(p.initial);
    return std::isfinite(value)?std::clamp(value,0.,1.):p.toNormalized(p.initial);
}
struct ControlAvailability {bool enabled=true;const char* reason="";};
inline ControlAvailability availability(ParamID id,const EditorServices& services) noexcept {
    const auto savedMode=unsigned(spec(mode).toPhysical(read(services,mode)));
    if((id==ratio || id==knee) && savedMode!=1)return {false,"Saved; applies only in Expand"};
    if((id==hold || id==hysteresis) && savedMode==1)return {false,"Saved; Expand uses a continuous curve"};
    if(id==scGain && read(services,scSource)<.5)return {false,"Saved; applies only to External SC"};
    if(id==scHPHz && read(services,scHPEnabled)<.5)return {false,"Saved; SC HP is Off"};
    if(id==scLPHz && read(services,scLPEnabled)<.5)return {false,"Saved; SC LP is Off"};
    if(id==lookahead)return {false,"Maximum Lookahead is fixed at 0 ms; delay compensation support pending"};
    return {};
}
inline bool writeOne(const EditorServices& s,ParamID id,double normalized) {
    if(!availability(id,s).enabled || !s.beginEdit || !s.performEdit || !s.endEdit ||
        !std::isfinite(normalized) || normalized<0 || normalized>1 || !s.beginEdit(s.owner,id))return false;
    const bool success=s.performEdit(s.owner,id,normalized);s.endEdit(s.owner,id);return success;
}
inline constexpr ParamID simpleIDs[]={threshold,range,attack,release};
// The approved Simple controls remain visible when Advanced adds the full table.
inline constexpr ParamID additionalIDs[]={hold,hysteresis,mode,ratio,knee,detector,scSource,scGain,scHPEnabled,scHPHz,scLPEnabled,scLPHz,input,output,lookahead};
inline const char* chineseLabel(ParamID id) noexcept {
    switch(id){
    case threshold:return "阈值";case range:return "衰减范围";case attack:return "启动";case release:return "释放";
    case hold:return "保持";case hysteresis:return "回差";case mode:return "模式";case ratio:return "扩展比";case knee:return "拐点";
    case detector:return "检测器";case scSource:return "侧链来源";case scGain:return "侧链增益";
    case scHPEnabled:return "侧链高通";case scHPHz:return "高通频率";case scLPEnabled:return "侧链低通";case scLPHz:return "低通频率";
    case input:return "输入";case output:return "输出";case lookahead:return "前瞻";default:return spec(id).title;
    }
}
inline DisplayPolicy displayPolicy(ParamID id) noexcept {DisplayPolicy policy;policy.labelZh=chineseLabel(id);return policy;}
inline const char* uiText(const EditorServices& services,const char* zh,const char* en) noexcept {
    return services.view?localized(*services.view,zh,en):en;
}
inline constexpr ParamID advancedIDs[]={mode,threshold,range,ratio,knee,hysteresis,attack,hold,release,detector,scSource,scGain,scHPEnabled,scHPHz,scLPEnabled,scLPHz,input,output,lookahead};
inline const char* help(ParamID id,unsigned currentMode) noexcept {
    if(id==range)return "Relative attenuation depth; never an absolute output floor";
    if(id==attack)return currentMode==2?"Duck attenuation entry speed; 63.2% dB step time":"Gain opens toward 0 dB; 63.2% dB step time";
    if(id==release)return currentMode==2?"Recovery after Duck; 63.2% dB step time":"Gain attenuation time; 63.2% dB step time";
    if(id==hold)return "Begins below Close threshold; return to Close cancels it. Changes apply to the next fall.";
    if(id==detector)return "Peak is instantaneous; RMS uses a fixed 5 ms exponential energy average";
    if(id==scSource)return "Missing External SC releases attenuation; never falls back to Internal";
    return "Edits the same saved host parameter in both views";
}
EditorContent* createEditorContent();
}
