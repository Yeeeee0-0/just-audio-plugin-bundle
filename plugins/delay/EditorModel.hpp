#pragma once
#include "Parameters.hpp"
#include "Labels.hpp"
#include "common/vst3/Module.hpp"
#include "common/ui/DisplayFormat.hpp"
namespace just::delay {
inline SoundState editorTargets(const EditorServices& services) noexcept {
    SoundState s;for(std::size_t i=0;i<registry.count;++i)s.targets[i]=services.readTarget(services.owner,parameters[i].id);return s;
}
// A paired explicit UI gesture. Two independent host parameters remain independent.
// Grouped host Undo is not claimed by the current shared editing API.
class Gesture {
    EditorServices services{};std::array<ParamID,2> ids{};std::size_t count=0;
public:
    explicit Gesture(EditorServices s):services(s){}
    ~Gesture(){end();}
    bool begin(ParamID id) {
        end();auto s=editorTargets(services);
        if(id==timeL && !services.view->advanced && !simpleTimeEditable(s))return false;
        if(!services.beginEdit(services.owner,id))return false;
        ids[0]=id;count=1;
        if(id==timeL && !services.view->advanced) {
            if(!services.beginEdit(services.owner,timeR)){end();return false;}
            ids[1]=timeR;count=2;
        }
        return true;
    }
    bool update(double normalized) {
        if(!count || !std::isfinite(normalized) || normalized<0 || normalized>1)return false;
        // If host automation exits the Simple subspace mid-gesture, end it immediately.
        if(count==2 && !simpleTimeEditable(editorTargets(services))){end();return false;}
        bool ok=true;for(std::size_t i=0;i<count;++i)ok=services.performEdit(services.owner,ids[i],normalized)&&ok;
        return ok;
    }
    void end(){while(count)services.endEdit(services.owner,ids[--count]);}
    bool active() const noexcept {return count!=0;}
};
inline bool editable(ParamID id,const SoundState& s,bool advanced) noexcept {
    if(id==timeL)return advanced?value(s,syncL)==0:simpleTimeEditable(s);
    if(id==timeR)return value(s,syncR)==0;
    if(id==noteL)return value(s,syncL)!=0;
    if(id==noteR)return value(s,syncR)!=0;
    if(id==crossfeed)return value(s,route)!=2;
    if(id==start)return value(s,route)==2;
    return true;
}
// Adapt the public single-control service to existing module gestures. The
// original service remains the only owner of actual controller targets.
class ControlServicesAdapter {
    EditorServices source;
    Gesture gesture;
    ParamID parameter;
public:
    ControlServicesAdapter(EditorServices services,ParamID id):source(services),gesture(services),parameter(id){}
    EditorServices services() noexcept {
        EditorServices adapted;adapted.owner=this;adapted.view=source.view;
        adapted.readTarget=[](void* owner,ParamID id){auto& self=*static_cast<ControlServicesAdapter*>(owner);return self.source.readTarget(self.source.owner,id);};
        adapted.beginEdit=[](void* owner,ParamID id){auto& self=*static_cast<ControlServicesAdapter*>(owner);return id==self.parameter && editable(id,editorTargets(self.source),self.source.view->advanced) && self.gesture.begin(id);};
        adapted.performEdit=[](void* owner,ParamID id,double n){auto& self=*static_cast<ControlServicesAdapter*>(owner);if(id!=self.parameter || !editable(id,editorTargets(self.source),self.source.view->advanced)){self.gesture.end();return false;}return self.gesture.update(n);};
        adapted.endEdit=[](void* owner,ParamID){static_cast<ControlServicesAdapter*>(owner)->gesture.end();};
        return adapted;
    }
    void end() noexcept {gesture.end();}
};
inline bool muteEndpoint(ParamID id) noexcept {return id==wetLevel || id==tap1Level || id==tap2Level || id==tap3Level || id==tap4Level;}
inline void formatDisplay(const SoundState& s,ParamID id,char* out,std::size_t capacity) noexcept {
    const auto& p=spec(id);
    if(p.enumLabels){p.format(s.targets[registry.index(id)],out,capacity);return;}
    char number[64];DisplayPolicy policy;policy.muteAtMinimum=muteEndpoint(id);policy.muteText="-∞";
    just::formatDisplay(p,s.targets[registry.index(id)],DisplayContext::simple,policy,number,sizeof(number));
    std::snprintf(out,capacity,"%s %s",number,p.unit);
}
inline bool parseDisplay(ParamID id,const char* text,double& normalized) noexcept {
    if(!text)return false;
    if(muteEndpoint(id) && (std::strcmp(text,"-∞")==0 || std::strcmp(text,"-∞ dB")==0 || std::strcmp(text,"−∞")==0 || std::strcmp(text,"−∞ dB")==0 || std::strcmp(text,"-inf")==0 || std::strcmp(text,"-inf dB")==0)){normalized=0;return true;}
    return spec(id).parse(text,normalized);
}
inline DisplayPolicy controlPolicy(ParamID id,bool custom=false,bool simple=false,EditorViewState* view=nullptr) noexcept {
    DisplayPolicy policy;policy.muteAtMinimum=muteEndpoint(id);policy.muteText="-∞";
    policy.labelZh=simple && id==timeL?"时间":chineseLabel(id);
    if(custom){policy.context=view;policy.formatWithContext=[](void* context,double,DisplayContext,char* out,std::size_t size){
        const auto* view=static_cast<const EditorViewState*>(context);
        std::snprintf(out,size,"%s",view && view->language==UiLanguage::chinese?"自定义":"Custom");
    };}
    return policy;
}
// View-only return route. Reading targets never changes saved sound state.
// A preset/old project that opens directly in Ping-Pong has no saved prior
// route; the only deterministic Off fallback is Stereo.
class PingPongRoute {
    int previous=0;
public:
    void observe(const SoundState& state) noexcept {
        const int current=int(value(state,route));
        if(current==0 || current==1)previous=current;
    }
    int next(const SoundState& state) noexcept {
        observe(state);
        return value(state,route)==2?previous:2;
    }
    int returnRoute() const noexcept {return previous;}
};
EditorContent* createEditorContent();
}
