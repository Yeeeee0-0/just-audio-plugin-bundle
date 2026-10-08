#pragma once
#include "Dsp.hpp"
#include "common/vst3/Module.hpp"
#include <cstdio>
#include "common/ui/DisplayFormat.hpp"
namespace just::distortion {
// Presentation order is independent of the immutable host enum ordinals.
inline constexpr unsigned modelDisplayOrder[]={1,2,3,4,5,0};
inline const char* parameterLabelZh(ID id) noexcept {
    switch(id){
        case model:return "模型";case input_db:return "输入";case output_db:return "输出";
        case drive_db:return "驱动";case boost:return "提升";case bias:return "偏置";
        case soft_shape:return "Soft 塑形";case hard_softness:return "Hard 柔化";
        case asym_shape:return "Asym 塑形";case fold_shape:return "Fold 塑形";
        case pre_hp_enabled:return "前级高通开关";case pre_hp_hz:return "前级高通";case pre_tilt_db:return "前级倾斜";
        case post_bass_db:return "低频";case post_treble_db:return "高频";
        case post_lp_enabled:return "高切开关";case post_lp_hz:return "高切";
        case crush_bits:return "位深";case crush_hold_hz:return "保持速率";case crush_aa:return "Crush 抗混叠";case crush_dither:return "抖动";
        case makeup_db:return "补偿增益";case drive_comp:return "驱动补偿";case match_gain_db:return "匹配增益";
        case mix:return "混合";case quality:return "品质";default:return nullptr;
    }
}
inline bool visibleInSimple(ID id,Model active) noexcept {
    return id==model || id==mix || (active==Model::Crush
        ? id==crush_bits || id==crush_hold_hz
        : id==drive_db);
}
inline bool simpleHasCustom(const SoundState& state) noexcept {
    const auto active=static_cast<Model>(int(physical(state,model)));
    for(const auto& p:parameters)
        if(p.id!=bypass && !visibleInSimple(static_cast<ID>(p.id),active)
           && state.targets[registry.index(p.id)]!=p.toNormalized(p.initial))return true;
    return false;
}
struct EditorModel {
    EditorServices services{};
    double normalized(ID id) const noexcept {return services.readTarget?services.readTarget(services.owner,id):parameters[registry.index(id)].toNormalized(parameters[registry.index(id)].initial);}
    double target(ID id) const noexcept {const auto& p=parameters[registry.index(id)];auto n=normalized(id);return n==p.toNormalized(p.initial)?p.initial:p.toPhysical(n);}
    Model currentModel() const noexcept {return static_cast<Model>(int(target(model)));}
    bool write(ID id,double n,bool gesture=true) const {
        if(!std::isfinite(n) || !services.performEdit)return false;
        n=std::clamp(n,0.0,1.0);
        if(gesture && (!services.beginEdit || !services.beginEdit(services.owner,id)))return false;
        bool result=services.performEdit(services.owner,id,n);
        if(gesture && services.endEdit)services.endEdit(services.owner,id);
        return result;
    }
    bool hiddenCustom() const noexcept {
        for(const auto& p:parameters){
            if(p.id==bypass || p.id==model || p.id==mix)continue;
            bool visible=visibleInSimple(static_cast<ID>(p.id),currentModel());
            if(!visible && normalized(static_cast<ID>(p.id))!=p.toNormalized(p.initial))return true;
        }
        return false;
    }
    std::string text(ID id) const {
        const auto& p=parameters[registry.index(id)];auto v=target(id);
        if(p.enumLabels)return p.enumLabels[int(v)];
        char number[64],buffer[80];formatDisplay(p,normalized(id),DisplayContext::simple,{},number,sizeof(number));std::snprintf(buffer,sizeof(buffer),"%s%s%s",number,*p.unit?" ":"",p.unit);return buffer;
    }
};
EditorContent* createEditorContent();
}
