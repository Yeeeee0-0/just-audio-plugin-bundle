#include "Controller.hpp"
#include "StateStreams.hpp"
#include "LayoutMessages.hpp"
#include "AnalysisTransport.hpp"
#include "PresetMessages.hpp"
#include "../ui/Editor.hpp"
#include "base/source/fstreamer.h"
#include <chrono>
namespace just {
using namespace Steinberg;
using namespace Steinberg::Vst;
#include "PresetController.inc"
#include "AuditionController.inc"
Controller::Controller(){
    const auto& module=moduleDefinition();
    if(module.defaultEditorWidth)viewState.width=module.defaultEditorWidth;
    if(module.defaultEditorHeight)viewState.height=module.defaultEditorHeight;
    module.editorSizeLimits().constrain(viewState.width,viewState.height);
}
tresult PLUGIN_API Controller::connect(IConnectionPoint* other) {
    auto result=EditController::connect(other);if(result!=kResultOk)return result;
    static std::atomic<std::uint64_t> nextSession{0};auto session=++nextSession;
    {std::lock_guard<std::mutex> lock(runtimeWrites);layoutConnected=true;layoutSession=session;lastLayoutSequence=0;layoutPublication.publish({});telemetryContext=0;lastTelemetrySequence=0;telemetryPublication.publish({});}
    // Handles either legal host connection order. This request has no audio effect.
    auto message=owned(allocateMessage());
    if(writeLayoutRequest(message.get(),pluginIdentities[JUST_PLUGIN_INDEX].processor,session))sendMessage(message);
    sendAnalysisSubscription();
    queryCompleteState();
    return result;
}
tresult PLUGIN_API Controller::disconnect(IConnectionPoint* other) {
    cancelAudition();audition={};
    hasComplete=undoValid=false;pendingPreset=0;presetStatus=PresetTransactionStatus::unavailable;
    analysis.clear();
    if(other==getPeer()) {std::lock_guard<std::mutex> lock(runtimeWrites);layoutConnected=false;layoutSession=0;layoutPublication.publish({});telemetryContext=0;telemetryPublication.publish({});}
    return EditController::disconnect(other);
}
tresult PLUGIN_API Controller::terminate() {
    cancelAudition();
    analysis.stop();analysis.clear();
    {std::lock_guard<std::mutex> lock(runtimeWrites);layoutConnected=false;layoutSession=0;layoutPublication.publish({});telemetryContext=0;telemetryPublication.publish({});}
    return EditController::terminate();
}
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),auditionStatusID)){
        if(!messageSession(message,layoutSession))return kResultFalse;const void* data=nullptr;uint32 size=0;
        if(message->getAttributes()->getBinary("Status",data,size)!=kResultOk || !data || size!=sizeof(AuditionStatus))return kInvalidArgument;
        AuditionStatus s;std::memcpy(&s,data,sizeof(s));if(s.token!=audition.token || s.bandAnchor!=audition.bandAnchor || std::uint32_t(s.phase)>4)return kResultFalse;
        if(audition.phase!=AuditionPhase::ended)audition=s;return kResultOk;
    }
    if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),presetStateID))return receivePresetState(message);
    if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),analysisMessageID)) {
        auto* a=message->getAttributes();const void* bytes=nullptr;uint32 size=0;int64 head=0,epoch=0;
        if(!a || a->getBinary("Data",bytes,size)!=kResultOk || !bytes || size!=sizeof(AnalysisMessage) ||
            a->getInt("SourceSample",head)!=kResultOk || head<0 || a->getInt("SourceEpoch",epoch)!=kResultOk || epoch<=0)return kInvalidArgument;
        AnalysisMessage data;std::memcpy(&data,bytes,sizeof(data));auto& h=data.kind==AnalysisKind::envelope?data.window.header:data.sampleFrame.header;
        if(!layoutConnected || !editorCount || !data.valid() || !(data.plugin==pluginIdentities[JUST_PLUGIN_INDEX].processor) || h.session!=layoutSession)return kResultFalse;
        analysis.accept(data,head,epoch);return kResultOk;
    }
    if(message && message->getMessageID() && std::strcmp(message->getMessageID(),layoutMessageID)==0) {
        BusLayoutSnapshot s;
        if(!readLayoutMessage(message,pluginIdentities[JUST_PLUGIN_INDEX].processor,s))return kInvalidArgument;
        std::lock_guard<std::mutex> lock(runtimeWrites);
        if(!layoutConnected || s.session!=layoutSession || s.sequence<=lastLayoutSequence)return kResultFalse;
        lastLayoutSequence=s.sequence;layoutPublication.publish(s);return kResultOk;
    }
    if(!message || !message->getMessageID())return kInvalidArgument;
    // SDK fallback decoder is intentionally preceded by strict size/context checks.
    auto* attrs=message->getAttributes();const char* id=message->getMessageID();
    if(!std::strcmp(id,"DataExchange") || !std::strcmp(id,"DataExchangeQueueOpened") || !std::strcmp(id,"DataExchangeQueueClosed")) {
        int64 context=0;if(!attrs || attrs->getInt("UserContextID",context)!=kResultOk || context<=0 || context>std::numeric_limits<uint32>::max())return kInvalidArgument;
        if(!std::strcmp(id,"DataExchangeQueueOpened")){int64 size=0;if(attrs->getInt("BlockSize",size)!=kResultOk || size!=sizeof(RuntimeTelemetrySnapshot))return kInvalidArgument;}
        if(!std::strcmp(id,"DataExchange")){const void* data=nullptr;uint32 size=0;if(attrs->getBinary("Data",data,size)!=kResultOk || !data || size!=sizeof(RuntimeTelemetrySnapshot))return kInvalidArgument;}
        return telemetryReceiver.onMessage(message)?kResultOk:kResultFalse;
    }
    return EditController::notify(message);
}
void Controller::sendAnalysisSubscription(){
    if(!getPeer() || !layoutSession)return;
    auto m=owned(allocateMessage());if(!m)return;m->setMessageID(analysisSubscriptionID);auto* a=m->getAttributes();if(!a)return;
    a->setInt("Session",layoutSession);a->setInt("Enabled",editorCount?1:0);sendMessage(m);
}
void Controller::editorAttached(){if(!editorCount++){analysis.clear();analysis.start();sendAnalysisSubscription();}}
void Controller::editorRemoved(){cancelAudition();if(editorCount && !--editorCount){sendAnalysisSubscription();analysis.stop();analysis.clear();}}
AnalysisAvailability Controller::readAnalysis(AnalysisCursor& cursor,AnalysisBatch& out) const noexcept {
    BusLayoutSnapshot l;if(!editorCount || !readBusLayout(l) || !l.active){out={};return AnalysisAvailability::unavailable;}return analysis.read(l.session,cursor,out);
}
AnalysisAvailability Controller::readSpectrum(SpectrumSnapshot& out) const noexcept {
    BusLayoutSnapshot l;if(!editorCount || !readBusLayout(l) || !l.active){out={};return AnalysisAvailability::unavailable;}return analysis.readSpectrum(l.session,out);
}
AnalysisAvailability Controller::readSamples(SampleFrame& out) const noexcept {
    BusLayoutSnapshot l;if(!editorCount || !readBusLayout(l) || !l.active){out={};return AnalysisAvailability::unavailable;}return analysis.readSamples(l.session,out);
}
AnalysisAvailability Controller::readExtendedSpectrum(ExtendedSpectrumSnapshot& out) const noexcept {
    BusLayoutSnapshot l;if(!hasExtendedSpectrum || !editorCount || !readBusLayout(l) || !l.active){out={};return AnalysisAvailability::unavailable;}
    return analysis.readExtendedSpectrum(l.session,out);
}
bool Controller::readAnalyzerPreferences(AnalyzerPreferences& out) const noexcept {
    out={};if(!hasExtendedSpectrum)return false;out=analyzerPreferences;return true;
}
bool Controller::writeAnalyzerPreferences(const AnalyzerPreferences& value) noexcept {
    if(!hasExtendedSpectrum || !value.valid() || !analysis.configureExtendedSpectrum(value.fftSize,value.releaseDbPerSecond()))return false;
    if(analyzerPreferences==value)return true;
    analyzerPreferences=value;FUnknownPtr<IComponentHandler2> handler(componentHandler);if(handler)handler->setDirty(true);return true;
}
bool Controller::readBusLayout(BusLayoutSnapshot& out) const noexcept {
    out={};BusLayoutSnapshot staged;
    if(!layoutPublication.read(staged) || !staged.valid() || !(staged.validFields&layoutBuses))return false;
    out=staged;return true;
}
static std::uint64_t receiveTime() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void PLUGIN_API Controller::queueOpened(DataExchangeUserContextID context,uint32 bytes,TBool& background) {
    background=false;std::lock_guard<std::mutex> lock(runtimeWrites);
    if(!layoutConnected || !context || context<=lastTelemetryContext || bytes!=sizeof(RuntimeTelemetrySnapshot))return;
    lastTelemetryContext=telemetryContext=context;lastTelemetrySequence=0;telemetryPublication.publish({});
}
void PLUGIN_API Controller::queueClosed(DataExchangeUserContextID context) {
    std::lock_guard<std::mutex> lock(runtimeWrites);
    if(context!=telemetryContext)return;telemetryContext=0;telemetryPublication.publish({});
}
void PLUGIN_API Controller::onDataExchangeBlocksReceived(DataExchangeUserContextID context,uint32 count,DataExchangeBlock* blocks,TBool background) {
    if(background || !blocks || count>4)return; // requested main dispatch; hard bound
    std::lock_guard<std::mutex> lock(runtimeWrites);
    if(!layoutConnected || !context || context!=telemetryContext)return;
    for(uint32 i=0;i<count;++i){
        if(!blocks[i].data || blocks[i].size!=sizeof(RuntimeTelemetrySnapshot))continue;
        RuntimeTelemetrySnapshot f;std::memcpy(&f,blocks[i].data,sizeof(f));
        if(!f.valid() || !(f.plugin==pluginIdentities[JUST_PLUGIN_INDEX].processor) || f.session!=layoutSession || f.queueContext!=context || f.sequence<=lastTelemetrySequence)continue;
        const auto now=receiveTime();if(now<f.sourceNanoseconds || now-f.sourceNanoseconds>telemetryStaleNanoseconds)continue;
        lastTelemetrySequence=f.sequence;telemetryPublication.publish({f,now});
    }
}
TelemetryAvailability Controller::readRuntimeTelemetry(RuntimeTelemetrySnapshot& out) const noexcept {
    out={};ReceivedTelemetry r;BusLayoutSnapshot layout;
    if(!readBusLayout(layout) || !layout.active || !(layout.validFields&layoutSampleRate) || !telemetryPublication.read(r) || !r.receivedNanoseconds || !r.frame.valid() || r.frame.session!=layout.session)return TelemetryAvailability::unavailable;
    const auto now=receiveTime();out=r.frame;
    if(now<r.receivedNanoseconds || now-r.receivedNanoseconds>telemetryStaleNanoseconds || now<r.frame.sourceNanoseconds || now-r.frame.sourceNanoseconds>telemetryStaleNanoseconds){out.validFields=0;out.flags=0;return TelemetryAvailability::stale;}
    return TelemetryAvailability::fresh;
}
static void asciiToString(const char* source,String128 target) {
    int i=0;for(;source[i] && i<127;++i)target[i]=static_cast<TChar>(static_cast<unsigned char>(source[i]));
    target[i]=0;
}
class SpecParameter final:public Parameter {
    ParameterSpec spec;
public:
    explicit SpecParameter(ParameterSpec p):spec(p) {
        info.id=p.id;asciiToString(p.title,info.title);asciiToString(p.title,info.shortTitle);asciiToString(p.unit,info.units);
        info.stepCount=p.stepCount;info.defaultNormalizedValue=p.toNormalized(p.initial);
        info.flags=(p.automatable?ParameterInfo::kCanAutomate:0)|(p.id==bypassParamID?ParameterInfo::kIsBypass:0);
        info.unitId=kRootUnitId;valueNormalized=info.defaultNormalizedValue;
    }
    void toString(ParamValue v,String128 text) const override{char buffer[128];spec.format(v,buffer,sizeof(buffer));asciiToString(buffer,text);}
    bool fromString(const TChar* text,ParamValue& v) const override {
        if(!text)return false;char buffer[128];std::size_t i=0;
        for(;text[i] && i<127;++i){if(text[i]>127)return false;buffer[i]=static_cast<char>(text[i]);}
        if(text[i])return false;buffer[i]=0;return spec.parse(buffer,v);
    }
    ParamValue toPlain(ParamValue v) const override{return spec.toPhysical(v);}
    ParamValue toNormalized(ParamValue v) const override{return spec.toNormalized(v);}
};
tresult PLUGIN_API Controller::initialize(FUnknown* host) {
    auto result=EditController::initialize(host);if(result!=kResultOk)return result;
    if(!moduleDefinition().valid())return kResultFalse;
    for(std::size_t i=0;i<registry.count;++i)parameters.addParameter(new SpecParameter(registry.specs[i]));
    return kResultOk;
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    SoundState state;
    if(!readSoundState(stream,pluginIdentities[JUST_PLUGIN_INDEX].processor,registry,state))return kResultFalse;
    hasComplete=false;undoValid=false;
    for(unsigned i=0;i<registry.count;++i)++targetRevisions[i];
    for(std::size_t i=0;i<registry.count;++i)EditController::setParamNormalized(registry.specs[i].id,state.targets[i]);
    return kResultOk;
}
tresult PLUGIN_API Controller::setParamNormalized(Steinberg::Vst::ParamID id,ParamValue value) {
    if(registry.index(id)==registry.count || !std::isfinite(value) || value<0 || value>1)return kInvalidArgument;
    // An explicit host write is newer even when it repeats the controller's
    // old value while an audio-applied preset ACK is still in flight. Ignore
    // only the identical synchronous echo of our own performEdit notification.
    if(!applyingPresetNotification || value!=getParamNormalized(id)){undoValid=false;++targetRevisions[registry.index(id)];}
    return EditController::setParamNormalized(id,value);
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    return name && std::strcmp(name,ViewType::kEditor)==0?new Editor(this):nullptr;
}
tresult PLUGIN_API Controller::getState(IBStream* stream) {
    if(!stream)return kInvalidArgument;
    IBStreamer s(stream,kLittleEndian);
    if(!(s.writeInt32(hasExtendedSpectrum?3:2) && s.writeInt32(viewState.width) && s.writeInt32(viewState.height) &&
        s.writeDouble(viewState.scale) && s.writeBool(viewState.advanced) &&
        s.writeInt32(static_cast<int32>(viewState.language)) && s.writeBool(viewState.backgroundEnabled) &&
        s.writeBool(viewState.reduceMotion) && s.writeBool(viewState.lowPerformance) && s.writeInt32(viewState.backgroundFps) &&
        s.writeDouble(viewState.renderScale)))return kResultFalse;
    if(!hasExtendedSpectrum)return kResultOk;
    return s.writeInt32(analyzerPreferenceTag) && s.writeInt32(analyzerPreferenceVersion) && s.writeInt32(analyzerPreferenceBytes) &&
        s.writeInt32(analyzerPreferences.rangeDb) && s.writeDouble(analyzerPreferences.tiltDbPerOctave) && s.writeInt32(analyzerPreferences.fftSize) &&
        s.writeInt32(static_cast<int32>(analyzerPreferences.response)) && s.writeInt32(static_cast<int32>(analyzerPreferences.source))?kResultOk:kResultFalse;
}
tresult PLUGIN_API Controller::setState(IBStream* stream) {
    if(!stream)return kInvalidArgument;
    IBStreamer s(stream,kLittleEndian);int32 version;EditorViewState staged;AnalyzerPreferences stagedAnalyzer;
    if(!s.readInt32(version) || (version!=1 && version!=2 && !(hasExtendedSpectrum && version==3)) || !s.readInt32(staged.width) || !s.readInt32(staged.height) ||
       !s.readDouble(staged.scale) || !s.readBool(staged.advanced) ||
       staged.width<EditorSizeLimits::defaultMinimumWidth || staged.width>EditorSizeLimits::maximumWidth ||
       staged.height<EditorSizeLimits::defaultMinimumHeight || staged.height>EditorSizeLimits::maximumHeight ||
       !std::isfinite(staged.scale) || staged.scale<0.75 || staged.scale>2)return kResultFalse;
    if(version>=2){int32 language=0,fps=0;
        if(!s.readInt32(language) || language<0 || language>1 || !s.readBool(staged.backgroundEnabled) ||
           !s.readBool(staged.reduceMotion) || !s.readBool(staged.lowPerformance) || !s.readInt32(fps) || fps<1 || fps>30 ||
           !s.readDouble(staged.renderScale) || !std::isfinite(staged.renderScale) || staged.renderScale<.75 || staged.renderScale>1.5)return kResultFalse;
        staged.language=static_cast<UiLanguage>(language);staged.backgroundFps=fps;
    }
    if(version==3){
        int32 tag=0,schema=0,bytes=0,range=0,fft=0,response=0,source=0;
        if(!s.readInt32(tag) || tag!=analyzerPreferenceTag || !s.readInt32(schema) || schema!=analyzerPreferenceVersion ||
            !s.readInt32(bytes) || bytes!=analyzerPreferenceBytes || !s.readInt32(range) || !s.readDouble(stagedAnalyzer.tiltDbPerOctave) ||
            !s.readInt32(fft) || !s.readInt32(response) || !s.readInt32(source))return kResultFalse;
        stagedAnalyzer.rangeDb=static_cast<std::uint32_t>(range);stagedAnalyzer.fftSize=static_cast<std::uint32_t>(fft);
        stagedAnalyzer.response=static_cast<AnalyzerResponse>(response);stagedAnalyzer.source=static_cast<AnalyzerSource>(source);
        if(!stagedAnalyzer.valid())return kResultFalse;
    }
    auto limits=moduleDefinition().editorSizeLimits();
    if(version>=2){limits.minimumWidth=std::max(EditorSizeLimits::defaultMinimumWidth,int(std::ceil(limits.minimumWidth*staged.renderScale)));limits.minimumHeight=std::max(EditorSizeLimits::defaultMinimumHeight,int(std::ceil(limits.minimumHeight*staged.renderScale)));}
    limits.constrain(staged.width,staged.height);
    // Host UI restore must not reset an attached editor's runtime pause generation.
    staged.visualsPaused=viewState.visualsPaused;staged.visualResumeGeneration=viewState.visualResumeGeneration;
    if(hasExtendedSpectrum){
        if(!analysis.configureExtendedSpectrum(stagedAnalyzer.fftSize,stagedAnalyzer.releaseDbPerSecond()))return kResultFalse;
        analyzerPreferences=stagedAnalyzer; // host restore is not a new user edit
    }
    viewState=staged;return kResultOk;
}
}
