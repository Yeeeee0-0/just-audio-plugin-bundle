#include "GateEngine.hpp"
#include <limits>
namespace just::gate {
GateEngine::GateEngine() noexcept {
    for(std::size_t i=0;i<parameterCount;++i){targets[i]=values[i]=parameters[i].initial;smoothers[i].reset(values[i]);}
    reset(ResetReason::firstActivation);
}
bool GateEngine::prepare(const PrepareSpec& s) {
    if(!s.valid() || s.inputChannels!=s.outputChannels || s.sampleRate<8000 || s.sampleRate>384000)return false;
    prepared=s;rate=s.sampleRate;
    smoothingSamples=std::max(1u,unsigned(std::ceil(rate*0.003)));
    modeSamples=std::max(1u,unsigned(std::ceil(rate*0.005)));
    finishSamples=std::max(1u,unsigned(std::ceil(rate*0.0005)));
    rmsCoefficient=std::exp(-1/(rate*0.005));reset(ResetReason::sampleRateChange);return true;
}
void GateEngine::reset(ResetReason reason) noexcept {
    for(auto& f:filters)f.reset();rmsEnergy={};gateTrigger.reset();duckTrigger.reset();
    envelopes[0].reset(-value(range));envelopes[1].reset(-value(range));envelopes[2].reset(0);
    current={};diagnostics_={};firstAudio=true;if(reason!=ResetReason::seek)firstTargets=true;
}
double GateEngine::boundedMultiply(double a,double b) noexcept {
    const double product=a*b;if(std::isfinite(product))return product;
    current.invalidInput=true;
    return std::copysign(std::numeric_limits<double>::max(),a)*std::copysign(1.0,b);
}
void GateEngine::applyTargets(const SoundState& state,std::int32_t) noexcept {
    const bool first=firstTargets;
    for(std::size_t i=0;i<parameterCount;++i) {
        const auto& p=parameters[i];const double physical=p.toPhysical(state.targets[i]);
        if(first){targets[i]=values[i]=physical;smoothers[i].reset(physical);}
        else if(physical!=targets[i]){targets[i]=physical;smoothers[i].setTarget(physical,p.transition==Transition::continuous && p.smoothingMs>0?smoothingSamples:0);}
    }
    const auto next=static_cast<Mode>(unsigned(targets[registry.index(mode)]));
    if(first) {
        requestedMode=next;for(unsigned i=0;i<3;++i)modeWeights[i].reset(i==unsigned(next)?1:0);
        bypassMix.reset(targets[0]);sourceMix.reset(targets[registry.index(scSource)]);
        hpMix.reset(targets[registry.index(scHPEnabled)]);lpMix.reset(targets[registry.index(scLPEnabled)]);
        detectorMix.reset(targets[registry.index(detector)]);connectedMix.reset(1);previousConnected=true;
        envelopes[0].reset(-value(range));envelopes[1].reset(-value(range));envelopes[2].reset(0);
    } else {
        if(next!=requestedMode){requestedMode=next;for(unsigned i=0;i<3;++i)modeWeights[i].setTarget(i==unsigned(next)?1:0,modeSamples);}
        if(bypassMix.effective()!=targets[0] && targets[0]!=value(bypass))bypassMix.setTarget(targets[0],modeSamples);
        if(targets[registry.index(scSource)]!=value(scSource))sourceMix.setTarget(targets[registry.index(scSource)],smoothingSamples);
        if(targets[registry.index(scHPEnabled)]!=value(scHPEnabled))hpMix.setTarget(targets[registry.index(scHPEnabled)],smoothingSamples);
        if(targets[registry.index(scLPEnabled)]!=value(scLPEnabled))lpMix.setTarget(targets[registry.index(scLPEnabled)],smoothingSamples);
        if(targets[registry.index(detector)]!=value(detector))detectorMix.setTarget(targets[registry.index(detector)],smoothingSamples);
    }
    firstTargets=false;
    diagnostics_.lookaheadPending=targets[registry.index(lookahead)]>0 || state.configurationCount>0;
}
template<class Sample> void GateEngine::render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
    if(context.seek)reset(ResetReason::seek);
    if(block.inputChannels<1 || block.inputChannels>2 || block.outputChannels!=block.inputChannels || block.sidechainChannels>2)return;
    bool connected=prepared.sidechainChannels>0 && block.sidechainChannels>0;
    if(connected)for(unsigned ch=0;ch<block.sidechainChannels;++ch)connected&=block.sidechain[ch]!=nullptr || (block.sidechainSilenceFlags&(1ull<<ch));
    const bool external=targets[registry.index(scSource)]>=0.5,useEffect=!external || connected;
    if(firstAudio){connectedMix.reset(useEffect?1:0);previousConnected=useEffect;firstAudio=false;}
    else if(useEffect!=previousConnected){connectedMix.setTarget(useEffect?1:0,smoothingSamples);previousConnected=useEffect;}
    diagnostics_.sidechainMissing=external && !connected;diagnostics_.sidechainSilent=external && connected;
    for(unsigned sample=0;sample<block.samples;++sample) {
        for(std::size_t p=0;p<parameterCount;++p)values[p]=smoothers[p].tick();
        const double source=sourceMix.tick(),hp=hpMix.tick(),lp=lpMix.tick(),rms=detectorMix.tick();
        const double bypassWeight=bypassMix.tick(),connection=connectedMix.tick();
        const double inputGain=linear(value(input)),outputGain=linear(value(output)),scTrim=linear(value(scGain));
        const double hpG=std::tan(3.141592653589793*std::min(value(scHPHz),0.45*rate)/rate);
        const double lpG=std::tan(3.141592653589793*std::min(value(scLPHz),0.45*rate)/rate);
        double raw[2]={},main[2]={},peak=0,energy=0;bool sampleSCActive=false;
        for(unsigned ch=0;ch<block.inputChannels;++ch) {
            double x=block.inputs[ch] && !(block.inputSilenceFlags&(1ull<<ch))?double(block.inputs[ch][sample]):0;
            if(!std::isfinite(x)){x=0;current.invalidInput=true;filters[ch].reset();rmsEnergy[ch]=0;}
            raw[ch]=x;main[ch]=boundedMultiply(x,inputGain);current.inputPeak=std::max(current.inputPeak,std::abs(x));
        }
        for(unsigned ch=0;ch<2;++ch) {
            double side=0;
            if(connected) {
                const auto channel=std::min(ch,block.sidechainChannels-1);
                if(block.sidechain[channel] && !(block.sidechainSilenceFlags&(1ull<<channel)))side=double(block.sidechain[channel][sample]);
                if(!std::isfinite(side)){side=0;current.invalidInput=true;filters[ch].reset();rmsEnergy[ch]=0;}
                diagnostics_.sidechainSilent&=side==0;sampleSCActive|=side!=0;
            }
            const double internal=main[std::min(ch,block.inputChannels-1)];
            // Cap detector arithmetic only, so squaring cannot overflow; finite
            // over-0dBFS main audio is neither clamped nor secretly limited.
            double detect=std::clamp((1-source)*std::clamp(internal,-1e100,1e100)+source*boundedMultiply(std::clamp(side,-1e100,1e100),scTrim),-1e100,1e100);
            detect=filters[ch].tick(detect,hpG/(1+hpG),lpG/(1+lpG),hp,lp);
            rmsEnergy[ch]=rmsCoefficient*rmsEnergy[ch]+(1-rmsCoefficient)*detect*detect;
            if(rmsEnergy[ch]<1e-30)rmsEnergy[ch]=0;
            peak=std::max(peak,std::abs(detect));energy=std::max(energy,rmsEnergy[ch]);
        }
        const double level=decibels((1-rms)*peak+rms*std::sqrt(energy));
        const double depth=value(range),open=value(threshold),close=open-value(hysteresis);
        const auto holdSamples=std::uint64_t(std::llround(rate*value(hold)*0.001));
        const double ac=std::exp(-1/(rate*value(attack)*0.001)),rc=std::exp(-1/(rate*value(release)*0.001));
        const bool ga=gateTrigger.tick(level,open,close,holdSamples,envelopes[0].value()==0,envelopes[0].value()==-depth);
        const bool da=duckTrigger.tick(level,open,close,holdSamples,envelopes[2].value()==-depth,envelopes[2].value()==0);
        double modeGain[3];
        modeGain[0]=envelopes[0].tick(ga?0:-depth,ga?ac:rc,finishSamples);
        const double expand=expansion(level,open,value(ratio),value(knee),depth);
        if(value(ratio)==1)envelopes[1].reset(0);
        modeGain[1]=envelopes[1].tick(expand,expand>envelopes[1].value()?ac:rc,finishSamples);
        modeGain[2]=envelopes[2].tick(da?-depth:0,da?ac:rc,finishSamples);
        double gain=0;for(unsigned i=0;i<3;++i)gain+=modeWeights[i].tick()*linear(modeGain[i]);
        gain=1+connection*(gain-1);diagnostics_.gainDb=decibels(gain);diagnostics_.detectorDb=level;
        diagnostics_.gatePhase=gateTrigger.phase();diagnostics_.duckPhase=duckTrigger.phase();diagnostics_.holdRemaining=gateTrigger.remaining();
        if(context.analysis) {
            EffectAnalysisSample measured;measured.validFields=analysisReduction|analysisGate|analysisSidechain;
            measured.reductionDb=std::max(0.,-diagnostics_.gainDb);
            measured.gateState=(unsigned(requestedMode)<<8)|unsigned(requestedMode==Mode::Duck?duckTrigger.phase():gateTrigger.phase());
            measured.flags=(external?1u:0u)|(connected?2u:0u)|(sampleSCActive?4u:0u);
            for(unsigned i=0;i<3;++i)if(modeWeights[i].effective()>0 && modeWeights[i].effective()<1)measured.flags|=8u;
            context.analysis->pushSample(context.blockSampleOffset+sample,measured);
        }
        for(unsigned ch=0;ch<block.outputChannels;++ch)if(block.outputs[ch]) {
            const double wet=boundedMultiply(boundedMultiply(main[ch],gain),outputGain);
            double result=bypassWeight==0?wet:bypassWeight==1?raw[ch]:bypassWeight*raw[ch]+(1-bypassWeight)*wet;
            const double limit=double(std::numeric_limits<Sample>::max());
            if(!std::isfinite(result)){result=0;current.invalidInput=true;}
            if(std::abs(result)>limit){result=std::copysign(limit,result);current.invalidInput=true;}
            block.outputs[ch][sample]=Sample(result);current.outputPeak=std::max(current.outputPeak,std::abs(result));
        }
    }
}
template void GateEngine::render<float>(AudioBlock<float>,const ProcessContext&) noexcept;
template void GateEngine::render<double>(AudioBlock<double>,const ProcessContext&) noexcept;
}
