#pragma once
#include "common/dsp/Engine.hpp"
#include <vector>

namespace just::stereo {
// Physical DSP targets are independent of both host normalization and editor view.
struct Targets {
    double inputDb=0, outputDb=0, width=60, existingSide=100, mix=100;
    double lowHz=150, highHz=20000;
    double fieldWidth=1,asymmetry=0,rotation=0;
    bool inputMS=false,invertLeft=false,invertRight=false;
    bool highEnabled=false, diffuse=false, swap=false, mono=false, bypass=false;
};
inline double clean(double x) noexcept {
    return std::isfinite(x) && std::abs(x)>=1e-24 ? x : 0.0;
}

class Allpass {
    std::vector<double> history;
    std::size_t delay=1, position=0;
    double gain=0.5;
public:
    void prepare(std::size_t capacity) { history.assign(capacity,0); }
    void configure(std::size_t samples,double g) noexcept {
        delay=std::clamp(samples,std::size_t(1),history.size());
        gain=std::clamp(g,-0.65,0.65);clear();
    }
    void clear() noexcept {std::fill(history.begin(),history.end(),0);position=0;}
    double tick(double x) noexcept {
        // Stored value is x[n-D] + g*y[n-D]; y = -g*x + that value.
        const double y=clean(history[position]-gain*x);
        history[position]=clean(x+gain*y);
        if(++position==delay)position=0;
        return y;
    }
};
class Network {
    std::array<Allpass,4> stages;
public:
    bool diffuse=false;
    void prepare(double fs) {for(auto& stage:stages)stage.prepare(std::size_t(std::ceil(fs*0.0117))+2);}
    void configure(double fs,bool next) noexcept {
        diffuse=next;
        constexpr double tight[]={1.3,4.7,2.1,6.3},wide[]={3.7,8.3,5.1,11.7};
        // Original prototype coefficients, not a reconstruction of a competitor.
        constexpr double gains[]={0.55,-0.45,-0.50,0.60};
        for(std::size_t i=0;i<4;++i)
            stages[i].configure(std::size_t(std::max(1.0,std::round(fs*(next?wide[i]:tight[i])/1000))),gains[i]);
    }
    double tick(double mid) noexcept {
        const auto a=stages[1].tick(stages[0].tick(mid));
        const auto b=stages[3].tick(stages[2].tick(mid));
        return clean(0.5*a-0.5*b);
    }
};
class Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
public:
    void clear() noexcept {x1=x2=y1=y2=0;}
    void configure(double fs,double hz,bool highpass) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double omega=2*pi*std::clamp(hz,1.0,0.45*fs)/fs;
        const double c=std::cos(omega),alpha=std::sin(omega)/std::sqrt(2.0),den=1+alpha;
        b0=(highpass?(1+c):(1-c))*0.5/den;
        b1=(highpass?-(1+c):(1-c))/den;b2=b0;
        a1=-2*c/den;a2=(1-alpha)/den;
    }
    double tick(double x) noexcept {
        const double y=clean(b0*x+b1*x1+b2*x2-a1*y1-a2*y2);
        x2=x1;x1=x;y2=y1;y1=y;return y;
    }
};
struct FilterSettings {
    double low=150,high=20000;bool highEnabled=false;
    bool operator==(const FilterSettings& b) const noexcept {return low==b.low && high==b.high && highEnabled==b.highEnabled;}
};
struct Filters {
    Biquad highpass,lowpass;FilterSettings settings;
    void configure(double fs,FilterSettings next) noexcept {
        settings=next;highpass.configure(fs,next.low,true);lowpass.configure(fs,next.high,false);
    }
    void clear() noexcept {highpass.clear();lowpass.clear();}
    double tick(double q) noexcept {
        const double hp=highpass.tick(q),lp=lowpass.tick(hp);
        return settings.highEnabled?lp:hp;
    }
};

class Core {
    PrepareSpec prepared{};Targets desired{};
    std::array<Network,2> networks;
    std::array<Filters,2> filters;
    unsigned networkActive=0,filterActive=0;
    std::uint32_t networkRemaining=0,networkLength=1,filterRemaining=0,filterLength=1;
    bool initialized=false,seekPending=false,ready=false;
    LinearSmoother input,output,width,existing,mix,swap,mono,bypass;
    LinearSmoother fieldWidth,asymmetry,rotation,inputMS,invertLeft,invertRight;
    double fieldTarget=1,asymmetryTarget=0,rotationTarget=0,inputMSTarget=0,leftTarget=1,rightTarget=1;
    double inputTarget=1,outputTarget=1,widthTarget=.6,existingTarget=1,mixTarget=1,swapTarget=1,monoTarget=1,bypassTarget=0;
    std::uint32_t ms(double time) const noexcept {return std::max(1u,std::uint32_t(std::round(prepared.sampleRate*time/1000)));}
    FilterSettings wantedFilters() const noexcept {
        return {std::min(desired.lowHz,.45*prepared.sampleRate),std::min(desired.highHz,.45*prepared.sampleRate),desired.highEnabled};
    }
    void smooth(LinearSmoother& s,double& previous,double target,double time) noexcept {
        if(target!=previous){previous=target;s.setTarget(target,ms(time));}
    }
    void initializeTargets() noexcept {
        inputTarget=std::pow(10.,desired.inputDb/20);input.reset(inputTarget);
        outputTarget=std::pow(10.,desired.outputDb/20);output.reset(outputTarget);
        widthTarget=desired.width/100;width.reset(widthTarget);
        existingTarget=desired.existingSide/100;existing.reset(existingTarget);
        mixTarget=desired.mix/100;mix.reset(mixTarget);
        swapTarget=desired.swap?-1:1;swap.reset(swapTarget);
        monoTarget=desired.mono?0:1;mono.reset(monoTarget);
        bypassTarget=desired.bypass?1:0;bypass.reset(bypassTarget);
        fieldTarget=desired.fieldWidth;fieldWidth.reset(fieldTarget);
        asymmetryTarget=desired.asymmetry;asymmetry.reset(asymmetryTarget);
        rotationTarget=desired.rotation;rotation.reset(rotationTarget);
        inputMSTarget=desired.inputMS?1:0;inputMS.reset(inputMSTarget);
        leftTarget=desired.invertLeft?-1:1;invertLeft.reset(leftTarget);
        rightTarget=desired.invertRight?-1:1;invertRight.reset(rightTarget);
        networks[0].configure(prepared.sampleRate,desired.diffuse);
        networks[1].configure(prepared.sampleRate,!desired.diffuse);
        for(auto& f:filters){f.configure(prepared.sampleRate,wantedFilters());f.clear();}
        networkActive=filterActive=0;networkRemaining=filterRemaining=0;seekPending=false;initialized=true;
    }
    void serviceTransitions() noexcept {
        if(!networkRemaining && (seekPending || networks[networkActive].diffuse!=desired.diffuse)) {
            auto& target=networks[1-networkActive];
            const bool seek=seekPending;
            // At most two banks. Dense changes replace desired; never add banks.
            if(seek || target.diffuse!=desired.diffuse)target.configure(prepared.sampleRate,desired.diffuse);
            networkLength=networkRemaining=ms(seek?10:30);seekPending=false;
        }
        const auto next=wantedFilters();
        if(!filterRemaining && !(filters[filterActive].settings==next)) {
            // Copy input/output history, generate two stable complete coefficient
            // sets, then fade outputs. Never interpolate recursive coefficients.
            filters[1-filterActive]=filters[filterActive];
            filters[1-filterActive].configure(prepared.sampleRate,next);
            filterLength=filterRemaining=ms(10);
        }
    }
    double generated(double mid) noexcept {
        serviceTransitions();
        const double old=networks[networkActive].tick(mid),next=networks[1-networkActive].tick(mid);
        double q=old;
        if(networkRemaining) {
            const double fraction=double(networkLength-networkRemaining+1)/networkLength;
            q=old+(next-old)*fraction;
            if(!--networkRemaining) {
                networkActive=1-networkActive;
                if(networks[1-networkActive].diffuse==networks[networkActive].diffuse)
                    networks[1-networkActive].configure(prepared.sampleRate,!networks[networkActive].diffuse);
            }
        }
        const auto a=filters[filterActive].tick(q),b=filters[1-filterActive].tick(q);
        double filtered=a;
        if(filterRemaining) {
            const double fraction=double(filterLength-filterRemaining+1)/filterLength;
            filtered=a+(b-a)*fraction;
            if(!--filterRemaining)filterActive=1-filterActive;
        }
        return clean(filtered);
    }
    template<class Sample> void render(AudioBlock<Sample> b,const ProcessContext& context) noexcept {
        if(!ready)return;
        if(!initialized)initializeTargets();
        if(context.seek)seekPending=true;
        for(std::uint32_t i=0;i<b.samples;++i) {
            double l=b.inputs[0] && !(b.inputSilenceFlags&1)?double(b.inputs[0][i]):0;
            double r=b.inputChannels==2 && b.inputs[1] && !(b.inputSilenceFlags&2)?double(b.inputs[1][i]):l;
            // A silent/missing right channel in a stereo layout is zero, not mono.
            if(b.inputChannels==2 && (!b.inputs[1] || (b.inputSilenceFlags&2)))r=0;
            if(!std::isfinite(l)){l=0;telemetry.invalidInput=true;}
            if(!std::isfinite(r)){r=0;telemetry.invalidInput=true;}
            telemetry.inputPeak=std::max({telemetry.inputPeak,std::abs(l),std::abs(r)});
            const double rawMid=b.inputChannels==1?l:(.5*l+.5*r);
            const double rawSide=b.inputChannels==1?0:(.5*l-.5*r);
            const auto bp=bypass.tick();
            const auto gin=input.tick()*(1-bp)+bp,gout=output.tick()*(1-bp)+bp;
            const auto w=width.tick(),k=existing.tick(),m=mix.tick(),sign=swap.tick(),audition=mono.tick();
            const double fw=fieldWidth.tick(),asym=asymmetry.tick(),rot=rotation.tick();
            const double msInput=inputMS.tick(),leftSign=invertLeft.tick(),rightSign=invertRight.tick();
            // Deliberate exact legacy path, including its operation order. The
            // appended neutral Init values cannot add matrix rounding to old audio.
            const bool neutral=fw==1 && asym==0 && rot==0 && leftSign==1 &&
                (b.inputChannels==1 || (rightSign==1 && msInput==0));
            double yl=0,yr=0;
            double fallbackMid=rawMid;
            if(neutral) {
            const double scaledMid=rawMid*gin,scaledSide=rawSide*gin;
            if(!std::isfinite(scaledMid) || !std::isfinite(scaledSide))telemetry.invalidInput=true;
            const double mid=clean(scaledMid);
            // Bypass drains both prepared networks instead of freezing their tails.
            const double q=generated(mid*(1-bp));
            const double wetSide=sign*((1-m+m*k)*clean(scaledSide)+m*w*q);
            const double side=(1-bp)*audition*wetSide+bp*rawSide;
            yl=gout*(mid+side);yr=gout*(mid-side);
            if(b.outputChannels==1)yl=gout*mid;
                fallbackMid=mid;
            } else {
                const double a=l*leftSign,bb=b.inputChannels==1?a:r*rightSign;
                double decodedMid=b.inputChannels==1?a:(.5*a+.5*bb);
                double decodedSide=b.inputChannels==1?0:(.5*a-.5*bb);
                if(b.inputChannels==2 && msInput!=0) {
                    decodedMid+=(a-decodedMid)*msInput;
                    decodedSide+=(bb-decodedSide)*msInput;
                }
                const double mid=clean(decodedMid*gin),sourceSide=clean(decodedSide*gin);
                if(!std::isfinite(decodedMid*gin) || !std::isfinite(decodedSide*gin))telemetry.invalidInput=true;
                const double q=generated(mid*(1-bp));
                const double side0=(1-m+m*k)*sourceSide+m*w*q;
                const double side1=side0;
                constexpr double radians=3.14159265358979323846/180.;
                // JUST-defined shear and orthogonal M/S rotation, not S1 emulation.
                const double mid1=mid+std::sin(asym*radians)*side1;
                const double cosine=std::cos(rot*radians),sine=std::sin(rot*radians);
                const double mid2=cosine*mid1-sine*side1;
                const double side2=(sine*mid1+cosine*side1)*fw*sign*audition;
                yl=gout*((1-bp)*(mid2+side2)+bp*l);
                yr=gout*((1-bp)*(mid2-side2)+bp*r);
                // The original mono output path has no stereo field transform.
                if(b.outputChannels==1)yl=gout*((1-bp)*mid+bp*l);
                fallbackMid=(1-bp)*mid+bp*rawMid;
            }
            if(!std::isfinite(yl) || !std::isfinite(yr)) {
                yl=yr=finiteOr(gout*fallbackMid);telemetry.invalidInput=true;
            }
            // Preserve ordinary over-0 dBFS audio. Only quarantine results that
            // cannot be represented by the host's requested sample format.
            const double limit=std::numeric_limits<Sample>::max();
            if(std::abs(yl)>limit || std::abs(yr)>limit) {
                yl=yr=0;telemetry.invalidInput=true;
            }
            if(b.outputs[0])b.outputs[0][i]=Sample(yl);
            if(b.outputChannels==2 && b.outputs[1])b.outputs[1][i]=Sample(yr);
            telemetry.outputPeak=std::max({telemetry.outputPeak,std::abs(yl),std::abs(yr)});
        }
    }
public:
    Telemetry telemetry{};
    bool prepare(const PrepareSpec& spec) {
        if(!spec.valid() || spec.sampleRate<1000 || spec.sampleRate>768000 ||
           (spec.inputChannels==2 && spec.outputChannels==1))return false;
        ready=false;prepared=spec;for(auto& n:networks)n.prepare(spec.sampleRate);
        ready=true;
        reset(ResetReason::firstActivation);return true;
    }
    void reset(ResetReason reason) noexcept {
        if(reason==ResetReason::seek && initialized){seekPending=true;return;}
        initialized=false;networkRemaining=filterRemaining=0;seekPending=false;telemetry={};
    }
    void setTargets(Targets t) noexcept {
        auto bounded=[](double x,double fallback,double lo,double hi){return std::clamp(finiteOr(x,fallback),lo,hi);};
        t.inputDb=bounded(t.inputDb,0,-24,12);t.outputDb=bounded(t.outputDb,0,-24,12);
        t.width=bounded(t.width,60,0,200);t.existingSide=bounded(t.existingSide,100,0,200);
        t.mix=bounded(t.mix,100,0,100);t.lowHz=bounded(t.lowHz,150,30,500);t.highHz=bounded(t.highHz,20000,1000,20000);
        t.fieldWidth=bounded(t.fieldWidth,1,0,3);t.asymmetry=bounded(t.asymmetry,0,-90,90);t.rotation=bounded(t.rotation,0,-45,45);
        desired=t;
        if(!ready)return;
        if(!initialized){initializeTargets();return;}
        smooth(input,inputTarget,std::pow(10.,t.inputDb/20),10);smooth(output,outputTarget,std::pow(10.,t.outputDb/20),10);
        smooth(width,widthTarget,t.width/100,10);smooth(existing,existingTarget,t.existingSide/100,10);smooth(mix,mixTarget,t.mix/100,10);
        smooth(fieldWidth,fieldTarget,t.fieldWidth,10);smooth(asymmetry,asymmetryTarget,t.asymmetry,10);smooth(rotation,rotationTarget,t.rotation,10);
        smooth(inputMS,inputMSTarget,t.inputMS?1:0,10);smooth(invertLeft,leftTarget,t.invertLeft?-1:1,5);smooth(invertRight,rightTarget,t.invertRight?-1:1,5);
        smooth(swap,swapTarget,t.swap?-1:1,5);smooth(mono,monoTarget,t.mono?0:1,5);smooth(bypass,bypassTarget,t.bypass?1:0,5);
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept {render(b,c);}
    std::uint32_t tail() const noexcept {return std::uint32_t(std::ceil(2*prepared.sampleRate));}
};
}
