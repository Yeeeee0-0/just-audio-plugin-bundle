#pragma once
#include "Filter.hpp"

namespace just::eq {
// Module-local audio-thread helper. It deliberately does not define a host/UI
// transport. The shared owner supplies that command/lifecycle contract.
// Common is the sole lease authority: it invokes release on end/expiry.
// No audition state belongs to SoundState, automation, presets or serialization.
class Audition {
    std::array<Biquad,2> filter{};
    double fs=48000,blend=0;
    std::uint32_t ramp=240;
    Target field=Target::stereo;
    Shape shape=Shape::bell;
    bool held=false;
public:
    void prepare(double rate) noexcept {fs=rate;ramp=std::max(1u,std::uint32_t(std::ceil(fs*0.005)));clear();}
    void clear() noexcept {held=false;blend=0;for(auto& f:filter)f.reset();}
    void update(double hz,double q) noexcept {
        // Audition the incoming region, never the EQ gain/dynamic output.
        // Cuts reverse the pass side; shelves use their affected side. Limit
        // resonance for the half-spectrum audition so it cannot add gain.
        Coefficients c;
        if(shape==Shape::highPass || shape==Shape::lowShelf)c=design(Shape::lowPass,hz,0,std::min(q,0.7071067811865476),fs);
        else if(shape==Shape::lowPass || shape==Shape::highShelf)c=design(Shape::highPass,hz,0,std::min(q,0.7071067811865476),fs);
        else c=bandpass(hz,std::clamp(q,0.1,20.0),fs);
        for(auto& f:filter)f.c=c;
    }
    void begin(Shape selectedShape,Target target,double hz,double q) noexcept {
        shape=selectedShape;field=target;
        for(auto& f:filter)f.reset();
        update(hz,q);
        held=true;
    }
    void release() noexcept {held=false;}
    bool active() const noexcept {return held || blend>0;}
    std::array<double,2> tick(double inputL,double inputR,double normalL,double normalR,bool mono) noexcept {
        blend=held?std::min(1.0,blend+1.0/ramp):std::max(0.0,blend-1.0/ramp);
        if(blend==0)return {normalL,normalR}; // exact normal path after release
        double mid=mono?inputL:inputL*0.5+inputR*0.5;
        double side=mono?0:inputL*0.5-inputR*0.5;
        mid=filter[0].tick(mid);side=filter[1].tick(side);
        if(field==Target::mid)side=0;
        if(field==Target::side)mid=0;
        const double left=mid+side,right=mono?left:mid-side;
        return {normalL+(left-normalL)*blend,normalR+(right-normalR)*blend};
    }
};
}
