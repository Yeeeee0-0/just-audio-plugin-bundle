#pragma once
#include "../parameters/Parameters.hpp"
#include <string>
namespace just {
enum class ControlStyle { rotary,vertical,horizontal };
enum class DisplayContext {simple,advanced,editing};
struct DisplayPolicy {
    int decimals=-1; // -1: one decimal; preserve small non-dB values where needed
    bool integer=false,muteAtMinimum=false;
    const char* muteText="-inf";
    void (*format)(double,DisplayContext,char*,std::size_t)=nullptr;
    bool (*parse)(const char*,double&)=nullptr;
    ControlStyle style=ControlStyle::rotary;
    const char* labelZh=nullptr; // optional UI-only translation, never parameter identity
    void* context=nullptr;
    void (*formatWithContext)(void*,double,DisplayContext,char*,std::size_t)=nullptr;
    bool (*parseWithContext)(void*,const char*,double&)=nullptr;
    bool dark=false; // dark floating panels; does not change geometry or interaction
    // Optional macOS rotary typography in logical points. Zero retains the
    // current automatic numeric font, label11 and field22; no value formatting.
    int valueFontSize=0,labelFontSize=0,valueFieldHeight=0;
};
inline void formatDisplay(const ParameterSpec& spec,double normalized,DisplayContext context,
                          const DisplayPolicy& policy,char* out,std::size_t size) noexcept {
    if(!out || !size)return;
    if(policy.formatWithContext){policy.formatWithContext(policy.context,normalized,context,out,size);return;}
    if(policy.format){policy.format(normalized,context,out,size);return;}
    if(spec.enumLabels && spec.stepCount){spec.format(normalized,out,size);return;}
    if(policy.muteAtMinimum && normalized<=0){std::snprintf(out,size,"%s",policy.muteText);return;}
    double value=spec.toPhysical(normalized);
    if(context==DisplayContext::editing){std::snprintf(out,size,"%.17g",value);return;}
    int decimals=policy.integer || spec.stepCount?0:policy.decimals;
    if(decimals<0){
        decimals=1;
        // dB is a signed logarithmic unit: its zero neighbourhood needs no
        // extra digits. In particular, normalized Init round trips can leave
        // a few floating-point ulps instead of an exact physical zero.
        const bool decibels=std::strcmp(spec.unit,"dB")==0 || std::strcmp(spec.unit,"dBFS")==0 || std::strcmp(spec.unit,"dBTP")==0;
        if(std::strcmp(spec.unit,"Hz")==0 && std::abs(value)<1)decimals=2;
        if(std::strcmp(spec.title,"Q")==0 || std::strcmp(spec.unit,"Q")==0)decimals=2;
        if(!decibels && value!=0 && std::abs(value)<.1)decimals=std::max(decimals,int(std::ceil(-std::log10(std::abs(value))))+1);
    }
    decimals=std::clamp(decimals,0,8);
    // Do not draw a negative zero. This affects text only.
    if(std::abs(value)<.5*std::pow(10.,-decimals))value=0;
    std::snprintf(out,size,"%.*f",decimals,value);
}
inline bool parseDisplay(const ParameterSpec& spec,const DisplayPolicy& policy,const char* text,double& normalized) noexcept {
    if(policy.parseWithContext)return policy.parseWithContext(policy.context,text,normalized) && std::isfinite(normalized) && normalized>=0 && normalized<=1;
    if(policy.parse)return policy.parse(text,normalized) && std::isfinite(normalized) && normalized>=0 && normalized<=1;
    if(policy.muteAtMinimum && text && (!std::strcmp(text,policy.muteText) || !std::strcmp(text,"−∞") || !std::strcmp(text,"-∞"))){normalized=0;return true;}
    return spec.parse(text,normalized);
}
inline const char* controlDisplayUnit(const ParameterSpec& spec,double normalized,bool editing,const DisplayPolicy& policy) noexcept {
    if((spec.enumLabels && spec.stepCount) || !std::strcmp(spec.unit,"enum") || !std::strcmp(spec.unit,"boolean"))return "";
    return !editing && !policy.format && !policy.formatWithContext && !std::strcmp(spec.unit,"Hz") && spec.toPhysical(normalized)>=1000?"kHz":spec.unit;
}
inline void formatControlDisplay(const ParameterSpec& spec,double normalized,DisplayContext context,const DisplayPolicy& policy,char* out,std::size_t size) noexcept {
    if(context!=DisplayContext::editing && !policy.format && !policy.formatWithContext && !std::strcmp(spec.unit,"Hz") && spec.toPhysical(normalized)>=1000){std::snprintf(out,size,"%.1f",spec.toPhysical(normalized)/1000.);return;}
    formatDisplay(spec,normalized,context,policy,out,size);
}
// UI-thread text session preserves the precise original when Return is unchanged.
class TextEditSession {
    std::string initial;double original=0;
public:
    std::string begin(const ParameterSpec& spec,const DisplayPolicy& policy,double normalized) {
        original=normalized;char text[128];formatDisplay(spec,normalized,DisplayContext::editing,policy,text,sizeof(text));
        initial=text;return initial;
    }
    bool changed(const ParameterSpec& spec,const DisplayPolicy& policy,const char* text,double& normalized) const noexcept {
        if(!text || initial==text)return false;
        return parseDisplay(spec,policy,text,normalized) && normalized!=original;
    }
};
}
