#include "Processor.hpp"
#include "StateStreams.hpp"
#include "LayoutMessages.hpp"
#include "PresetMessages.hpp"
#include "../runtime/AnalysisReader.hpp"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
namespace just {
using namespace Steinberg;
using namespace Steinberg::Vst;
#include "PresetProcessor.inc"
#include "AuditionProcessor.inc"
void Processor::publishLayout(bool unavailable) {
    if(!getPeer() || !layoutSession)return;
    BusLayoutSnapshot s;s.sequence=++layoutSequence;s.session=layoutSession;
    if(!unavailable && getAudioInput(0) && getAudioOutput(0)) {
        s.validFields=layoutBuses;
        s.inputChannels=SpeakerArr::getChannelCount(getAudioInput(0)->getArrangement());
        s.outputChannels=SpeakerArr::getChannelCount(getAudioOutput(0)->getArrangement());
        if(auto* sc=getAudioInput(1)){s.sidechainChannels=SpeakerArr::getChannelCount(sc->getArrangement());s.sidechainActive=sc->isActive();}
        s.active=layoutActive;
        if(layoutPrepared){s.validFields|=layoutSampleRate;s.sampleRate=prepared.sampleRate;s.offline=prepared.offline;}
    }
    auto message=owned(allocateMessage());
    if(writeLayoutMessage(message.get(),s,identity().processor))sendMessage(message);
}
tresult PLUGIN_API Processor::connect(IConnectionPoint* other) {
    auto result=AudioEffect::connect(other);if(result==kResultOk){telemetryTransport.connect(other,getHostContext());stateTimer=owned(Timer::create(this,33));publishLayout();}return result;
}
tresult PLUGIN_API Processor::disconnect(IConnectionPoint* other) {
    if(other==getPeer()){auditionMain.enabled=0;auditionPending.publish(auditionMain);}
    if(other==getPeer() && stateTimer){stateTimer->stop();stateTimer=nullptr;}
    if(other==getPeer()){analysisTransport.stop();analysisRequested.store(false);telemetryTransport.disconnect(other);telemetrySession.store(0,std::memory_order_release);publishLayout(true);layoutSession=0;}
    return AudioEffect::disconnect(other);
}
tresult PLUGIN_API Processor::notify(IMessage* message) {
    if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),auditionRequestID))return handleAuditionMessage(message);
    if(message && message->getMessageID() && (!std::strcmp(message->getMessageID(),presetRequestID) || !std::strcmp(message->getMessageID(),presetQueryID)))return handlePresetMessage(message);
    if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),analysisSubscriptionID)) {
        auto* a=message->getAttributes();int64 session=0,enabled=0;
        if(!a || a->getInt("Session",session)!=kResultOk || std::uint64_t(session)!=telemetrySession.load() || !session ||
            a->getInt("Enabled",enabled)!=kResultOk || (enabled!=0 && enabled!=1))return kInvalidArgument;
        analysisRequested.store(enabled!=0,std::memory_order_release);return kResultOk;
    }
    if(message && message->getMessageID() && std::strcmp(message->getMessageID(),layoutRequestID)==0) {
        std::uint64_t session=0;
        if(!readLayoutRequest(message,identity().processor,session))return kInvalidArgument;
        if(session<lastLayoutSession)return kResultFalse;
        lastLayoutSession=layoutSession=session;telemetrySession.store(session,std::memory_order_release);
        // Controller may connect first: remember its token before our peer exists.
        publishLayout();return kResultOk;
    }
    return AudioEffect::notify(message);
}
tresult PLUGIN_API Processor::activateBus(MediaType type,BusDirection direction,int32 index,TBool state) {
    auto result=AudioEffect::activateBus(type,direction,index,state);
    if(result==kResultOk && type==kAudio)publishLayout();return result;
}
class VstEvents final:public EventSource {
    IParameterChanges* changes;
public:
    explicit VstEvents(IParameterChanges* p):changes(p){}
    int32 queueCount() const noexcept override{return changes?changes->getParameterCount():0;}
    ParamID parameterID(int32 q) const noexcept override{auto* p=changes->getParameterData(q);return p?p->getParameterId():~ParamID(0);}
    int32 pointCount(int32 q) const noexcept override{auto* p=changes->getParameterData(q);return p?p->getPointCount():0;}
    bool point(int32 q,int32 index,ParamEvent& e) const noexcept override {
        auto* p=changes->getParameterData(q);if(!p)return false;
        e.id=p->getParameterId();return p->getPoint(index,e.sampleOffset,e.normalizedValue)==kResultTrue;
    }
};
Processor::Processor() {
    if(moduleDefinition().valid())engine.reset(moduleDefinition().createEngine());
    const auto& uid=identity().controller.words;setControllerClass(FUID(uid[0],uid[1],uid[2],uid[3]));
    activePublication.publish(current);pendingPublication.publish(current);
    presetAudio.state=current;presetPublication.publish(presetAudio);
    analysis.setSender(identity().processor,&analysisTransport,[](void* p,const AnalysisMessage& m){return static_cast<AnalysisTransport*>(p)->send(m);});
}
tresult PLUGIN_API Processor::initialize(FUnknown* host) {
    if(!engine || !moduleDefinition().valid())return kResultFalse;
    auto result=AudioEffect::initialize(host);if(result!=kResultOk)return result;
    addAudioInput(STR16("Input"),SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"),SpeakerArr::kStereo);
    if(moduleDefinition().buses.optionalSidechain)addAudioInput(STR16("Sidechain"),SpeakerArr::kStereo,kAux,0);
    processContextRequirements.needTempo();processContextRequirements.needTimeSignature();
    processContextRequirements.needProjectTimeMusic();processContextRequirements.needTransportState();
    return kResultOk;
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* ins,int32 ni,SpeakerArrangement* outs,int32 no) {
    const auto buses=moduleDefinition().buses;
    if(!ins || !outs || ni!=(buses.optionalSidechain?2:1) || no!=1)return kResultFalse;
    bool main=(buses.mono && ins[0]==SpeakerArr::kMono && outs[0]==SpeakerArr::kMono) ||
        (buses.stereo && ins[0]==SpeakerArr::kStereo && outs[0]==SpeakerArr::kStereo) ||
        (buses.monoToStereo && ins[0]==SpeakerArr::kMono && outs[0]==SpeakerArr::kStereo);
    if(!main || (ni==2 && ins[1]!=SpeakerArr::kEmpty && ins[1]!=SpeakerArr::kMono && ins[1]!=SpeakerArr::kStereo))return kResultFalse;
    auto result=AudioEffect::setBusArrangements(ins,ni,outs,no);
    if(result==kResultOk){layoutPrepared=false;publishLayout();}return result;
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size){return size==kSample32 || size==kSample64?kResultTrue:kResultFalse;}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& spec) {
    if(spec.maxSamplesPerBlock<=0 || canProcessSampleSize(spec.symbolicSampleSize)!=kResultTrue ||
       !std::isfinite(spec.sampleRate) || spec.sampleRate<=0)return kInvalidArgument;
    auto oldRate=prepared.sampleRate;
    prepared={spec.sampleRate,static_cast<uint32>(spec.maxSamplesPerBlock),
        static_cast<uint32>(SpeakerArr::getChannelCount(getAudioInput(0)->getArrangement())),static_cast<uint32>(SpeakerArr::getChannelCount(getAudioOutput(0)->getArrangement())),
        getAudioInput(1) && getAudioInput(1)->isActive()?static_cast<uint32>(SpeakerArr::getChannelCount(getAudioInput(1)->getArrangement())):0,
        spec.symbolicSampleSize==kSample32?SampleFormat::float32:SampleFormat::float64,spec.processMode==kOffline};
    if(!engine->prepare(prepared))return kResultFalse;
    analysis.prepare(prepared.sampleRate,prepared.maxBlockSize,engine->latencySamples());
    if(oldRate!=prepared.sampleRate)engine->reset(ResetReason::sampleRateChange);
    havePosition=false;auto result=AudioEffect::setupProcessing(spec);
    if(result==kResultOk){layoutPrepared=true;publishLayout();}return result;
}
tresult PLUGIN_API Processor::setActive(TBool active) {
    if(!active){auditionMain.enabled=0;auditionPending.publish(auditionMain);endAudioAudition();}
    if(active && firstActivation){engine->reset(ResetReason::firstActivation);firstActivation=false;}
    if(bool(active)!=layoutActive){
        if(!active){analysisTransport.stop();telemetryTransport.deactivate();}
        else {analysis.restart();analysisTransport.start();telemetrySamples=0;telemetryTransport.activate(processSetup);}
    }
    auto result=AudioEffect::setActive(active);
    if(result==kResultOk){layoutActive=active;publishLayout();}
    else if(active)telemetryTransport.deactivate();return result;
}
ProcessContext Processor::contextFor(const ProcessData& data) noexcept {
    ProcessContext c;auto* p=data.processContext;if(!p){havePosition=false;return c;}
    c.transportValid=true;
    c.playing=(p->state&Steinberg::Vst::ProcessContext::kPlaying)!=0;
    c.cycleActive=(p->state&Steinberg::Vst::ProcessContext::kCycleActive)!=0;
    c.tempoValid=(p->state&Steinberg::Vst::ProcessContext::kTempoValid) && std::isfinite(p->tempo) && p->tempo>0;
    if(c.tempoValid)c.bpm=p->tempo;
    c.timeSignatureValid=(p->state&Steinberg::Vst::ProcessContext::kTimeSigValid) && p->timeSigNumerator>0 && p->timeSigDenominator>0;
    if(c.timeSignatureValid){c.numerator=p->timeSigNumerator;c.denominator=p->timeSigDenominator;}
    c.ppqValid=(p->state&Steinberg::Vst::ProcessContext::kProjectTimeMusicValid) && std::isfinite(p->projectTimeMusic);
    if(c.ppqValid)c.ppq=p->projectTimeMusic;
    c.projectSamplesValid=true;c.projectSamples=p->projectTimeSamples;
    c.seek=havePosition && c.playing && p->projectTimeSamples!=nextProjectSample;
    nextProjectSample=p->projectTimeSamples<=std::numeric_limits<std::int64_t>::max()-data.numSamples?p->projectTimeSamples+data.numSamples:p->projectTimeSamples;
    havePosition=c.playing;
    return c;
}
template<class Sample> void Processor::render(ProcessData& data,const ProcessContext& context) noexcept {
    const auto& input=data.inputs[0];auto& output=data.outputs[0];
    auto** in=std::is_same<Sample,float>::value?reinterpret_cast<Sample**>(input.channelBuffers32):reinterpret_cast<Sample**>(input.channelBuffers64);
    auto** out=std::is_same<Sample,float>::value?reinterpret_cast<Sample**>(output.channelBuffers32):reinterpret_cast<Sample**>(output.channelBuffers64);
    if(!out)return;
    if(context.analysis)analysis.capture(in,input.numChannels,input.silenceFlags,data.numSamples);
    auto makeBlock=[&](int32 offset,int32 length) {
        AudioBlock<Sample> block;block.inputChannels=input.numChannels;block.outputChannels=output.numChannels;
        block.samples=length;block.inputSilenceFlags=input.silenceFlags;
        for(int32 ch=0;ch<input.numChannels;++ch)block.inputs[ch]=in && in[ch]?in[ch]+offset:nullptr;
        for(int32 ch=0;ch<output.numChannels;++ch)block.outputs[ch]=out[ch]?out[ch]+offset:nullptr;
        if(data.numInputs==2) {
            const auto& sc=data.inputs[1];block.sidechainChannels=sc.numChannels;block.sidechainSilenceFlags=sc.silenceFlags;
            auto** scInput=std::is_same<Sample,float>::value?reinterpret_cast<Sample**>(sc.channelBuffers32):reinterpret_cast<Sample**>(sc.channelBuffers64);
            for(int32 ch=0;ch<sc.numChannels;++ch)block.sidechain[ch]=scInput && scInput[ch]?scInput[ch]+offset:nullptr;
        }
        return block;
    };
    auto processSegment=[&](int32 offset,int32 length,const ProcessContext& c){
        if(context.analysis)analysis.captureBypass(offset,length,current.targets[registry.index(bypassParamID)]>=.5);
        if(auditionState.phase==AuditionPhase::active && auditionRemaining<std::uint64_t(length)){
            auto first=static_cast<int32>(auditionRemaining);if(first)engine->process(makeBlock(offset,first),c);endAudioAudition();
            auto rest=c;rest.blockSampleOffset=offset+first;if(first)rest.seek=false;engine->process(makeBlock(offset+first,length-first),rest);
        }else {engine->process(makeBlock(offset,length),c);if(auditionState.phase==AuditionPhase::active){auditionRemaining-=length;if(!auditionRemaining)endAudioAudition();}}
    };
    if(!data.inputParameterChanges || data.inputParameterChanges->getParameterCount()==0) {
        engine->applyTargets(current,0);processSegment(0,data.numSamples,context);
    } else {
        for(int32 sample=0;sample<data.numSamples;++sample) {
            automation.evaluate(sample,current);engine->applyTargets(current,sample);
            auto segment=context;segment.blockSampleOffset=sample;
            // projectSamples/ppq remain block origin; offset is carried separately.
            // seek is delivered only at the original block boundary.
            if(sample)segment.seek=false;
            processSegment(sample,1,segment);
        }
    }
    // Effects may produce tails from silent input. Derive output silence from
    // rendered samples instead of copying the host input flag.
    output.silenceFlags=0;
    for(int32 ch=0;ch<output.numChannels;++ch) {
        bool silent=true;
        if(out[ch])for(int32 i=0;i<data.numSamples;++i)if(out[ch][i]!=0){silent=false;break;}
        if(silent)output.silenceFlags|=1ull<<ch;
    }
    if(context.analysis){
        std::uint32_t flags=(context.playing?analysisPlaying:0)|(context.transportValid?analysisTransportKnown:0)|
            (prepared.offline?analysisOffline:0)|(current.targets[registry.index(bypassParamID)]>=.5?analysisBypassed:0);
        const auto session=telemetrySession.load(std::memory_order_acquire);
        analysis.finish(out,input.numChannels,output.numChannels,data.numSamples,session,flags);
        analysisTransport.publishSource({session,analysis.currentEpoch(),analysis.sourceSample()});
    }
}
tresult PLUGIN_API Processor::process(ProcessData& data) {
    if(!engine || data.numSamples<0 || static_cast<uint32>(data.numSamples)>prepared.maxBlockSize ||
       canProcessSampleSize(data.symbolicSampleSize)!=kResultTrue)return kInvalidArgument;
    SoundState staged;
    const auto before=current;
    const auto pending=latestRestore.load(std::memory_order_acquire);
    if(pending>current.revision && pendingPublication.read(staged) && staged.revision==pending)current=staged;
    if(!sameSoundState(before,current,registry))++presetAudio.epoch;
    const auto requested=requestedPreset.load(std::memory_order_acquire);
    if(requested && requested!=seenPreset){PresetRequest request;
        if(presetPending.read(request) && request.transaction==requested){seenPreset=requested;presetAudio.transaction=requested;
            if(request.session==telemetrySession.load(std::memory_order_acquire) && request.expectedEpoch==presetAudio.epoch && request.expectedRestore==pending){
                auto revision=current.revision;current=request.desired;current.revision=revision;++presetAudio.epoch;presetAudio.status=PresetTransactionStatus::applied;
            }else presetAudio.status=PresetTransactionStatus::rejected;
        }
    }
    const auto beforeAutomation=current;
    updateAudition();
    VstEvents events(data.inputParameterChanges);automation.begin(&events,current,data.numSamples);
    if(data.numSamples==0){automation.evaluate(0,current);engine->applyTargets(current,0);activePublication.publish(current);if(!sameSoundState(beforeAutomation,current,registry))++presetAudio.epoch;presetAudio.state=current;presetPublication.publish(presetAudio);return kResultOk;}
    if(data.numInputs<1 || data.numInputs>(moduleDefinition().buses.optionalSidechain?2:1) || data.numOutputs!=1 || !data.inputs || !data.outputs ||
       data.inputs[0].numChannels<1 || data.inputs[0].numChannels>2 ||
       data.outputs[0].numChannels<1 || data.outputs[0].numChannels>2 ||
       static_cast<uint32>(data.inputs[0].numChannels)!=prepared.inputChannels ||
       static_cast<uint32>(data.outputs[0].numChannels)!=prepared.outputChannels ||
       (data.numInputs==2 && (data.inputs[1].numChannels<0 || data.inputs[1].numChannels>2)))return kInvalidArgument;
    auto context=contextFor(data);
    analysis.setEnabled(analysisRequested.load(std::memory_order_acquire));
    if(context.seek){const bool enabled=analysis.ready();analysis.restart();analysis.setEnabled(enabled);}
    if(analysis.ready()){context.analysis=&analysis;context.analysisSourceSample=analysis.sourceSample();context.analysisEpoch=analysis.currentEpoch();}
    if(data.symbolicSampleSize==kSample32)render<float>(data,context);else render<double>(data,context);
    engine->endBlock();publishTelemetry(context,static_cast<std::uint32_t>(data.numSamples));activePublication.publish(current);
    if(!sameSoundState(beforeAutomation,current,registry))++presetAudio.epoch;presetAudio.state=current;presetPublication.publish(presetAudio);return kResultOk;
}
void Processor::publishTelemetry(const ProcessContext& c,std::uint32_t samples) noexcept {
    // Drain at most eight legacy/module snapshots each block; only this wrapper
    // consumes Engine::readTelemetry. Module UI meters must use separate storage.
    Telemetry metrics{},staged{};bool have=false;
    for(unsigned i=0;i<8;++i){if(!engine->readTelemetry(staged))break;metrics=staged;have=true;}
    const auto interval=std::max(1u,static_cast<std::uint32_t>(std::min(prepared.sampleRate/30.,double(std::numeric_limits<std::uint32_t>::max()))));
    telemetrySamples=static_cast<std::uint32_t>(std::min(std::uint64_t(telemetrySamples)+samples,std::uint64_t(interval)));
    if(telemetrySamples<interval)return;telemetrySamples=0;
    RuntimeTelemetrySnapshot f;f.plugin=identity().processor;f.session=telemetrySession.load(std::memory_order_acquire);
    f.sequence=++telemetrySequence;f.queueContext=telemetryTransport.queueContext();
    f.sourceNanoseconds=analysisNow();
    if(!f.session || !f.queueContext)return;
    f.validFields=telemetryLatency;
    if(c.transportValid)f.validFields|=just::telemetryTransport;f.latencySamples=engine->latencySamples();
    f.flags=(c.playing?telemetryFlagPlaying:0)|(c.cycleActive?telemetryFlagCycle:0);
    if(c.tempoValid){f.validFields|=telemetryTempo;f.bpm=c.bpm;}
    if(c.ppqValid){f.validFields|=telemetryPpq;f.ppq=c.ppq;}
    if(have){
        // Modules may supply only effect-owned fields, never host context flags.
        const auto fields=metrics.validFields & (legacyTelemetryFields|telemetryGainReduction|telemetryEffectiveRate|telemetryEffectiveDelay|telemetrySync|telemetryProtection|telemetrySidechain|telemetryTail|telemetryDuck);
        f.validFields|=fields;f.inputPeak=metrics.inputPeak;f.outputPeak=metrics.outputPeak;
        f.gainReductionDb=metrics.gainReductionDb;f.effectiveRateHz=metrics.effectiveRateHz;f.effectiveDelayMs=metrics.effectiveDelayMs;f.tailEnergy=metrics.tailEnergy;f.duckDb=metrics.duckDb;
        if((fields&telemetryInvalidInput) && metrics.invalidInput)f.flags|=telemetryFlagInvalidInput;
        if(fields&telemetrySync)f.flags|=metrics.flags&telemetryFlagSyncUnavailable;
        if(fields&telemetryProtection)f.flags|=metrics.flags&telemetryFlagProtection;
        if(fields&telemetrySidechain)f.flags|=metrics.flags&(telemetryFlagSidechainMissing|telemetryFlagSidechainSilent);
    }
    if(f.valid())telemetryTransport.send(f);
}
tresult PLUGIN_API Processor::terminate() {
    if(stateTimer){stateTimer->stop();stateTimer=nullptr;}
    analysisTransport.stop();analysisRequested.store(false);
    telemetryTransport.disconnect(getPeer());telemetrySession.store(0,std::memory_order_release);
    layoutSession=0;return AudioEffect::terminate();
}
uint32 PLUGIN_API Processor::getTailSamples() {
    if(!engine)return kNoTail;
    auto tail=engine->tailSamples();
    return tail.kind==TailKind::infinite?kInfiniteTail:tail.kind==TailKind::finite?tail.samples:kNoTail;
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    std::lock_guard<std::mutex> lock(stateCalls);
    SoundState state;if(!activePublication.read(state))return kResultFalse;
    if(latestRestore.load(std::memory_order_acquire)>state.revision) {
        if(!pendingPublication.read(state))return kResultFalse;
    }
    return writeSoundState(stream,state,registry)?kResultOk:kResultFalse;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    std::lock_guard<std::mutex> lock(stateCalls);
    SoundState state;if(!readSoundState(stream,identity().processor,registry,state))return kResultFalse;
    state.revision=++nextRevision;
    // Complete restores coalesce to the latest snapshot while stopped. Audio
    // takes a bounded read at the next block boundary; no queue saturation.
    pendingPublication.publish(state);latestRestore.store(state.revision,std::memory_order_release);
    return kResultOk;
}
}
