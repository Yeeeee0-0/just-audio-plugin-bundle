#pragma once
#include "EditorModel.hpp"
#include "common/ui/DisplayFormat.hpp"

namespace just::tremolo {
// UI coordinate only: upward travel increases the displayed dB Amount toward
// zero. Host Depth keeps its original 0..1 normalized mapping and ParamID.
struct AmountDisplay {
    EditorServices host;
    explicit AmountDisplay(EditorServices services):host(services){}
    double mixValue() const noexcept {
        return host.readTarget?host.readTarget(host.owner,mix):spec(mix).toNormalized(spec(mix).initial);
    }
    static double read(void* p,ParamID id) {
        auto& self=*static_cast<AmountDisplay*>(p);
        const double n=self.host.readTarget?self.host.readTarget(self.host.owner,id):spec(id).toNormalized(spec(id).initial);
        return id==depth?1-n:n;
    }
    static bool begin(void* p,ParamID id) {
        auto& self=*static_cast<AmountDisplay*>(p);return self.host.beginEdit && self.host.beginEdit(self.host.owner,id);
    }
    static bool perform(void* p,ParamID id,double n) {
        auto& self=*static_cast<AmountDisplay*>(p);
        return self.host.performEdit && std::isfinite(n) && n>=0 && n<=1 && self.host.performEdit(self.host.owner,id,id==depth?1-n:n);
    }
    static void end(void* p,ParamID id) {
        auto& self=*static_cast<AmountDisplay*>(p);if(self.host.endEdit)self.host.endEdit(self.host.owner,id);
    }
    EditorServices controlServices() noexcept {
        EditorServices result{};result.owner=this;result.view=host.view;
        result.readTarget=read;result.beginEdit=begin;result.performEdit=perform;result.endEdit=end;
        return result;
    }
    ParameterSpec controlSpec() const noexcept {
        auto result=spec(depth);result.title="Amount";result.unit="dB";
        result.minimum=0;result.maximum=1;result.initial=1-spec(depth).toNormalized(spec(depth).initial);
        return result;
    }
    void format(double coordinate,DisplayContext context,char* out,std::size_t size) const noexcept {
        const double db=amountDb(1-coordinate,mixValue());
        if(std::isinf(db))std::snprintf(out,size,"−∞");
        else if(context==DisplayContext::editing)std::snprintf(out,size,"%.17g",db);
        else std::snprintf(out,size,"%.1f",db==0?0:db);
    }
    bool parse(const char* text,double& coordinate) const noexcept {
        if(!text)return false;
        double db=0;
        if(!std::strcmp(text,"−∞") || !std::strcmp(text,"-∞") || !std::strcmp(text,"-inf"))db=-INFINITY;
        else {
            char* end=nullptr;db=std::strtod(text,&end);
            while(end && (*end==' ' || *end=='\t'))++end;
            if(end==text || !end || (*end && std::strcmp(end,"dB")))return false;
        }
        double next=0;if(!depthForAmount(db,mixValue(),next))return false;
        coordinate=1-next;return true;
    }
};
}
