#pragma once
#include "common/dsp/Engine.hpp"
#include <limits>

// Physical-domain DSP. Engine.cpp maps the reviewed host registry into Settings;
// this core never owns controller state or host parameter identities.
namespace just::compressor {
enum class Style { clean, punch };
enum class Detector { peak, rms, blend };
struct Settings {
    double thresholdDb=0, ratio=2, attackMs=10, releaseMs=100;
    Style style=Style::clean;
    Detector detector=Detector::rms;
    double rmsBlend=0.5, kneeDb=6, rangeDb=24;
    double inputDb=0, makeupDb=0, mix=1, stereoLink=1;
    bool externalSidechain=false;
    double sidechainDb=0;
    bool highPass=false, lowPass=false;
    double highPassHz=80, lowPassHz=12000;
    bool bypass=false;
};
inline double bounded(double v,double lo,double hi,double init) noexcept {
    return std::clamp(std::isfinite(v)?v:init,lo,hi);
}
inline Settings sanitized(Settings p) noexcept {
    p.thresholdDb=bounded(p.thresholdDb,-60,0,0);
    p.ratio=bounded(p.ratio,1,20,2);
    p.attackMs=bounded(p.attackMs,0.05,200,10);
    p.releaseMs=bounded(p.releaseMs,10,2000,100);
    if(p.style!=Style::clean && p.style!=Style::punch)p.style=Style::clean;
    if(p.detector!=Detector::peak && p.detector!=Detector::rms && p.detector!=Detector::blend)p.detector=Detector::rms;
    p.rmsBlend=bounded(p.rmsBlend,0,1,0.5);
    p.kneeDb=bounded(p.kneeDb,0,24,6);
    p.rangeDb=bounded(p.rangeDb,0,36,24);
    p.inputDb=bounded(p.inputDb,-24,24,0);
    p.makeupDb=bounded(p.makeupDb,-12,24,0);
    p.mix=bounded(p.mix,0,1,1);
    p.stereoLink=bounded(p.stereoLink,0,1,1);
    p.sidechainDb=bounded(p.sidechainDb,-24,24,0);
    p.highPassHz=bounded(p.highPassHz,20,2000,80);
    p.lowPassHz=bounded(p.lowPassHz,1000,20000,12000);
    return p;
}
inline double dbToGain(double db) noexcept {return std::pow(10.0,db/20.0);}
inline double gainToDb(double gain) noexcept {return 20*std::log10(std::max(gain,1e-15));}
inline double timeCoefficient(double milliseconds,double sampleRate) noexcept {
    return std::exp(-1.0/(0.001*milliseconds*sampleRate));
}
// Negative gain in dB; the quadratic knee is continuous with a continuous first
// derivative at both joins. Range bounds gain reduction, never absolute output.
inline double reductionForLevel(double levelDb,double threshold,double ratio,double knee,double range) noexcept {
    const double over=levelDb-threshold, slope=1.0/ratio-1.0;
    double gain=0;
    if(knee<=0)gain=over>0?slope*over:0;
    else if(over>=knee*0.5)gain=slope*over;
    else if(over>-knee*0.5)gain=slope*(over+knee*0.5)*(over+knee*0.5)/(2*knee);
    return std::clamp(gain,-range,0.0);
}
class GainEnvelope {
    double db=0;
public:
    void reset(double value=0) noexcept {db=value;}
    double tick(double target,double attack,double release,bool linearAttack,double range) noexcept {
        if(target<db && linearAttack) {
            const double gain=attack*dbToGain(db)+(1-attack)*dbToGain(target);
            db=gainToDb(gain);
        } else {
            const double c=target<db?attack:release;
            db=target+(db-target)*c;
        }
        db=std::clamp(db,-range,0.0);
        if(std::abs(db)<1e-15)db=0;
        return db;
    }
};
// One-pole target smoothing with stable endpoints. Repeating the same target at
// each sample never restarts a ramp. Time/ratio targets are smoothed in log space.
class Smoothed {
    double value=0,target=0,coefficient=0;
public:
    void prepare(double rate,double ms,double initial) noexcept {
        coefficient=timeCoefficient(ms,rate);value=target=initial;
    }
    void set(double next) noexcept {target=next;}
    double tick() noexcept {
        value=target+(value-target)*coefficient;
        if(std::abs(value-target)<1e-12)value=target;
        return value;
    }
    void snap(double next) noexcept {value=target=next;}
};
struct MeterFrame {
    double inputPeak=0,outputPeak=0,detectorPeak=0,gainReductionDb=0;
    bool sidechainMissing=false,sidechainSilent=false,invalidInput=false,numericSaturation=false;
    bool bypass=false;
    std::uint32_t latencySamples=0;
};
class Core {
    double rate=48000,rmsCoefficient=0;
    Settings targets{};
    enum Control {threshold,ratioLog,attackLog,releaseLog,knee,range,input,makeup,mix,link,
                  source,scGain,hpEnabled,lpEnabled,hpHzLog,lpHzLog,rmsWeight,styleWeight,bypassWeight,count};
    std::array<Smoothed,count> controls{};
    struct Channel {double hpLow=0,lpLow=0,rmsPower=0;GainEnvelope clean,punch;};
    std::array<Channel,2> channels{};
    SpscQueue<MeterFrame,8> meters;
    MeterFrame meter{};
    bool prepared=false,sawExternalSample=false,externalConnected=false;
    static double rmsWeightFor(const Settings& s) noexcept {
        return s.detector==Detector::peak?0:s.detector==Detector::rms?1:s.rmsBlend;
    }
    std::array<double,count> values(const Settings& s) const noexcept {
        return {s.thresholdDb,std::log(s.ratio),std::log(s.attackMs),std::log(s.releaseMs),s.kneeDb,s.rangeDb,
                s.inputDb,s.makeupDb,s.mix,s.stereoLink,double(s.externalSidechain),s.sidechainDb,
                double(s.highPass),double(s.lowPass),std::log(s.highPassHz),std::log(s.lowPassHz),
                rmsWeightFor(s),s.style==Style::punch?1.0:0.0,double(s.bypass)};
    }
    double finiteSample(double v,std::uint32_t ch) noexcept {
        if(std::isfinite(v))return v;
        channels[ch]={};meter.invalidInput=true;return 0;
    }
    double multiply(double v,double gain) noexcept {
        const double limit=std::numeric_limits<double>::max();
        if(gain>1 && std::abs(v)>limit/gain) {
            meter.numericSaturation=true;return std::copysign(limit,v);
        }
        return v*gain;
    }
    double blendSamples(double a,double b,double amount) noexcept {
        const long double value=(1.0L-amount)*a+static_cast<long double>(amount)*b;
        const long double limit=std::numeric_limits<double>::max();
        if(value>limit || value<-limit)meter.numericSaturation=true;
        return static_cast<double>(std::clamp(value,-limit,limit));
    }
    template<class Sample> void render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
        if(!prepared)return;
        const auto active=std::min<std::uint32_t>(2,std::min(block.inputChannels,block.outputChannels));
        externalConnected=block.sidechainChannels>0 && block.sidechainChannels<=2 && block.sidechain[0] &&
            (block.sidechainChannels==1 || block.sidechain[1]);
        for(std::uint32_t i=0;i<block.samples;++i) {
            std::array<double,count> v{};
            for(std::size_t p=0;p<count;++p)v[p]=controls[p].tick();
            const double inputGain=dbToGain(v[input]),makeupGain=dbToGain(v[makeup]);
            const double sidechainGain=dbToGain(v[scGain]);
            const double attack=timeCoefficient(std::exp(v[attackLog]),rate);
            const double release=timeCoefficient(std::exp(v[releaseLog]),rate);
            const double hpCoefficient=std::exp(-2*3.14159265358979323846*std::min(std::exp(v[hpHzLog]),0.45*rate)/rate);
            const double lpCoefficient=std::exp(-2*3.14159265358979323846*std::min(std::exp(v[lpHzLog]),0.45*rate)/rate);
            std::array<double,2> raw{},trimmed{},desired{};bool sampleSCActive=false;
            for(std::uint32_t ch=0;ch<active;++ch) {
                raw[ch]=block.inputs[ch] && !(block.inputSilenceFlags&(1ull<<ch))?finiteSample(block.inputs[ch][i],ch):0;
                trimmed[ch]=multiply(raw[ch],inputGain);
                meter.inputPeak=std::max(meter.inputPeak,std::abs(raw[ch]));
                double external=0;
                if(externalConnected) {
                    const auto sc=block.sidechainChannels==1?0:ch;
                    if(!(block.sidechainSilenceFlags&(1ull<<sc)))external=finiteSample(block.sidechain[sc][i],ch);
                    sawExternalSample|=external!=0;sampleSCActive|=external!=0;
                }
                double detected=blendSamples(trimmed[ch],external,v[source]);
                detected=multiply(detected,sidechainGain);
                // Only the detector is bounded for overflow-safe energy math.
                // This is far above Range saturation; ordinary audio is untouched.
                detected=std::clamp(detected,-1e100,1e100);
                auto& state=channels[ch];
                state.hpLow=hpCoefficient*state.hpLow+(1-hpCoefficient)*detected;
                detected=blendSamples(detected,detected-state.hpLow,v[hpEnabled]);
                state.lpLow=lpCoefficient*state.lpLow+(1-lpCoefficient)*detected;
                detected=blendSamples(detected,state.lpLow,v[lpEnabled]);
                if(std::abs(state.hpLow)<1e-30)state.hpLow=0;
                if(std::abs(state.lpLow)<1e-30)state.lpLow=0;
                state.rmsPower=rmsCoefficient*state.rmsPower+(1-rmsCoefficient)*detected*detected;
                if(state.rmsPower<1e-30)state.rmsPower=0;
                const double amplitude=std::sqrt((1-v[rmsWeight])*detected*detected+v[rmsWeight]*state.rmsPower);
                meter.detectorPeak=std::max(meter.detectorPeak,amplitude);
                desired[ch]=reductionForLevel(gainToDb(amplitude),v[threshold],std::exp(v[ratioLog]),v[knee],v[range]);
                // A missing external bus never falls back to the internal input.
                // Leave the existing envelopes to release toward unity.
                if(!externalConnected && v[source]==1)desired[ch]=0;
            }
            if(active==2) {
                const double deepest=std::min(desired[0],desired[1]);
                for(auto& gain:desired)gain+=v[link]*(deepest-gain);
            }
            double measuredReduction=0;
            for(std::uint32_t ch=0;ch<std::min<std::uint32_t>(2,block.outputChannels);++ch) {
                if(ch>=active) {if(block.outputs[ch])block.outputs[ch][i]=0;continue;}
                auto& state=channels[ch];
                const double cleanGain=dbToGain(state.clean.tick(desired[ch],attack,release,false,v[range]));
                const double punchGain=dbToGain(state.punch.tick(desired[ch],attack,release,true,v[range]));
                const double gain=cleanGain+(punchGain-cleanGain)*v[styleWeight];
                meter.gainReductionDb=std::min(meter.gainReductionDb,gainToDb(gain));
                measuredReduction=std::max(measuredReduction,-gainToDb(gain));
                const double wet=multiply(multiply(trimmed[ch],gain),makeupGain);
                const double mixed=blendSamples(trimmed[ch],wet,v[mix]);
                const double output=blendSamples(mixed,raw[ch],v[bypassWeight]);
                const double limit=std::numeric_limits<Sample>::max();
                if(std::abs(output)>limit)meter.numericSaturation=true;
                const Sample result=static_cast<Sample>(std::clamp(output,-limit,limit));
                if(block.outputs[ch])block.outputs[ch][i]=result;
                meter.outputPeak=std::max(meter.outputPeak,std::abs(double(result)));
            }
            if(context.analysis) {
                EffectAnalysisSample sample;sample.validFields=analysisReduction|analysisSidechain;
                sample.reductionDb=measuredReduction; // stage GR: excludes makeup, mix and output trim
                sample.flags=(targets.externalSidechain?1u:0u)|(externalConnected?2u:0u)|(sampleSCActive?4u:0u);
                context.analysis->pushSample(context.blockSampleOffset+i,sample);
            }
        }
        meter.bypass=targets.bypass;
        meter.sidechainMissing=targets.externalSidechain && !externalConnected;
        meter.sidechainSilent=targets.externalSidechain && externalConnected && !sawExternalSample;
    }
public:
    // This development stage has zero configured lookahead/PDC.
    // Prepared maximum-lookahead changes need the shared owner's restart bridge.
    bool prepare(const PrepareSpec& spec,Settings initial={}) noexcept {
        if(!spec.valid() || spec.sampleRate>768000 || spec.inputChannels!=spec.outputChannels)return false;
        rate=spec.sampleRate;targets=sanitized(initial);
        rmsCoefficient=timeCoefficient(10,rate);
        const auto initialValues=values(targets);
        for(std::size_t i=0;i<count;++i)controls[i].prepare(rate,i>=source?5:10,initialValues[i]);
        channels={};meter={};sawExternalSample=false;externalConnected=false;prepared=true;return true;
    }
    void setTargets(Settings p,bool initialSnapshot=false) noexcept {
        targets=sanitized(p);const auto next=values(targets);
        for(std::size_t i=0;i<count;++i) {
            if(initialSnapshot)controls[i].snap(next[i]);else controls[i].set(next[i]);
        }
    }
    void reset() noexcept {channels={};meter={};sawExternalSample=false;externalConnected=false;}
    void process(AudioBlock<float> block,const ProcessContext& context={}) noexcept {render(block,context);}
    void process(AudioBlock<double> block,const ProcessContext& context={}) noexcept {render(block,context);}
    // Audio producer, one non-real-time consumer. Full queue drops one display
    // update; it cannot block processing. This is not a processor/controller bridge.
    void endBlock() noexcept {meters.push(meter);meter={};sawExternalSample=false;}
    bool readMeter(MeterFrame& frame) noexcept {return meters.pop(frame);}
    std::uint32_t latencySamples() const noexcept {return 0;}
    Tail tailSamples() const noexcept {return {};}
};
}
