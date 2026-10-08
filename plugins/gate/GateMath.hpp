#pragma once
#include "common/dsp/Realtime.hpp"
namespace just::gate {
inline double decibels(double amplitude) noexcept {return 20*std::log10(std::max(std::abs(amplitude),1e-15));}
inline double linear(double db) noexcept {return std::pow(10.0,db/20.0);}
inline double expansion(double level,double threshold,double ratio,double knee,double range) noexcept {
    const double x=level-threshold,slope=ratio-1;
    double gain=0;
    if(knee<=0 || x<=-knee/2)gain=slope*x;
    else if(x<knee/2)gain=-slope*(x-knee/2)*(x-knee/2)/(2*knee);
    return std::clamp(gain,-range,0.0);
}
// 63.2% dB-domain time constant followed by a finite 0.5 ms finish inside
// 0.001 dB. A changing target replans continuously from the current value.
class GainEnvelope {
    double value_=0,target_=0,increment_=0;unsigned finish_=0;
public:
    void reset(double v) noexcept {value_=target_=v;finish_=0;increment_=0;}
    double tick(double target,double coefficient,unsigned finishSamples) noexcept {
        if(target!=target_){target_=target;finish_=0;}
        if(value_==target_)return value_;
        if(finish_){value_+=increment_;if(!--finish_)value_=target_;}
        else if(std::abs(value_-target_)<=0.001) {
            finish_=std::max(1u,finishSamples);increment_=(target_-value_)/finish_;
            value_+=increment_;if(!--finish_)value_=target_;
        } else value_=target_+(value_-target_)*coefficient;
        return value_;
    }
    double value() const noexcept {return value_;}
};
enum class Phase {Closed,Opening,Open,Holding,Closing};
class Trigger {
    Phase phase_=Phase::Closed;std::uint64_t remaining_=0;
public:
    void reset() noexcept {phase_=Phase::Closed;remaining_=0;}
    bool tick(double level,double open,double close,std::uint64_t holdSamples,bool reachedActive,bool reachedIdle) noexcept {
        switch(phase_) {
        case Phase::Closed:if(level>open)phase_=Phase::Opening;break;
        case Phase::Opening:case Phase::Open:
            if(level<close){remaining_=holdSamples;phase_=remaining_?Phase::Holding:Phase::Closing;}
            else if(reachedActive)phase_=Phase::Open;
            break;
        case Phase::Holding:
            if(level>=close){remaining_=0;phase_=reachedActive?Phase::Open:Phase::Opening;}
            else if(remaining_ && --remaining_==0)phase_=Phase::Closing;
            break;
        case Phase::Closing:
            if(level>open)phase_=Phase::Opening;
            else if(reachedIdle)phase_=Phase::Closed;
            break;
        }
        return phase_==Phase::Opening || phase_==Phase::Open || phase_==Phase::Holding;
    }
    Phase phase() const noexcept {return phase_;}
    std::uint64_t remaining() const noexcept {return remaining_;}
};
// One-pole TPT detector-only filters, 6 dB/oct; main audio stays unfiltered.
class DetectorFilter {
    double hpState=0,lpState=0;
public:
    void reset() noexcept {hpState=lpState=0;}
    double tick(double x,double hc,double lc,double hpMix,double lpMix) noexcept {
        const double v=(x-hpState)*hc,low=v+hpState;hpState=low+v;x-=hpMix*low;
        const double w=(x-lpState)*lc,lp=w+lpState;lpState=lp+w;x+=lpMix*(lp-x);
        if(std::abs(hpState)<1e-30)hpState=0;if(std::abs(lpState)<1e-30)lpState=0;
        return x;
    }
};
}
