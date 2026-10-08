#pragma once
#include "Parameters.hpp"
#include <limits>

namespace just::tremolo {
inline constexpr double pi=3.1415926535897932384626433832795;
inline double fraction(double x) noexcept {return x-std::floor(x);}
inline double circularDifference(double x,double y) noexcept {return fraction(x-y+0.5)-0.5;}
inline double divisionBeats(unsigned ordinal,int numerator=4,int denominator=4) noexcept {
    static constexpr double notes[]={1.0/16,1.0/8,1.0/4,1.0/2,1,2,4};
    ordinal=std::min(ordinal,29u);const unsigned base=ordinal%10;
    double value=base<7?notes[base]:std::ldexp(double(numerator)*4/denominator,int(base-7));
    return value*(ordinal<10?1:ordinal<20?1.5:2.0/3);
}
inline double edgePhase(double milliseconds,double hz,double duty,bool square) noexcept {
    const double shortest=square?std::min(duty,1-duty):1;
    return std::min(milliseconds*0.001*hz,0.45*shortest);
}
inline double raisedCosine(double t) noexcept {return 0.5-0.5*std::cos(pi*t);}
inline double wave(unsigned shape,double phase,double duty,double edge) noexcept {
    const double p=fraction(phase),half=edge*0.5;
    if(shape==0)return (1+std::cos(2*pi*p))*0.5;
    if(shape==1)return std::abs(2*p-1);
    if(shape==2) {
        if(edge>0 && (p<half || p>1-half))return raisedCosine((p<half?p+half:p-1+half)/edge);
        if(edge>0 && p>=duty-half && p<=duty+half)return 1-raisedCosine((p-duty+half)/edge);
        return p<duty?1:0;
    }
    double down=1-p;
    if(edge>0 && (p<half || p>1-half))down=half+(1-edge)*raisedCosine((p<half?p+half:p-1+half)/edge);
    return shape==3?1-down:down;
}
struct Ramp {
    double value=0,target=0,step=0;std::uint32_t remaining=0;
    void reset(double x) noexcept {value=target=x;step=0;remaining=0;}
    void set(double x,std::uint32_t duration,bool circular=false) noexcept {
        if(x==target)return;target=x;remaining=duration;
        double distance=circular?circularDifference(x,value):x-value;
        step=duration?distance/duration:0;
        if(!duration)value=x;
    }
    double tick() noexcept {if(remaining){value+=step;if(!--remaining)value=target;}return value;}
};
struct RuntimeStatus {
    double effectiveRate=4,bpm=120,phase=0;
    bool tempoUnavailable=false,positionUnavailable=false,transportNeedsSync=false;
    std::uint32_t channels=2;
};
class TremoloEngine final : public Engine {
    PrepareSpec prepared{};bool initialized=false,wasPlaying=false,wasTransport=false,haveLastGain=false;
    double accumulator=0,lastBpm=120,lastExpectedBase=0;
    int lastNumerator=4,lastDenominator=4;
    unsigned selectedShape=0,selectedDivision=3,selectedTimeMode=0;
    bool syncOn=false;
    Ramp inputDb,outputDb,depthValue,mixValue,rate,phaseValue,separation,dutyValue,edgeValue,bypassValue;
    std::array<Ramp,5> weights{};
    std::array<double,2> lastGain{{1,1}},jumpGain{{1,1}};
    std::uint32_t jumpRemaining=0;
    Telemetry current{};SpscQueue<Telemetry,8> telemetry;
    RuntimeStatus runtime{};
    std::uint32_t duration(double ms) const noexcept {return std::max(1u,static_cast<std::uint32_t>(std::ceil(prepared.sampleRate*ms*0.001)));}
    void jump() noexcept {if(haveLastGain){jumpGain=lastGain;jumpRemaining=duration(5);}}
    template<class Sample> void render(AudioBlock<Sample> block,const ProcessContext& c) noexcept {
        if(!block.samples)return;
        if(c.tempoValid && std::isfinite(c.bpm) && c.bpm>0)lastBpm=c.bpm;
        if(c.timeSignatureValid && c.numerator>0 && c.denominator>0){lastNumerator=c.numerator;lastDenominator=c.denominator;}
        const double beats=divisionBeats(selectedDivision,lastNumerator,lastDenominator);
        const bool transport=selectedTimeMode==1 && syncOn && c.ppqValid && std::isfinite(c.ppq);
        if(selectedTimeMode==2 && c.playing && !wasPlaying){accumulator=0;jump();}
        if(transport!=wasTransport || (transport && c.seek))jump();
        wasPlaying=c.playing;
        runtime.tempoUnavailable=syncOn && !(c.tempoValid && std::isfinite(c.bpm) && c.bpm>0);
        runtime.positionUnavailable=selectedTimeMode==1 && syncOn && !transport;
        runtime.transportNeedsSync=selectedTimeMode==1 && !syncOn;
        runtime.bpm=lastBpm;runtime.channels=prepared.outputChannels;
        for(std::uint32_t n=0;n<block.samples;++n) {
            const double freeRate=rate.tick();
            double hz=syncOn?lastBpm/(60*beats):freeRate;
            if(!std::isfinite(hz) || hz<=0)hz=4;
            hz=std::min(hz,prepared.sampleRate*0.45);
            const double step=hz/prepared.sampleRate;
            double base=accumulator;
            if(transport) {
                const double offset=c.playing?double(c.blockSampleOffset)+n:0;
                // Reduce before division so a finite but extreme host PPQ cannot overflow.
                base=fraction(std::fmod(c.ppq,beats)/beats+offset*step);
                if(n==0 && wasTransport && c.playing && std::abs(circularDifference(base,lastExpectedBase))>1e-7)jump();
            }
            const double phase=phaseValue.tick(),stereo=separation.tick();
            const double depth=depthValue.tick(),mix=mixValue.tick();
            const double duty=dutyValue.tick(),edge=edgeValue.tick();
            const double gain=std::pow(10.0,(inputDb.tick()+outputDb.tick())/20);
            const double bypass=bypassValue.tick();
            std::array<double,5> w{};for(unsigned s=0;s<5;++s)w[s]=weights[s].tick();
            const double blend=jumpRemaining?1-double(jumpRemaining)/duration(5):1;
            EffectAnalysisSample measured;measured.validFields=analysisModulation;measured.effectiveHz=hz;
            measured.flags=(runtime.tempoUnavailable?1u:0u)|(runtime.positionUnavailable?2u:0u)|(runtime.transportNeedsSync?4u:0u);
            for(std::uint32_t channel=0;channel<block.outputChannels && channel<2;++channel) {
                double envelope=0;
                const double p=base+phase+(channel==1?stereo:0);
                for(unsigned s=0;s<5;++s)if(w[s]!=0)envelope+=w[s]*wave(s,p,duty,edgePhase(edge,hz,duty,s==2));
                double shaped=gain*(1-mix*depth*(1-envelope));
                if(jumpRemaining)shaped=jumpGain[channel]+(shaped-jumpGain[channel])*blend;
                lastGain[channel]=shaped;
                const double finalGain=bypass==1?1:shaped+(1-shaped)*bypass;
                measured.modulation[channel]=finalGain;measured.phase[channel]=fraction(p);
                Sample input=channel<block.inputChannels && block.inputs[channel] && !(block.inputSilenceFlags&(1ull<<channel))?block.inputs[channel][n]:Sample(0);
                if(!std::isfinite(input)){input=0;current.invalidInput=true;}
                if(std::abs(input)<std::numeric_limits<Sample>::min())input=0;
                current.inputPeak=std::max(current.inputPeak,std::abs(double(input)));
                if(block.outputs[channel]) {
                    const double output=double(input)*finalGain;
                    block.outputs[channel][n]=std::isfinite(output) && std::abs(output)<=std::numeric_limits<Sample>::max() && std::abs(output)>=std::numeric_limits<Sample>::min()?Sample(output):Sample(0);
                    current.outputPeak=std::max(current.outputPeak,std::abs(double(block.outputs[channel][n])));
                }
            }
            if(c.analysis)c.analysis->pushSample(c.blockSampleOffset+n,measured);
            if(jumpRemaining)--jumpRemaining;
            haveLastGain=true;
            accumulator=fraction(base+step);lastExpectedBase=accumulator;
            runtime.effectiveRate=hz;runtime.phase=fraction(base+phase);
            wasTransport=transport;
        }
    }
public:
    bool prepare(const PrepareSpec& s) override {
        if(!s.valid() || s.sampleRate>768000 || s.inputChannels!=s.outputChannels || s.sidechainChannels)return false;
        prepared=s;runtime.channels=s.outputChannels;return true;
    }
    void reset(ResetReason) noexcept override {
        initialized=false;accumulator=0;lastBpm=120;lastNumerator=lastDenominator=4;
        wasPlaying=wasTransport=haveLastGain=false;jumpRemaining=0;current={};runtime={};runtime.channels=prepared.outputChannels;
    }
    void applyTargets(const SoundState& state,std::int32_t) noexcept override {
        auto valid=[&](ParamID id){const double v=state.targets[registry.index(id)];return std::isfinite(v) && v>=0 && v<=1;};
        auto set=[&](Ramp& ramp,ParamID id,double divisor=1,bool circular=false) {
            if(!valid(id))return;
            const double v=physical(state,id)/divisor;
            if(!initialized)ramp.reset(v);else ramp.set(v,duration(spec(id).smoothingMs),circular);
        };
        set(inputDb,inputGain);set(outputDb,outputGain);set(depthValue,depth,100);set(mixValue,mix,100);
        set(rate,rateHz);set(phaseValue,phase,360,true);set(separation,stereoPhase,360,true);
        set(dutyValue,duty,100);set(edgeValue,edgeMs);set(bypassValue,bypass);
        if(valid(sync))syncOn=physical(state,sync)>=0.5;
        if(valid(division))selectedDivision=unsigned(physical(state,division));
        if(valid(timeMode))selectedTimeMode=unsigned(physical(state,timeMode));
        if(valid(shape)) {
            const unsigned next=unsigned(physical(state,shape));
            if(!initialized)for(unsigned s=0;s<5;++s)weights[s].reset(s==next?1:0);
            else if(next!=selectedShape)for(unsigned s=0;s<5;++s)weights[s].set(s==next?1:0,duration(10));
            selectedShape=next;
        }
        initialized=true;
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {telemetry.push(current);current={};}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override {return {};}
    bool readTelemetry(Telemetry& t) noexcept override {return telemetry.pop(t);}
    RuntimeStatus runtimeStatus() const noexcept {return runtime;} // Test-only; not exposed to GUI.
};
}
