#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace just::tremolo {
// UI-only value; infinity is never submitted to the host/DSP.
inline double amountDb(double depth,double mix) noexcept {
    const double valley=1-std::clamp(depth,0.0,1.0)*std::clamp(mix,0.0,1.0);
    return valley==0?-INFINITY:20*std::log10(valley);
}
inline bool depthForAmount(double db,double mix,double& destination) noexcept {
    if(!std::isfinite(mix) || mix<=0 || mix>1 || std::isnan(db) || db>0)return false;
    if(std::isinf(db)) {if(db<0 && mix==1){destination=1;return true;}return false;}
    const double minimum=mix==1?-INFINITY:20*std::log10(1-mix);
    if(db<minimum-1e-12)return false;
    destination=std::clamp(-std::expm1(db*std::log(10.0)/20)/mix,0.0,1.0);
    return true;
}
inline void formatAmount(double depth,double mix,char* text,std::size_t capacity) noexcept {
    const double value=amountDb(depth,mix);
    if(std::isinf(value))std::snprintf(text,capacity,"-inf");
    else std::snprintf(text,capacity,"%.2f",value==0?0:value);
}
}
