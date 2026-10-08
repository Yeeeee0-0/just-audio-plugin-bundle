#pragma once
#include "common/runtime/Analysis.hpp"

namespace just::stereo {
// UI-only measurements of the exact final-output sample window. No synthesis,
// DSP pointers, audio-thread allocation, parameter-derived dots, or wallclock motion.
struct FieldPoint {double side=0,mid=0;};
struct FieldMeasurement {
    std::array<FieldPoint,analysisSampleCapacity> points{};
    std::size_t count=0;
    double scale=1,peakLR[2]{},peakMS[2]{},correlation=0;
    bool correlationValid=false,valid=false;
    AnalysisHeader header{};
    void clear() noexcept {*this={};}
    bool accept(const SampleFrame& frame) noexcept {
        clear();
        if(!validAnalysisHeader(frame.header) || frame.count==0 || frame.count>analysisSampleCapacity ||
           frame.header.endSample-frame.header.startSample!=frame.count)return false;
        double ll=0,rr=0,lr=0;
        for(unsigned i=0;i<frame.count;++i) {
            const double l=frame.samples[2][i],r=frame.header.outputChannels==1?l:frame.samples[3][i];
            if(!std::isfinite(l) || !std::isfinite(r)){clear();return false;}
            const double m=.5*l+.5*r,s=.5*l-.5*r;
            peakLR[0]=std::max(peakLR[0],std::abs(l));peakLR[1]=std::max(peakLR[1],std::abs(r));
            peakMS[0]=std::max(peakMS[0],std::abs(m));peakMS[1]=std::max(peakMS[1],std::abs(s));
            // Fold antipodal vectors, never the side axis alone: +/- samples
            // describe one direction. L-only remains left, R-only remains right.
            const double polarity=m<0?-1:1;
            points[i]={-s*polarity,std::abs(m)};
            scale=std::max(scale,std::hypot(m,s));ll+=l*l;rr+=r*r;lr+=l*r;
        }
        correlationValid=frame.header.outputChannels==2 && ll/frame.count>1e-9 && rr/frame.count>1e-9;
        if(correlationValid)correlation=std::clamp(lr/std::sqrt(ll*rr),-1.,1.);
        count=frame.count;header=frame.header;valid=true;return true;
    }
    double peak(unsigned channel,bool ms) const noexcept{return (ms?peakMS:peakLR)[channel>0?1:0];}
};
struct FieldGeometry {
    double centerX=0,baseline=0,radius=0;
    static FieldGeometry fit(double width,double height) noexcept {
        return {width*.5,height-38,std::max(0.,std::min((width-72)*.5,height-82))};
    }
    FieldPoint pixel(FieldPoint p,double scale) const noexcept {
        return {centerX+p.side*radius/scale,baseline-p.mid*radius/scale};
    }
};
}
