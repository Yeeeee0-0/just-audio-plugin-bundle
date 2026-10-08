#pragma once
#include "Filter.hpp"
#include "Audition.hpp"
#include "common/dsp/Engine.hpp"
namespace just::eq {
struct BandValues {
    bool on=false,dynamic=false;Shape shape=Shape::bell;Target field=Target::stereo;
    double hz=1000,gain=0,q=0.7071067811865476,range=6,threshold=-24,knee=6,attack=20,release=200;
    int slope=1,detector=1,source=0;
};
inline BandValues readBand(const SoundState& s,std::size_t b) noexcept {
    auto get=[&](Field f){return physical(s,index(b,f));};
    return {get(enabled)>0.5,get(dynamicEnabled)>0.5,Shape(int(get(type))),Target(int(get(target))),
        get(frequency),get(gain),get(q),get(range),get(threshold),get(knee),get(attack),get(release),effectiveSlope(s,b),int(get(detector)),int(get(source))};
}
class EqEngine final:public Engine {
    struct Path {
        bool on=false;Shape shape=Shape::bell;Target field=Target::stereo;int slope=1;
        std::array<FilterBank,2> banks{};
        void reset() noexcept {for(auto& b:banks)b.reset();}
        bool matches(const BandValues& v) const noexcept {return on==v.on && shape==v.shape && field==v.field && slope==v.slope;}
        void set(const BandValues& v) noexcept {on=v.on;shape=v.shape;field=v.field;slope=v.slope;reset();}
        void update(double hz,double db,double q,double fs) noexcept {for(auto& b:banks)b.update(shape,hz,db,q,slope,fs);}
        std::array<double,2> tick(double mid,double side,bool mono) noexcept {
            if(!on)return {mid,side};
            if(field!=Target::side)mid=banks[0].tick(mid);
            if(!mono && field!=Target::mid)side=banks[1].tick(side);
            return {mid,side};
        }
    };
    struct Band {
        BandValues target{};std::array<Path,2> paths{};int active=0,fade=0;
        double logHz=std::log(1000.0),db=0,logQ=std::log(0.7071067811865476),dynamicDb=0,energy=0;
        double rangeDb=6,thresholdDb=-24,kneeDb=6,logAttack=std::log(20.0),logRelease=std::log(200.0);
        std::array<Biquad,2> detectors{};
        double lastHz=-1,lastQ=-1,lastDb=1e99;
        void reset() noexcept {for(auto& p:paths)p.reset();for(auto& d:detectors)d.reset();energy=dynamicDb=0;lastHz=-1;fade=0;}
        void init() noexcept {
            reset();logHz=std::log(target.hz);logQ=std::log(target.q);db=target.gain;
            rangeDb=target.range;thresholdDb=target.threshold;kneeDb=target.knee;
            logAttack=std::log(target.attack);logRelease=std::log(target.release);
            paths[0].set(target);paths[1].set(target);active=0;
        }
        void smoothControls(double coefficient) noexcept {
            logHz+=(std::log(target.hz)-logHz)*coefficient;logQ+=(std::log(target.q)-logQ)*coefficient;db+=(target.gain-db)*coefficient;
            rangeDb+=(target.range-rangeDb)*coefficient;thresholdDb+=(target.threshold-thresholdDb)*coefficient;kneeDb+=(target.knee-kneeDb)*coefficient;
            logAttack+=(std::log(target.attack)-logAttack)*coefficient;logRelease+=(std::log(target.release)-logRelease)*coefficient;
        }
    };
    std::array<Band,bandCount> bands{};double fs=48000,smooth=0.002,wet=1,inputDb=0,outputDb=0;
    double targetWet=1,targetInput=0,targetOutput=0;int fadeLength=240;bool primed=false;std::uint64_t clock=0;
    LinearSmoother wetFade;std::uint32_t bypassLength=240;
    Audition audition;std::size_t auditionBand=bandCount;
    SpscQueue<Telemetry,8> telemetry;Telemetry current{};
    double sanitize(double x) noexcept {if(std::isfinite(x))return x;current.invalidInput=true;return 0;}
    template<class Sample> void render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
        if(context.seek)reset(ResetReason::seek);
        bool mono=block.inputChannels==1;
        for(std::uint32_t i=0;i<block.samples;++i,++clock) {
            auto read=[&](std::uint32_t ch){return ch<block.inputChannels && block.inputs[ch] && !(block.inputSilenceFlags&(1ull<<ch))?sanitize(block.inputs[ch][i]):0.0;};
            double l=read(0),r=mono?l:read(1);
            current.inputPeak=std::max({current.inputPeak,std::abs(l),std::abs(r)});
            wet=wetFade.tick();inputDb+=(targetInput-inputDb)*smooth;outputDb+=(targetOutput-outputDb)*smooth;
            if(std::abs(inputDb-targetInput)<1e-12)inputDb=targetInput;
            if(std::abs(outputDb-targetOutput)<1e-12)outputDb=targetOutput;
            double ing=std::pow(10.0,inputDb/20),outg=std::pow(10.0,outputDb/20);
            double dryMid=mono?l*ing:(l*0.5+r*0.5)*ing,drySide=mono?0:(l*0.5-r*0.5)*ing;
            double mid=dryMid,side=drySide;
            for(auto& b:bands) {
                auto& t=b.target;
                b.smoothControls(smooth);
                if(!t.on && !b.paths[b.active].on && !b.fade) {
                    b.dynamicDb=b.energy=0;continue;
                }
                if(!b.fade && !b.paths[b.active].matches(t)) {b.paths[1-b.active].set(t);b.fade=fadeLength;b.lastHz=-1;}
                double hz=effectiveFrequency(std::exp(b.logHz),fs),q=std::exp(b.logQ);
                if(clock%16==0 || b.lastHz<0) {
                    if(hz!=b.lastHz || q!=b.lastQ) {
                        auto c=bandpass(hz,q,fs);for(auto& d:b.detectors)d.c=c;
                    }
                }
                double dl=dryMid,dr=drySide;bool available=true;
                if(t.source==1) {
                    available=block.sidechainChannels>0 && block.sidechain[0];
                    auto sc=[&](int ch){return block.sidechain[ch] && !(block.sidechainSilenceFlags&(1ull<<ch))?sanitize(block.sidechain[ch][i]):0.0;};
                    double sl=available?sc(0):0,sr=block.sidechainChannels>1?sc(1):sl;
                    dl=sl*0.5+sr*0.5;dr=sl*0.5-sr*0.5;
                }
                double a=b.detectors[0].tick(dl),d=b.detectors[1].tick(dr);
                double e=t.field==Target::mid?a*a:t.field==Target::side?d*d:a*a+d*d;
                // Sum of M/S energies avoids cancellation of anti-phase L/R.
                b.energy+=(e-b.energy)*(1-std::exp(-1/(0.01*fs)));
                double level=20*std::log10(std::max(1e-12,t.detector==1?std::sqrt(std::max(0.0,b.energy)):std::sqrt(std::max(0.0,e))));
                double dyn=t.on && t.dynamic && available && int(t.shape)<=2?downwardGain(level,b.thresholdDb,b.kneeDb,b.rangeDb):0;
                double time=std::exp(dyn<b.dynamicDb?b.logAttack:b.logRelease);
                b.dynamicDb+=(dyn-b.dynamicDb)*(1-std::exp(-1/(time*0.001*fs)));
                b.dynamicDb=std::clamp(b.dynamicDb,-b.rangeDb,0.0);
                double db=b.db+b.dynamicDb;
                if(clock%16==0 || b.lastHz<0) {
                    if(hz!=b.lastHz || q!=b.lastQ || db!=b.lastDb) {
                        b.paths[b.active].update(hz,db,q,fs);
                        if(b.fade)b.paths[1-b.active].update(hz,db,q,fs);
                        b.lastHz=hz;b.lastQ=q;b.lastDb=db;
                    }
                }
                auto old=b.paths[b.active].tick(mid,side,mono);
                if(b.fade) {
                    auto next=b.paths[1-b.active].tick(mid,side,mono);
                    double mix=1-double(b.fade)/fadeLength;
                    mid=old[0]+(next[0]-old[0])*mix;side=old[1]+(next[1]-old[1])*mix;
                    if(!--b.fade)b.active=1-b.active;
                } else {mid=old[0];side=old[1];}
            }
            // Delta reconstruction keeps Init (all bands off, gains zero) bit exact.
            double wl=(l*ing+(mid-dryMid)+(side-drySide))*outg;
            double wr=(r*ing+(mid-dryMid)-(side-drySide))*outg;
            double ol=sanitize(wet==0?l:l+(wl-l)*wet),orr=sanitize(wet==0?r:r+(wr-r)*wet);
            if(audition.active() && auditionBand<bandCount && clock%16==0){const auto& b=bands[auditionBand];audition.update(effectiveFrequency(std::exp(b.logHz),fs),std::exp(b.logQ));}
            auto heard=audition.tick(l,r,ol,orr,mono);ol=heard[0];orr=heard[1];
            auto store=[&](Sample* output,double value) {
                if(!output)return;Sample sample=static_cast<Sample>(value);
                if(!std::isfinite(sample)){sample=0;current.invalidInput=true;}
                output[i]=sample;
            };
            store(block.outputs[0],ol);
            if(block.outputChannels>1)store(block.outputs[1],orr);
            current.outputPeak=std::max({current.outputPeak,std::abs(ol),std::abs(orr)});
        }
    }
public:
    bool prepare(const PrepareSpec& s) override {
        if(!s.valid() || s.inputChannels!=s.outputChannels || s.sampleRate<100)return false;
        fs=s.sampleRate;smooth=1-std::exp(-1/(0.01*fs));fadeLength=std::max(1,int(fs*0.005));
        bypassLength=std::max(1u,static_cast<std::uint32_t>(std::lround(fs*parameters[0].smoothingMs*0.001)));
        audition.prepare(fs);reset(ResetReason::sampleRateChange);return true;
    }
    void reset(ResetReason) noexcept override {audition.clear();auditionBand=bandCount;for(auto& b:bands)b.init();wetFade.reset(targetWet);wet=targetWet;inputDb=targetInput;outputDb=targetOutput;primed=false;clock=0;current={};}
    void applyTargets(const SoundState& s,std::int32_t) noexcept override {
        double nextWet=s.targets[0]>=0.5?0:1;
        if(nextWet==0)audition.release();
        if(primed && nextWet!=targetWet)wetFade.setTarget(nextWet,bypassLength);
        targetWet=nextWet;targetInput=physical(s,1);targetOutput=physical(s,2);
        for(std::size_t i=0;i<bandCount;++i){auto next=readBand(s,i);const auto& old=bands[i].target;if(i==auditionBand && (next.shape!=old.shape || next.field!=old.field || next.slope!=old.slope))audition.release();bands[i].target=next;}
        if(auditionBand<bandCount && !bands[auditionBand].target.on)audition.release();
        if(!primed){for(auto& b:bands)b.init();wetFade.reset(targetWet);wet=targetWet;inputDb=targetInput;outputDb=targetOutput;primed=true;}
    }
// Audio-thread entry points only; no UI/raw processor pointer or guessed bridge.
    bool beginAudition(std::size_t band) noexcept {
        if(band>=bandCount || targetWet==0 || !bands[band].target.on)return false;
        auditionBand=band;const auto& b=bands[band].target;audition.begin(b.shape,b.field,b.hz,b.q);return true;
    }
    // Common owns command validation, token lifetime and lease expiry.
    bool setAudition(ParamID bandAnchor,bool enabled) noexcept override {
        for(std::size_t b=0;b<bandCount;++b)if(id(b,frequency)==bandAnchor){
            if(enabled)return beginAudition(b);
            if(b!=auditionBand)return false;
            audition.release();return true;
        }return false;
    }
    void endAudition() noexcept {audition.release();}
    bool auditionActive() const noexcept {return audition.active();}
#ifdef JUST_EQ_TEST_OBSERVER
    // Test-only, caller-thread observation of the exact values used by render().
    struct SmoothingObservation {double wet,range,threshold,knee,attack,release,dynamicGain;};
    SmoothingObservation observe(std::size_t b) const noexcept {
        const auto& v=bands[b];return {wet,v.rangeDb,v.thresholdDb,v.kneeDb,std::exp(v.logAttack),std::exp(v.logRelease),v.dynamicDb};
    }
#endif
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {telemetry.push(current);current={};}
    std::uint32_t latencySamples() const noexcept override{return 0;}
    // Conservative finite IIR decay budget; unlike a pass-through, EQ can ring.
    Tail tailSamples() const noexcept override {
        for(const auto& b:bands)if(b.target.on || b.fade || b.paths[b.active].on)return {TailKind::finite,static_cast<std::uint32_t>(fs*30)};
        return {};
    }
    bool readTelemetry(Telemetry& t) noexcept override{return telemetry.pop(t);}
};
}
