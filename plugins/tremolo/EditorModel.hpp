#pragma once
#include "Amount.hpp"
#include "Dsp.hpp"
#include <memory>
#include <string>

namespace just::tremolo {
// UI-thread adapter. It owns gestures, never DSP or sound state.
class EditorModel {
    EditorServices services{};
    ParamID active=~ParamID(0);bool editing=false;
public:
    explicit EditorModel(EditorServices s):services(s) {}
    ~EditorModel(){cancel();}
    double target(ParamID id) const noexcept {
        return services.readTarget?services.readTarget(services.owner,id):spec(id).toNormalized(spec(id).initial);
    }
    double value(ParamID id) const noexcept {return physicalValue(spec(id),target(id));}
    bool begin(ParamID id) {
        if(editing && active==id)return true;
        cancel();
        if(!services.beginEdit || !services.performEdit || !services.endEdit)return false;
        editing=services.beginEdit(services.owner,id);active=id;return editing;
    }
    bool writeNormalized(ParamID id,double normalized) {
        if(!std::isfinite(normalized) || normalized<0 || normalized>1 || !begin(id))return false;
        return services.performEdit(services.owner,id,normalized);
    }
    bool writePhysical(ParamID id,double physical) {
        if(!std::isfinite(physical) || physical<spec(id).minimum || physical>spec(id).maximum)return false;
        return writeNormalized(id,spec(id).toNormalized(physical));
    }
    bool writeAmount(double db) {
        double next=0;
        if(!depthForAmount(db,value(mix)*0.01,next))return false;
        return writeNormalized(depth,next);
    }
    void cancel() {
        if(editing){services.endEdit(services.owner,active);editing=false;active=~ParamID(0);}
    }
    bool frequencyEditable() const noexcept {return value(sync)<0.5;}
    double amount() const noexcept {return amountDb(value(depth)*0.01,value(mix)*0.01);}
    bool amountEditable() const noexcept {return value(mix)>0;}
    const EditorServices& editorServices() const noexcept {return services;}
    bool separationEditable() const noexcept {BusLayoutSnapshot layout;return services.readBusLayout && services.readBusLayout(services.owner,layout) && (layout.validFields&layoutBuses) && layout.outputChannels==2;}
    static constexpr const char* separationReason="Host channel context pending common bridge; saved/automated value is preserved.";
};
}
