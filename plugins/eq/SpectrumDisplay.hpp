#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "common/ui/AnalyzerPreferences.hpp"

namespace just::eq {
// EQ-only display processing. This class never owns an audio/parameter service.
using AnalyzerSettings=just::AnalyzerPreferences;
class SpectrumDisplay {
public:
    static constexpr std::size_t capacity=4097;
    static constexpr double silenceDb=-180;
    struct Point {double hz=0,db=silenceDb;bool valid=false;};
private:
    std::array<std::array<double,capacity>,2> target{},envelope{};
    std::size_t count=0;unsigned fftSize=0;
    double sampleRate=0,lastTime=0;
    std::uint64_t generation=0,endSample=0;
    bool initialized=false;
    static double db(double amplitude) noexcept {
        return std::isfinite(amplitude) && amplitude>0?std::max(silenceDb,20*std::log10(amplitude)):silenceDb;
    }
    static double tangent(double a,double b) noexcept {
        return a*b<=0?0:2*a*b/(a+b); // monotone uniform-grid cubic, no overshoot
    }
    double interpolate(unsigned tap,double bin) const noexcept {
        bin=std::clamp(bin,1.,double(count-1));
        const auto i=std::min(std::size_t(bin),count-2);const double t=bin-i;
        const auto& y=envelope[tap];const double d=y[i+1]-y[i];
        const double m0=i>1?tangent(y[i]-y[i-1],d):d;
        const double m1=i+2<count?tangent(d,y[i+2]-y[i+1]):d;
        const double t2=t*t,t3=t2*t;
        return std::clamp((2*t3-3*t2+1)*y[i]+(t3-2*t2+t)*m0+(-2*t3+3*t2)*y[i+1]+(t3-t2)*m1,
                          std::min(y[i],y[i+1]),std::max(y[i],y[i+1]));
    }
public:
    void clear() noexcept {count=fftSize=0;sampleRate=lastTime=0;generation=endSample=0;initialized=false;}
    bool hasData() const noexcept {return initialized;}
    void copyEnvelope(float* pre,float* post) const noexcept {
        for(std::size_t k=0;k<count;++k){pre[k]=float(envelope[0][k]);post[k]=float(envelope[1][k]);}
    }
    double binHz() const noexcept {return fftSize?sampleRate/fftSize:0;}
    double maximumHz() const noexcept {return std::min(20000.,sampleRate*.5);}
    static double frequencyAt(double position) noexcept {return 20*std::pow(1000.,std::clamp(position,0.,1.));}
    static double tilt(double hz,double slope) noexcept {return slope*std::log2(hz/1000.);}
    static double verticalPosition(double dbValue,int rangeDb) noexcept {return std::clamp(-dbValue/rangeDb,0.,1.);}
    void advance(double seconds,const AnalyzerSettings& settings) noexcept {
        if(!initialized)return;
        const double dt=std::max(0.,seconds-lastTime);lastTime=std::max(lastTime,seconds);
        const double fall=settings.releaseDbPerSecond()*dt;
        for(unsigned tap=0;tap<2;++tap)for(std::size_t k=0;k<count;++k)
            envelope[tap][k]=std::max(target[tap][k],envelope[tap][k]-fall);
    }
    // Call only for complete real snapshots. generation includes analysis
    // configuration/resume identity. Repeated UI reads do not restart release.
    bool accept(const float* pre,const float* post,std::size_t bins,unsigned size,double rate,
                std::uint64_t nextGeneration,std::uint64_t sampleEnd,double seconds,const AnalyzerSettings& settings,
                const float* preEnvelope=nullptr,const float* postEnvelope=nullptr,double sourceSeconds=0) noexcept {
        if(!pre || !post || (size!=4096 && size!=8192) || bins!=size/2+1 || bins>capacity || !std::isfinite(rate) || rate<=0 || !std::isfinite(seconds))return false;
        const bool reset=!initialized || generation!=nextGeneration || fftSize!=size || sampleRate!=rate || sampleEnd<endSample;
        if(!reset && sampleEnd==endSample){advance(seconds,settings);return false;}
        const double fall=reset?0:settings.releaseDbPerSecond()*std::max(0.,seconds-lastTime);
        count=bins;fftSize=size;sampleRate=rate;generation=nextGeneration;endSample=sampleEnd;lastTime=seconds;
        for(unsigned tap=0;tap<2;++tap)for(std::size_t k=0;k<count;++k){
            const double value=db((tap?post:pre)[k]);target[tap][k]=value;
            const auto* measuredEnvelope=tap?postEnvelope:preEnvelope;
            envelope[tap][k]=measuredEnvelope?std::max(value,double(measuredEnvelope[k])):reset?value:std::max(envelope[tap][k]-fall,value);
        }
        initialized=true;
        if(preEnvelope && postEnvelope && sourceSeconds>0 && sourceSeconds<=seconds){lastTime=sourceSeconds;advance(seconds,settings);}
        return true;
    }
    // No fabricated audio samples: only the presentation envelope decays after
    // stale/unavailable input. Bypass never calls this or advance().
    void noFreshData(double seconds,const AnalyzerSettings& settings) noexcept {
        if(!initialized)return;
        advance(seconds,settings);
        for(auto& values:target)std::fill_n(values.begin(),count,silenceDb);
    }
    Point point(unsigned tap,double x,std::size_t columns,const AnalyzerSettings& settings) const noexcept {
        if(!initialized || tap>1 || columns<2)return {};
        const double position=std::clamp(x/double(columns-1),0.,1.);
        const double hz=frequencyAt(position),step=binHz();
        if(hz<step || hz>maximumHz())return {hz,silenceDb,false};
        double value=interpolate(tap,hz/step)+tilt(hz,settings.tiltDbPerOctave);
        // A high-frequency pixel covers several linear FFT bins. Keep the
        // strongest real bin in that pixel so a narrow peak is never discarded.
        const double left=frequencyAt((x-.5)/double(columns-1)),right=std::min(maximumHz(),frequencyAt((x+.5)/double(columns-1)));
        const auto first=std::max(std::size_t(1),std::size_t(std::ceil(left/step)));
        const auto last=std::min(count-1,std::size_t(std::floor(right/step)));
        for(std::size_t k=first;k<=last && k<count;++k)value=std::max(value,envelope[tap][k]+tilt(k*step,settings.tiltDbPerOctave));
        return {hz,value,true};
    }
};
}
