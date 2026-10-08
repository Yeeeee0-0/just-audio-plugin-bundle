#pragma once
#include "Parameters.hpp"
#include "common/dsp/Engine.hpp"
#include <limits>

namespace just::delay {
inline constexpr double pi=3.14159265358979323846;
inline constexpr std::uint32_t driveLatency=15;
inline double dbGain(double db) noexcept {return std::abs(db)<1e-12?1.0:std::pow(10.0,db/20.0);}
inline double levelGain(double db) noexcept {return db<=-120?0:dbGain(db);}
inline double scrub(double x) noexcept {return std::abs(x)<1e-30?0:x;}

struct Diagnostics {
    double effectiveTimeL=250,effectiveTimeR=250,duckReduction=0;
    std::uint64_t protectionCount=0;
    bool syncUnavailable=false,syncClamped=false,invalidInput=false;
};

// Two storage lines, bounded reads, and O(1) reset: stale samples cannot be read.
class Ring {
    std::vector<double> data;
    std::size_t cursor=0,history=0;
public:
    void prepare(std::size_t n){data.assign(n,0);reset();}
    void reset() noexcept {cursor=history=0;}
    double read(double distance) const noexcept {
        distance=std::clamp(distance,1.0,double(data.size()-2));
        auto n=std::size_t(distance);double fraction=distance-double(n);
        auto past=[&](std::size_t d){return d<=history?data[(cursor+data.size()-d)%data.size()]:0.0;};
        // Convex interpolation cannot increase the feedback L-infinity norm.
        return (1-fraction)*past(n)+fraction*past(n+1);
    }
    void write(double v) noexcept {data[cursor]=v;cursor=(cursor+1)%data.size();history=std::min(history+1,data.size()-1);}
};

class ReadHead {
    double current=1,target=1,old=1,pending=1;
    std::uint32_t step=0,length=0;
    bool smooth=true;
public:
    void reset(double v) noexcept {current=target=old=pending=v;step=length=0;smooth=true;}
    double effective() const noexcept {return current;}
    double maximumDistance() const noexcept {return std::max({current,target,old,pending});}
    void set(double v,bool useSmooth,std::uint32_t samples) noexcept {
        samples=std::max(1u,samples);
        if(useSmooth!=smooth){current=length?old+(target-old)*double(step)/length:current;step=length=0;smooth=useSmooth;target=current;}
        if(smooth) {
            pending=v;
            if(!length && std::abs(v-current)>1e-7){old=current;target=v;length=samples;step=0;}
        } else {pending=target=v;length=samples;}
    }
    double read(const Ring& ring,double modulation=0) noexcept {
        if(!smooth){current+=(target-current)/std::max(1u,length);if(std::abs(target-current)<1e-9)current=target;return ring.read(current+modulation);}
        if(!length)return ring.read(current+modulation);
        double t=double(step+1)/length;
        double v=(1-t)*ring.read(old+modulation)+t*ring.read(target+modulation);
        if(++step>=length){current=target;step=length=0;}
        return v;
    }
};

struct OnePole {
    double lastInput=0,lastOutput=0;
    void reset() noexcept {lastInput=lastOutput=0;}
    double process(double x,double k,bool hp) noexcept {
        double a=(1-k)/(1+k),b=(hp?1:k)/(1+k);
        double y=a*lastOutput+b*(hp?x-lastInput:x+lastInput);
        lastInput=x;lastOutput=scrub(y);return lastOutput;
    }
};
struct LoopFilter {
    std::array<OnePole,2> hp{},lp{};
    void reset() noexcept {for(auto& f:hp)f.reset();for(auto& f:lp)f.reset();}
    double process(double x,double h,double l) noexcept {
        for(auto& f:hp)x=f.process(x,h,true);
        for(auto& f:lp)x=f.process(x,l,false);
        return x;
    }
};

// 31-tap symmetric windowed-sinc FIR at 2Fs, two stages => 15 base-rate samples.
struct Fir {
    std::array<double,31> state{};std::size_t cursor=0;
    double tick(double x,const std::array<double,31>& coefficients) noexcept {
        state[cursor]=x;double y=0;
        for(std::size_t k=0;k<31;++k)y+=coefficients[k]*state[(cursor+31-k)%31];
        cursor=(cursor+1)%31;return y;
    }
    void reset() noexcept {state.fill(0);cursor=0;}
};
struct Pad {
    std::array<double,driveLatency> state{};std::size_t cursor=0;
    double tick(double x) noexcept {double y=state[cursor];state[cursor]=x;cursor=(cursor+1)%state.size();return y;}
    void reset() noexcept {state.fill(0);cursor=0;}
};
struct WetColor {
    Fir up,down;Pad clean;
    void reset() noexcept {up.reset();down.reset();clean.reset();}
    double tick(double x,double driveDb,const std::array<double,31>& coefficients) noexcept {
        double g=dbGain(driveDb),compensation=1/std::sqrt(g);
        double a=up.tick(x,coefficients)*2,b=up.tick(0,coefficients)*2;
        double y=down.tick(std::tanh(g*a)*compensation,coefficients);
        down.tick(std::tanh(g*b)*compensation,coefficients);
        double bypassed=clean.tick(x),blend=std::clamp(driveDb/0.5,0.0,1.0);
        return (1-blend)*bypassed+blend*y;
    }
};

class DelayEngine final : public Engine {
    PrepareSpec prepared{};
    std::array<Ring,2> rings;
    std::array<ReadHead,4> heads;
    std::array<LoopFilter,2> filters;
    std::array<WetColor,2> colors;
    std::array<Pad,2> dryPads,trimPads;
    std::array<OnePole,2> tone;
    std::array<double,31> firCoefficients{};
    std::array<double,std::size(parameters)> targets{},current{},increments{};
    std::array<std::uint32_t,std::size(parameters)> remaining{};
    std::array<double,2> freezeDistances{};
    double phase=0,duckEnvelope=0,freezeAmount=0,freezeIncrement=0;
    double pingAmount=0,pingIncrement=0,crossAmount=0,crossIncrement=0;
    double startAmount=0,startIncrement=0,bypassAmount=0,bypassIncrement=0;
    std::uint32_t freezeRemaining=0,pingRemaining=0,crossRemaining=0,startRemaining=0,bypassRemaining=0;
    bool preparedOk=false,initialTargets=true,headsInitialized=false,wasFrozen=false;
    Telemetry telemetry{};SpscQueue<Telemetry,8> telemetryQueue;
    Diagnostics diagnostics{};AtomicSnapshot<Diagnostics> diagnosticSnapshot;
    std::atomic<std::uint32_t> tailCount{0};std::atomic<bool> infiniteTail{false};

    double p(ParamID id) const noexcept {return current[registry.index(id)];}
    double t(ParamID id) const noexcept {return targets[registry.index(id)];}
    std::uint32_t msSamples(double ms) const noexcept {return std::max(1u,std::uint32_t(prepared.sampleRate*ms/1000));}
    static void transition(double target,double& v,double& increment,std::uint32_t& left,std::uint32_t n,bool initial) noexcept {
        if(initial){v=target;increment=0;left=0;return;}
        double previousTarget=v+increment*left;
        if(std::abs(target-previousTarget)>1e-12){left=n;increment=(target-v)/n;}
    }
    static double tick(double& v,double increment,std::uint32_t& left) noexcept {if(left){v+=increment;if(!--left){if(std::abs(v)<1e-12)v=0;else if(std::abs(v-1)<1e-12)v=1;}}return v;}
    void tickParameters() noexcept {
        for(std::size_t i=0;i<current.size();++i)if(remaining[i]){current[i]+=increments[i];if(!--remaining[i])current[i]=targets[i];}
        tick(freezeAmount,freezeIncrement,freezeRemaining);tick(pingAmount,pingIncrement,pingRemaining);
        tick(crossAmount,crossIncrement,crossRemaining);tick(startAmount,startIncrement,startRemaining);
        tick(bypassAmount,bypassIncrement,bypassRemaining);
    }
    double protect(double x) noexcept {
        if(!std::isfinite(x)){++diagnostics.protectionCount;return 0;}
        double a=std::abs(x);
        if(a<=8)return scrub(x);
        ++diagnostics.protectionCount;
        // Transparent below +18.06 dBFS; asymptotes at +/-16 inside the delay only.
        return std::copysign(8+8*((a-8)/(a)),x);
    }
    void updateTail() noexcept {
        infiniteTail.store(t(freeze)>=0.5,std::memory_order_relaxed);
        double g=std::clamp(std::max(t(feedback),p(feedback))/100,0.0,0.95);
        double repeats=g>0?std::ceil(std::log(1e-5)/std::log(g))+1:1;
        double mainMs=0,tapMs=0;
        for(int ch=0;ch<2;++ch){
            double requested=t(ch?syncR:syncL)>=0.5?(headsInitialized?(ch?diagnostics.effectiveTimeR:diagnostics.effectiveTimeL):std::min(8000.0,60000.0/t(localTempo)*noteBeats(int(t(ch?noteR:noteL))))):std::max(t(ch?timeR:timeL),p(ch?timeR:timeL));
            double distance=headsInitialized?heads[ch].maximumDistance()*1000/prepared.sampleRate:requested;
            mainMs=std::max(mainMs,std::min(8000.0,std::max(requested,distance)+std::max(t(modDepth),p(modDepth))));
            auto enabled=ch?tap4Enabled:tap3Enabled,time=ch?tap4Time:tap3Time;
            if(t(enabled)>0 || p(enabled)>0){double auxiliary=std::max(t(time),p(time));if(headsInitialized)auxiliary=std::max(auxiliary,(heads[ch+2].maximumDistance()-1)*1000/prepared.sampleRate);tapMs=std::max(tapMs,auxiliary);}
        }
        // Filter decay margin is necessary only when feedback actually circulates.
        double seconds=g>0?(mainMs*repeats+tapMs)/1000+4:std::max(mainMs,tapMs)/1000;
        // Reserve the complete FIR response and wet tone decay beyond its group delay.
        double colorMargin=(p(drive)>0 || t(drive)>0)?30:driveLatency;
        if(p(tilt)!=0 || t(tilt)!=0)seconds+=0.05;
        double samples=std::ceil(seconds*prepared.sampleRate+colorMargin+msSamples(50)*(freezeAmount>0?1:0));
        tailCount.store(std::uint32_t(std::min(samples,double(std::numeric_limits<std::uint32_t>::max()-1))),std::memory_order_relaxed);
    }
    template<class Sample> void render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
        if(!preparedOk)return;
        const bool hostTempo=context.tempoValid && std::isfinite(context.bpm) && context.bpm>0;
        double bpm=hostTempo?context.bpm:p(localTempo);
        std::array<double,2> effectiveMs{};
        for(int ch=0;ch<2;++ch) {
            auto sync=ch?syncR:syncL,note=ch?noteR:noteL,time=ch?timeR:timeL;
            double requested=t(sync)>=0.5?60000.0/bpm*noteBeats(int(t(note))):p(time);
            effectiveMs[ch]=std::clamp(requested,1.0,8000.0);
            diagnostics.syncClamped|=t(sync)>=0.5 && requested!=effectiveMs[ch];
        }
        diagnostics.syncUnavailable=(t(syncL)>=0.5 || t(syncR)>=0.5) && !hostTempo;
        diagnostics.effectiveTimeL=effectiveMs[0];diagnostics.effectiveTimeR=effectiveMs[1];
        if(!headsInitialized) {
            heads[0].reset(effectiveMs[0]*prepared.sampleRate/1000);
            heads[1].reset(effectiveMs[1]*prepared.sampleRate/1000);
            heads[2].reset(p(tap3Time)*prepared.sampleRate/1000+1);
            heads[3].reset(p(tap4Time)*prepared.sampleRate/1000+1);
            headsInitialized=true;
            freezeDistances={std::round(heads[0].effective()),std::round(heads[1].effective())};
        }
        updateTail(); // includes old/pending read heads; never a cosmetic cap
        for(std::uint32_t sample=0;sample<block.samples;++sample) {
            tickParameters();
            bool analysisValid=true;
            std::array<double,2> raw{},trimmed{},main{},loop{},wet{},delayedRaw{},delayedTrimmed{};
            // Capture all input channels before writing any in-place output.
            for(std::uint32_t ch=0;ch<block.inputChannels && ch<2;++ch) {
                double v=block.inputs[ch] && !(block.inputSilenceFlags&(1ull<<ch))?block.inputs[ch][sample]:0;
                if(!std::isfinite(v)){v=0;telemetry.invalidInput=diagnostics.invalidInput=true;analysisValid=false;}
                raw[ch]=v;telemetry.inputPeak=std::max(telemetry.inputPeak,std::abs(v));
            }
            if(block.inputChannels==1)raw[1]=raw[0];
            double inGain=dbGain(p(input)),outGain=dbGain(p(output));
            for(int ch=0;ch<2;++ch){trimmed[ch]=raw[ch]*inGain;delayedRaw[ch]=dryPads[ch].tick(raw[ch]);delayedTrimmed[ch]=trimPads[ch].tick(trimmed[ch]);}
            bpm=hostTempo?context.bpm:p(localTempo);
            auto n=msSamples(p(slew));bool smooth=t(timeChange)>=0.5;
            const bool frozen=t(freeze)>=0.5;
            if(frozen && !wasFrozen) {
                freezeDistances={std::round(heads[0].effective()),std::round(heads[1].effective())};
                // Identity/swap at fixed integer reads is a contraction while entering.
                for(int ch=0;ch<2;++ch)heads[ch].set(freezeDistances[ch],true,msSamples(50));
            }
            wasFrozen=frozen;
            for(int ch=0;ch<2;++ch) {
                double requested=t(ch?syncR:syncL)>=0.5?60000.0/bpm*noteBeats(int(t(ch?noteR:noteL))):p(ch?timeR:timeL);
                effectiveMs[ch]=frozen?freezeDistances[ch]*1000/prepared.sampleRate:std::clamp(requested,1.0,8000.0);
                double base=frozen?freezeDistances[ch]:effectiveMs[ch]*prepared.sampleRate/1000;
                heads[ch].set(base,frozen?true:smooth,frozen?msSamples(50):n);
                double depth=p(modDepth)*prepared.sampleRate/1000;
                depth=std::max(0.0,std::min({depth,base*0.2,base-prepared.sampleRate/1000,8*prepared.sampleRate-base}));
                double modulation=frozen?0:depth*std::sin(phase+(ch?p(modPhase)*pi/180:0));
                main[ch]=heads[ch].read(rings[ch],modulation);
            }
            if(!frozen){phase+=2*pi*p(modRate)/prepared.sampleRate;if(phase>=2*pi)phase-=2*pi;}
            double lpHz=std::clamp(p(highCut),1.0,0.45*prepared.sampleRate);
            double hpHz=std::clamp(p(highPass),0.1,std::max(0.1,lpHz*0.95));
            double hpK=std::tan(pi*hpHz/prepared.sampleRate),lpK=std::tan(pi*lpHz/prepared.sampleRate);
            for(int ch=0;ch<2;++ch) {
                double filtered=filters[ch].process(main[ch],hpK,lpK);
                loop[ch]=(1-freezeAmount)*filtered+freezeAmount*main[ch];
            }
            double cross=(1-freezeAmount)*crossAmount+freezeAmount*pingAmount;
            double g=(1-freezeAmount)*p(feedback)/100+freezeAmount;
            double mono=block.inputChannels==1?trimmed[0]:0.5*(trimmed[0]+trimmed[1]);
            std::array<double,2> inject={
                (1-pingAmount)*trimmed[0]+pingAmount*(1-startAmount)*mono,
                (1-pingAmount)*trimmed[1]+pingAmount*startAmount*mono
            };
            for(int ch=0;ch<2;++ch)rings[ch].write(protect((1-freezeAmount)*inject[ch]+g*((1-cross)*loop[ch]+cross*loop[1-ch])));
            // Main tap controls and the two output-only auxiliary taps never enter feedback.
            auto panInto=[&](double x,double pan) {
                double q=std::clamp((pan/100+1)/2,0.0,1.0);
                if(block.outputChannels==1)wet[0]+=x;
                else {wet[0]+=(1-q)*x;wet[1]+=q*x;}
            };
            panInto(main[0]*p(tap1Enabled)*levelGain(p(tap1Level)),p(tap1Pan));
            if(block.outputChannels>1 || block.inputChannels>1 || pingAmount>0)
                panInto(main[1]*p(tap2Enabled)*levelGain(p(tap2Level)),p(tap2Pan));
            // Rings have already advanced one sample: add one to auxiliary distances.
            for(int tap=0;tap<2;++tap) {
                auto enabled=tap?tap4Enabled:tap3Enabled,time=tap?tap4Time:tap3Time;
                auto level=tap?tap4Level:tap3Level,pan=tap?tap4Pan:tap3Pan;
                heads[tap+2].set(p(time)*prepared.sampleRate/1000+1,smooth,n);
                double x=heads[tap+2].read(rings[tap],0);
                panInto(x*p(enabled)*levelGain(p(level)),p(pan));
            }
            double duckGain=1;
            if(p(duckAmount)>0) {
                double detector=std::max(std::abs(trimmed[0]),std::abs(trimmed[1]));
                double coefficient=std::exp(-1/(prepared.sampleRate*(detector>duckEnvelope?p(duckAttack):p(duckRelease))/1000));
                duckEnvelope=coefficient*duckEnvelope+(1-coefficient)*detector;
                double over=20*std::log10(std::max(duckEnvelope,1e-15))-p(duckThreshold);
                double reduction=over<=-3?0:over<3?(over+3)*(over+3)/24:over*0.5;
                diagnostics.duckReduction=std::min(reduction,p(duckAmount));duckGain=dbGain(-diagnostics.duckReduction);
            } else {duckEnvelope=0;diagnostics.duckReduction=0;}
            for(int ch=0;ch<2;++ch) {
                wet[ch]=colors[ch].tick(wet[ch],p(drive),firCoefficients);
                double low=tone[ch].process(wet[ch],std::tan(pi*std::min(1000.0,0.4*prepared.sampleRate)/prepared.sampleRate),false);
                double lowGain=dbGain(-p(tilt)/2),highGain=dbGain(p(tilt)/2);
                wet[ch]=lowGain*low+highGain*(wet[ch]-low);
            }
            if(block.outputChannels==2){double mid=(wet[0]+wet[1])*0.5,side=(wet[0]-wet[1])*0.5*(p(width)/100);wet={mid+side,mid-side};}
            EffectAnalysisSample measured{};
            for(std::uint32_t ch=0;ch<block.outputChannels && ch<2;++ch) {
                double m=p(mix)/100,dry=delayedTrimmed[ch];
                const double contribution=(1-bypassAmount)*m*wet[ch]*duckGain*levelGain(p(wetLevel))*outGain;
                double processed=((1-m)*dry+m*wet[ch]*duckGain*levelGain(p(wetLevel)))*outGain;
                double v=bypassAmount>=1?delayedRaw[ch]:(1-bypassAmount)*processed+bypassAmount*delayedRaw[ch];
                if(!std::isfinite(v)){v=0;diagnostics.invalidInput=telemetry.invalidInput=true;analysisValid=false;}
                // Avoid float32 overflow on pathological yet finite source samples.
                if(std::abs(v)>double(std::numeric_limits<Sample>::max()))analysisValid=false;
                v=std::clamp(v,-double(std::numeric_limits<Sample>::max()),double(std::numeric_limits<Sample>::max()));
                if(!std::isfinite(contribution))analysisValid=false;else measured.wet[ch]=contribution;
                if(block.outputs[ch])block.outputs[ch][sample]=Sample(v);
                telemetry.outputPeak=std::max(telemetry.outputPeak,std::abs(v));
            }
            if(context.analysis) {
                if(analysisValid)measured.validFields=analysisWet|analysisDelay;
                measured.delayMs[0]=effectiveMs[0];measured.delayMs[1]=effectiveMs[1];
                context.analysis->pushSample(context.blockSampleOffset+sample,measured);
            }
        }
        diagnostics.effectiveTimeL=effectiveMs[0];diagnostics.effectiveTimeR=effectiveMs[1];
    }
public:
    DelayEngine(){for(std::size_t i=0;i<current.size();++i)current[i]=targets[i]=parameters[i].initial;}
    bool prepare(const PrepareSpec& s) override {
        preparedOk=false;
        if(!s.valid() || s.sampleRate<8000 || s.sampleRate>384000 || s.sidechainChannels ||
            (s.inputChannels==2 && s.outputChannels==1))return false;
        prepared=s;
        try{for(auto& ring:rings)ring.prepare(std::size_t(std::ceil(s.sampleRate*8))+4);}catch(...){return false;}
        double sum=0;
        for(int i=0;i<31;++i){double x=i-15,c=std::abs(x)<1e-9?0.44:std::sin(0.44*pi*x)/(pi*x);c*=0.5-0.5*std::cos(2*pi*i/30);firCoefficients[i]=c;sum+=c;}
        for(auto& c:firCoefficients)c/=sum;
        preparedOk=true;reset(ResetReason::firstActivation);updateTail();return true;
    }
    void reset(ResetReason) noexcept override {
        for(auto& ring:rings)ring.reset();for(auto& filter:filters)filter.reset();
        for(auto& color:colors)color.reset();for(auto& pad:dryPads)pad.reset();for(auto& pad:trimPads)pad.reset();for(auto& f:tone)f.reset();
        current=targets;remaining.fill(0);increments.fill(0);phase=duckEnvelope=0;
        headsInitialized=false;wasFrozen=false;initialTargets=true;telemetry={};diagnostics={};
        freezeRemaining=pingRemaining=crossRemaining=startRemaining=bypassRemaining=0;
        freezeAmount=t(freeze);pingAmount=t(route)==2?1:0;crossAmount=t(route)==2?1:t(crossfeed)/100;
        startAmount=t(start);bypassAmount=t(bypass);diagnosticSnapshot.publish(diagnostics);
    }
    void applyTargets(const SoundState& s,std::int32_t) noexcept override {
        for(std::size_t i=0;i<targets.size();++i) {
            double next=s.targets[i]==parameters[i].toNormalized(parameters[i].initial)?parameters[i].initial:parameters[i].toPhysical(s.targets[i]);
            if(initialTargets){targets[i]=current[i]=next;remaining[i]=0;continue;}
            if(next==targets[i])continue;
            targets[i]=next;auto id=parameters[i].id;
            // Read-head time transitions and route/Freeze/bypass use their own paths.
            if(id==timeL || id==timeR || id==tap3Time || id==tap4Time || parameters[i].transition==Transition::discrete) {
                if(id==tap1Enabled || id==tap2Enabled || id==tap3Enabled || id==tap4Enabled){remaining[i]=msSamples(20);increments[i]=(next-current[i])/remaining[i];}
                else {current[i]=next;remaining[i]=0;}
            } else {remaining[i]=msSamples(parameters[i].smoothingMs);increments[i]=(next-current[i])/remaining[i];}
        }
        transition(t(freeze),freezeAmount,freezeIncrement,freezeRemaining,msSamples(50),initialTargets);
        transition(t(route)==2?1:0,pingAmount,pingIncrement,pingRemaining,msSamples(50),initialTargets);
        transition(t(route)==2?1:t(crossfeed)/100,crossAmount,crossIncrement,crossRemaining,msSamples(50),initialTargets);
        transition(t(start),startAmount,startIncrement,startRemaining,msSamples(50),initialTargets);
        transition(t(bypass),bypassAmount,bypassIncrement,bypassRemaining,msSamples(10),initialTargets);
        initialTargets=false;if(preparedOk)updateTail();
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {updateTail();telemetry.latencySamples=driveLatency;telemetryQueue.push(telemetry);telemetry={};diagnosticSnapshot.publish(diagnostics);diagnostics.syncClamped=false;}
    std::uint32_t latencySamples() const noexcept override {return driveLatency;}
    Tail tailSamples() const noexcept override {return {infiniteTail.load(std::memory_order_relaxed)?TailKind::infinite:TailKind::finite,tailCount.load(std::memory_order_relaxed)};}
    bool readTelemetry(Telemetry& out) noexcept override {return telemetryQueue.pop(out);}
    bool readDiagnostics(Diagnostics& out) const noexcept {return diagnosticSnapshot.read(out);}
};
}
