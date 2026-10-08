#pragma once
#include "Parameters.hpp"
#include "common/vst3/Module.hpp"
#include "common/ui/DisplayFormat.hpp"

namespace just::stereo {
// UI-only adapter. Construction, snapshots, resize and view switches never write.
class EditorModel {
    EditorServices services{};
    ParamID active=~ParamID(0);
    bool holdingMono=false;double previousMono=0;
public:
    explicit EditorModel(EditorServices s):services(s){}
    ~EditorModel(){releaseMono();end();}
    double read(ParamID id) const noexcept {
        return services.readTarget?services.readTarget(services.owner,id):spec(id).toNormalized(spec(id).initial);
    }
    void display(ParamID id,char* text,std::size_t capacity) const noexcept {
        const auto& p=spec(id);char number[64];formatDisplay(p,read(id),DisplayContext::simple,{},number,sizeof(number));
        std::snprintf(text,capacity,"%s%s%s",number,*p.unit?" ":"",p.unit);
    }
    bool begin(ParamID id) {
        if(holdingMono)releaseMono();
        end();
        if(!services.beginEdit || !services.performEdit || !services.endEdit || registry.index(id)==registry.count)return false;
        if(!services.beginEdit(services.owner,id))return false;
        active=id;return true;
    }
    bool update(double normalized) {
        return active!=~ParamID(0) && std::isfinite(normalized) && normalized>=0 && normalized<=1 &&
            services.performEdit(services.owner,active,normalized);
    }
    void end(){if(active!=~ParamID(0)){services.endEdit(services.owner,active);active=~ParamID(0);}}
    bool write(ParamID id,double normalized) {
        if(!begin(id))return false;const bool ok=update(normalized);end();return ok;
    }
    bool text(ParamID id,const char* value) {
        double normalized;if(!spec(id).parse(value,normalized))return false;return write(id,normalized);
    }
    bool holdMono() {
        if(holdingMono)return true;
        previousMono=read(Mono);
        if(!begin(Mono))return false;
        holdingMono=update(1);
        if(!holdingMono)end();return holdingMono;
    }
    void releaseMono() {
        if(holdingMono){update(previousMono);holdingMono=false;end();}
    }
    bool isHoldingMono() const noexcept{return holdingMono;}
    void toggleMono(){releaseMono();write(Mono,read(Mono)>=.5?0:1);}
};
EditorContent* createEditorContent();
}
