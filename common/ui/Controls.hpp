#pragma once
#include "../vst3/Module.hpp"
#include "DisplayFormat.hpp"
#include "Interaction.hpp"
namespace just {
struct ControlGeometry {
    static constexpr int dial=98,cellWidth=112,cellHeight=148,valueHeight=24,labelHeight=22;
    static bool customTypography(const DisplayPolicy& p) noexcept {
        return p.style==ControlStyle::rotary && (p.valueFontSize>0 || p.labelFontSize>0 || p.valueFieldHeight>0);
    }
    static double labelPointSize(const DisplayPolicy& p) noexcept {
        return p.style==ControlStyle::rotary && p.labelFontSize>0?std::clamp(p.labelFontSize,8,24):11;
    }
    static double valuePointSize(double diameter,const DisplayPolicy& p) noexcept {
        return p.style==ControlStyle::rotary && p.valueFontSize>0?std::clamp(p.valueFontSize,8,32):(diameter>140?23:diameter<70?12:15);
    }
    static double labelFieldHeight(const DisplayPolicy& p) noexcept {return std::max(20.,labelPointSize(p)+6);}
    static double readoutHeight(const DisplayPolicy& p) noexcept {
        if(!customTypography(p))return 22;
        const double fontHeight=p.valueFontSize>0?valuePointSize(98,p)+8:22;
        return std::max(fontHeight,double(p.valueFieldHeight>0?std::clamp(p.valueFieldHeight,22,48):22));
    }
    static RotaryLayout layout(double width,double height,const DisplayPolicy& p={}) noexcept {
        return RotaryLayout::fit(width,height,readoutHeight(p),labelFieldHeight(p));
    }
};
// UI-only semantic binding. No synthetic host ParamID is introduced. The display
// spec describes physical mapping/units only; its id is ignored by this adapter.
struct RotaryBinding {
    void* owner=nullptr;EditorViewState* view=nullptr;
    double (*read)(void*)=nullptr;
    bool (*begin)(void*)=nullptr;
    bool (*write)(void*,double)=nullptr;
    void (*end)(void*)=nullptr;
};
// Native handle is owned by this object; destroy it before the module parent.
// All calls are UI-thread-only. A refresh never performs an edit.
class RotaryControl {
public:
    virtual ~RotaryControl()=default;
    virtual void resize(int x,int y,int width=ControlGeometry::cellWidth,int height=ControlGeometry::cellHeight)=0;
    virtual void refresh(bool enabled=true)=0;
    virtual void* nativeHandle() const noexcept=0;
    static std::unique_ptr<RotaryControl> create(void* parent,const EditorServices&,const ParameterSpec&,DisplayPolicy={});
    static std::unique_ptr<RotaryControl> create(void* parent,RotaryBinding,const ParameterSpec& displaySpec,DisplayPolicy={});
};
// Owns adapter storage for exactly as long as its native control.
inline std::unique_ptr<RotaryControl> RotaryControl::create(void* parent,RotaryBinding binding,const ParameterSpec& spec,DisplayPolicy policy) {
    class Bound final:public RotaryControl {
    public:
        RotaryBinding binding;std::unique_ptr<RotaryControl> native;
        void resize(int x,int y,int w,int h) override {native->resize(x,y,w,h);}
        void refresh(bool enabled) override {native->refresh(enabled);}
        void* nativeHandle() const noexcept override {return native->nativeHandle();}
    };
    if(!binding.read || !binding.begin || !binding.write || !binding.end)return {};
    auto result=std::make_unique<Bound>();result->binding=binding;
    EditorServices s;s.owner=&result->binding;s.view=binding.view;
    s.readTarget=[](void* p,ParamID){auto& b=*static_cast<RotaryBinding*>(p);return b.read(b.owner);};
    s.beginEdit=[](void* p,ParamID){auto& b=*static_cast<RotaryBinding*>(p);return b.begin(b.owner);};
    s.performEdit=[](void* p,ParamID,double v){auto& b=*static_cast<RotaryBinding*>(p);return b.write(b.owner,v);};
    s.endEdit=[](void* p,ParamID){auto& b=*static_cast<RotaryBinding*>(p);b.end(b.owner);};
    result->native=create(parent,s,spec,policy);if(!result->native)return {};return result;
}
// Bounded host gesture fanout. Module computes semantic values; this utility
// balances all successfully opened real IDs even when a later begin fails.
class MultiParameterGesture {
    EditorServices services{};std::array<ParamID,8> ids{};std::size_t active=0;
public:
    explicit MultiParameterGesture(EditorServices s):services(s){}
    ~MultiParameterGesture(){end();}
    MultiParameterGesture(const MultiParameterGesture&)=delete;
    bool begin(const ParamID* values,std::size_t count){
        end();if(!values || !count || count>ids.size() || !services.beginEdit || !services.endEdit)return false;
        for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<i;++j)if(values[i]==values[j])return false;
        for(std::size_t i=0;i<count;++i){if(!services.beginEdit(services.owner,values[i])){end();return false;}ids[active++]=values[i];}return true;
    }
    bool write(const double* normalized,std::size_t count){
        if(!normalized || count!=active || !count || !services.performEdit)return false;
        for(std::size_t i=0;i<count;++i)if(!std::isfinite(normalized[i]) || normalized[i]<0 || normalized[i]>1)return false;
        for(std::size_t i=0;i<count;++i)if(!services.performEdit(services.owner,ids[i],normalized[i]))return false;
        return true; // host edits are sequential, not a sound-state transaction
    }
    void end(){while(active)services.endEdit(services.owner,ids[--active]);}
};
}
