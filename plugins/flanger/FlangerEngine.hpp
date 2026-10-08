#pragma once
#include "Parameters.hpp"
#include "common/dsp/Engine.hpp"
#include <limits>

namespace just::flanger {
constexpr double tau=6.283185307179586476925286766559;
inline double cycle(double v) noexcept {return std::isfinite(v)?v-std::floor(v):0;}
inline double divisionBeats(unsigned ordinal,int numerator,int denominator) noexcept {
    constexpr double noteBeats[]={0.0625,0.125,0.25,0.5,1,2};
    const unsigned base=std::min(ordinal,26u)/3,variant=ordinal%3;
    const double bar=double(numerator)*4.0/double(denominator);
    const double straight=base<6?noteBeats[base]:bar*double(1u<<(base-6));
    return straight*(variant==1?1.5:variant==2?2.0/3.0:1.0);
}
struct Range {double minimum,maximum;};
inline Range sweepRange(double base,double depth,double sampleRate,bool manual=false) noexcept {
    const double minimum=std::max(0.05,1000.0/sampleRate);
    const double centre=std::clamp(base,minimum,12.0);
    const double extent=manual?0:std::clamp(depth/100.0,0.0,1.0)*std::min(centre-minimum,12.0-centre);
    return {centre-extent,centre+extent};
}
class CircularSmoother {
    double value=0,increment=0;unsigned remaining=0;
public:
    void reset(double next) noexcept {value=cycle(next);remaining=0;increment=0;}
    void target(double next,unsigned n) noexcept {
        const double difference=std::remainder(cycle(next)-value,1.0);
        remaining=n;increment=n?difference/n:0;if(!n)value=cycle(next);
    }
    double tick() noexcept {if(remaining){value=cycle(value+increment);--remaining;}return value;}
};
struct Diagnostics {
    double phase=0,delayLeftMs=2,delayRightMs=2,effectiveRate=0.25,ringPeak=0;
    double lastBpm=120;bool syncUnavailable=false;
};
class FlangerEngine final:public Engine {
    std::array<std::vector<double>,2> ring;
    std::array<double,2> lowpass{},previousDelay{},fadeFromDelay{};
    std::array<bool,2> invalidChannel{};
    std::array<LinearSmoother,std::size(parameters)> smoothers;
    std::array<double,std::size(parameters)> lastNormalized{};
    CircularSmoother startPhase;
    SpscQueue<Telemetry,8> telemetry;Telemetry peaks{};
    PrepareSpec prepared{};Diagnostics diagnostic{};
    std::size_t write=0;double freePhase=0,lastBpm=120,lastBeatLength=4;
    int numerator=4,denominator=4,lastShape=0,lastMode=0,lastTimeMode=0,lastSync=0;
    unsigned transitionLeft=0,transitionLength=1;bool initialized=false,playingBefore=false;
    bool transitionPending=false;std::uint64_t bypassSamples=0;
    double lastFrequency=-1,lpCoefficient=0;

    unsigned samples(double ms) const noexcept {return std::max(1u,unsigned(prepared.sampleRate*ms/1000.0));}
    double read(unsigned channel,double delayMs) const noexcept {
        const double delay=std::clamp(delayMs*prepared.sampleRate/1000.0,1.0,prepared.sampleRate*0.012);
        double pos=double(write)-delay;if(pos<0)pos+=double(ring[channel].size());
        const auto a=std::size_t(pos),b=(a+1)%ring[channel].size();const double fraction=pos-double(a);
        return (1-fraction)*ring[channel][a]+fraction*ring[channel][b];
    }
    void startTransition() noexcept {
        fadeFromDelay=previousDelay;transitionLength=samples(10);transitionLeft=transitionLength;
    }
    static double waveform(double phase,int shape) noexcept {
        if(!shape)return std::sin(tau*phase);
        // Triangle at phase0 = centre, initially rising. The delay signal gets
        // a two-sample corner rounding below, separate from enum selection.
        return 2.0/std::acos(-1.0)*std::asin(std::sin(tau*phase));
    }
    std::array<double,2> roundedTriangle{};
    template<class Sample> void render(AudioBlock<Sample> b,const ProcessContext& c) noexcept {
        if(ring[0].empty())return;
        const bool bpmAvailable=c.tempoValid && std::isfinite(c.bpm) && c.bpm>0;
        const bool tempoChanged=bpmAvailable && c.bpm!=lastBpm;
        if(bpmAvailable)lastBpm=c.bpm;
        if(c.timeSignatureValid && c.numerator>0 && c.denominator>0){numerator=c.numerator;denominator=c.denominator;}
        const int shape=lastShape,mode=lastMode,timeMode=lastTimeMode;const bool sync=lastSync!=0;
        const auto division=unsigned(parameters[registry.index(divisionID)].toPhysical(lastNormalized[registry.index(divisionID)]));
        const double beats=divisionBeats(division,numerator,denominator);
        diagnostic.syncUnavailable=sync && (!bpmAvailable || !c.timeSignatureValid || (timeMode==1 && !c.ppqValid));
        if((c.seek || tempoChanged || beats!=lastBeatLength) && sync && timeMode==1)startTransition();
        if(timeMode==2 && c.playing && !playingBefore){freePhase=0;startTransition();}
        playingBefore=c.playing;lastBeatLength=beats;
        if(transitionPending){startTransition();transitionPending=false;}
        for(unsigned i=0;i<b.samples;++i) {
            std::array<double,std::size(parameters)> values{};
            for(std::size_t n=0;n<values.size();++n)values[n]=smoothers[n].tick();
            auto v=[&](ParamID id){return values[registry.index(id)];};
            const double inputGain=v(inputGainID),outputGain=v(outputGainID);
            const double base=v(baseMsID),depth=v(depthID),feedback=std::clamp(v(feedbackID),-0.95,0.95);
            const double mix=v(mixID),bypass=v(bypassID),polarity=v(wetPolarityID);
            const double frequency=std::min(v(fbLowpassHzID),0.45*prepared.sampleRate);
            if(frequency!=lastFrequency){lpCoefficient=std::exp(-tau*frequency/prepared.sampleRate);lastFrequency=frequency;}
            const double rate=sync?lastBpm/(60.0*beats):v(rateHzID);
            double lfoPhase=freePhase;
            if(sync && timeMode==1 && c.ppqValid && std::isfinite(c.ppq))
                lfoPhase=cycle(std::fmod(c.ppq,beats)/beats + cycle((double(c.blockSampleOffset)+i)*(rate/prepared.sampleRate)));
            const double offset=startPhase.tick(),stereo=v(stereoPhaseID)/360.0;
            const auto range=sweepRange(base,depth,prepared.sampleRate,mode==1);
            const double excursion=(range.maximum-range.minimum)*0.5;
            std::array<double,2> delays{};
            for(unsigned ch=0;ch<2;++ch) {
                double wave=waveform(cycle(lfoPhase+offset+(ch?stereo:0)),shape);
                if(shape){roundedTriangle[ch]+=0.5*(wave-roundedTriangle[ch]);wave=roundedTriangle[ch];}
                else roundedTriangle[ch]=wave;
                delays[ch]=std::clamp(base+excursion*wave,range.minimum,range.maximum);
            }
            const double fade=transitionLeft?1.0-double(transitionLeft)/transitionLength:1.0;
            for(unsigned ch=0;ch<b.outputChannels && ch<2;++ch) {
                double input=ch<b.inputChannels && b.inputs[ch] && !(b.inputSilenceFlags&(1ull<<ch))?double(b.inputs[ch][i]):0;
                if(!std::isfinite(input)) {
                    input=0;peaks.invalidInput=true;
                    if(!invalidChannel[ch]){std::fill(ring[ch].begin(),ring[ch].end(),0);lowpass[ch]=0;}
                    invalidChannel[ch]=true;
                } else invalidChannel[ch]=false;
                peaks.inputPeak=std::max(peaks.inputPeak,std::abs(input));
                const double dry=input*inputGain;
                const double currentWet=read(ch,delays[ch]);
                const double wet=transitionLeft?(1-fade)*read(ch,fadeFromDelay[ch])+fade*currentWet:currentWet;
                lowpass[ch]=(1-lpCoefficient)*wet+lpCoefficient*lowpass[ch];
                if(std::abs(lowpass[ch])<1e-30)lowpass[ch]=0;
                double stored=dry*(1-bypass)+feedback*lowpass[ch];
                double output=((1-mix)*dry+mix*polarity*wet)*outputGain;
                output=(1-bypass)*output+bypass*input;
                if(!std::isfinite(stored) || !std::isfinite(output) || std::abs(output)>double(std::numeric_limits<Sample>::max())) {
                    std::fill(ring[ch].begin(),ring[ch].end(),0);lowpass[ch]=0;stored=0;output=input;peaks.invalidInput=true;
                }
                if(std::abs(stored)<1e-30)stored=0;
                ring[ch][write]=stored;diagnostic.ringPeak=std::max(diagnostic.ringPeak,std::abs(stored));
                if(b.outputs[ch])b.outputs[ch][i]=Sample(output);
                peaks.outputPeak=std::max(peaks.outputPeak,std::abs(output));
            }
            if(c.analysis){EffectAnalysisSample measured;measured.validFields=analysisDelay;
                measured.delayMs[0]=delays[0];measured.delayMs[1]=delays[1];
                measured.flags=diagnostic.syncUnavailable?1u:0u;
                c.analysis->pushSample(c.blockSampleOffset+i,measured);}
            previousDelay=delays;
            if(transitionLeft)--transitionLeft;
            if(++write==ring[0].size())write=0;
            freePhase=cycle(freePhase+rate/prepared.sampleRate);
            diagnostic.phase=lfoPhase;diagnostic.delayLeftMs=delays[0];diagnostic.delayRightMs=delays[1];
            diagnostic.effectiveRate=rate;diagnostic.lastBpm=lastBpm;
            peaks.validFields|=telemetryEffectiveRate|telemetryEffectiveDelay|telemetrySync;
            peaks.effectiveRateHz=rate;peaks.effectiveDelayMs=(delays[0]+delays[1])*.5;
            peaks.flags=diagnostic.syncUnavailable?telemetryFlagSyncUnavailable:0;
            if(bypass==1) {
                if(++bypassSamples==std::uint64_t(prepared.sampleRate*5)) {
                    for(auto& channel:ring)std::fill(channel.begin(),channel.end(),0);lowpass={};
                }
            } else bypassSamples=0;
        }
    }
public:
    bool prepare(const PrepareSpec& s) override {
        if(!s.valid() || s.sampleRate<1000 || s.sampleRate>768000 || s.inputChannels!=s.outputChannels)return false;
        prepared=s;for(auto& channel:ring)channel.assign(std::size_t(std::ceil(s.sampleRate*0.012))+2,0);
        reset(ResetReason::sampleRateChange);return true;
    }
    void reset(ResetReason reason) noexcept override {
        if(reason==ResetReason::seek){transitionPending=true;return;}
        for(auto& channel:ring)std::fill(channel.begin(),channel.end(),0);
        lowpass={};roundedTriangle={};write=0;freePhase=0;lastBpm=120;numerator=denominator=4;
        peaks={};diagnostic={};initialized=false;playingBefore=false;transitionLeft=0;
        transitionPending=false;lastFrequency=-1;bypassSamples=0;invalidChannel={};
        previousDelay={2,2};fadeFromDelay=previousDelay;lastBeatLength=4;
    }
    void applyTargets(const SoundState& state,std::int32_t) noexcept override {
        for(std::size_t i=0;i<registry.count;++i) {
            const auto& spec=parameters[i];const double normalized=std::clamp(finiteOr(state.targets[i],spec.toNormalized(spec.initial)),0.0,1.0);
            if(initialized && normalized==lastNormalized[i])continue;
            const double physical=spec.toPhysical(normalized);double value=physical;
            if(spec.id==inputGainID || spec.id==outputGainID)value=std::pow(10.0,physical/20.0);
            if(spec.id==depthID)value=physical; // percentage geometry is defined by V1.
            if(spec.id==feedbackID || spec.id==mixID)value=physical/100.0;
            if(spec.id==wetPolarityID)value=physical==0?1:-1;
            if(spec.id==phaseID) {
                if(initialized)startPhase.target(physical/360.0,samples(spec.smoothingMs));else startPhase.reset(physical/360.0);
            }
            if(initialized)smoothers[i].setTarget(value,spec.id==bypassID?samples(5):spec.smoothingMs>0?samples(spec.smoothingMs):0);else smoothers[i].reset(value);
            if(initialized && (spec.id==shapeID || spec.id==modeID || spec.id==timeModeID || spec.id==syncID || spec.id==divisionID))transitionPending=true;
            lastNormalized[i]=normalized;
        }
        lastShape=int(physical(state,shapeID));lastMode=int(physical(state,modeID));
        lastTimeMode=int(physical(state,timeModeID));lastSync=int(physical(state,syncID));initialized=true;
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {telemetry.push(peaks);peaks={};}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override {return {TailKind::finite,std::uint32_t(prepared.sampleRate*5)};}
    bool readTelemetry(Telemetry& value) noexcept override {return telemetry.pop(value);}
    // Offline tests only: this is audio-thread-owned, never exposed to the UI.
    Diagnostics diagnosticsForTest() const noexcept {return diagnostic;}
};
}
