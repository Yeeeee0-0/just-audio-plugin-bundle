#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "plugins/limiter/Parameters.hpp"
#include <iostream>
#include <functional>
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
class Handler final:public FObject,public IComponentHandler,public IComponentHandler2 {
public:
    unsigned starts=0,ends=0,groups=0,finishes=0,dirty=0;
    std::vector<std::pair<ParamID,double>> edits;
    std::function<void(ParamID)> onBegin;
    std::function<void(ParamID,double)> onPerform;
    std::array<bool,just::maxParameters> active{};
    tresult PLUGIN_API beginEdit(ParamID id) override{check(id<active.size() && !active[id],"nonoverlapping host begin");active[id]=true;++starts;if(onBegin)onBegin(id);return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v) override{check(id<active.size() && active[id] && std::isfinite(v),"perform within host gesture");edits.emplace_back(id,v);if(onPerform)onPerform(id,v);return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{check(id<active.size() && active[id],"balanced host end");active[id]=false;++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    tresult PLUGIN_API setDirty(TBool value) override{dirty+=value;return kResultOk;}
    tresult PLUGIN_API requestOpenEditor(FIDString) override{return kResultOk;}
    tresult PLUGIN_API startGroupEdit() override{++groups;return kResultOk;}
    tresult PLUGIN_API finishGroupEdit() override{++finishes;return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
        DEF_INTERFACE(IComponentHandler2)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
int main(){
    HostApplication host;auto p=owned(new just::Processor);auto c=owned(new just::Controller);auto handler=owned(new Handler);
    check(p->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"initialize");c->setComponentHandler(handler);
    check(p->connect(c)==kResultOk && c->connect(p)==kResultOk,"connect");ProcessSetup setup{kRealtime,kSample64,128,48000};check(p->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk,"activate");
    std::array<double,128> input{},output{};input.fill(.5);double* ins[]={input.data(),input.data()},*outs[]={output.data(),output.data()};AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers64=ins;out.channelBuffers64=outs;
    ProcessData d{};d.numSamples=128;d.symbolicSampleSize=kSample64;d.numInputs=d.numOutputs=1;d.inputs=&in;d.outputs=&out;
    auto process=[&](IParameterChanges* changes=nullptr){d.inputParameterChanges=changes;check(p->process(d)==kResultOk,"process preset block");d.inputParameterChanges=nullptr;};
    auto ack=[&](){p->onTimer(nullptr);};auto registry=just::moduleDefinition().parameters;
    just::SoundState before,desired,actual;check(c->readCompleteSoundState(before),"complete before");desired=before;desired.targets[1]=.25;desired.seed=before.seed+123;
    check(c->requestApplySoundState(desired),"request full state");check(!c->requestApplySoundState(before),"one request in flight");ack();check(c->readPresetTransaction()==just::PresetTransactionStatus::pending && handler->edits.empty(),"no host edits before audio applied");
    process();ack();check(c->readCompleteSoundState(actual) && just::sameSoundState(actual,desired,registry),"complete desired adopted");check(handler->starts==1 && handler->ends==1 && handler->edits.size()==1 && handler->edits[0].second==.25 && handler->groups==1 && handler->finishes==1 && handler->dirty==1,"balanced grouped actual host notification");
    check(c->canUndoLastPreset() && c->undoLastPreset(),"undo request");process();ack();check(c->readCompleteSoundState(actual) && just::sameSoundState(actual,before,registry),"full undo restores seed config targets");
    const double unchanged=c->getParamNormalized(1);check(c->requestApplySoundState(desired),"request before identical host write");process();c->setParamNormalized(1,unchanged);handler->edits.clear();ack();
    check(c->getParamNormalized(1)==unchanged && handler->edits.empty() && !c->canUndoLastPreset(),"explicit identical old host value still supersedes pending ACK");
    check(c->requestApplySoundState(before),"request for host echo fixture");process();ack();
    handler->onPerform=[&](ParamID id,double v){c->setParamNormalized(id,v);};check(c->requestApplySoundState(desired),"request with synchronous host echo");process();ack();
    check(c->canUndoLastPreset(),"identical echo of own performEdit keeps valid Undo");handler->onPerform={};
    handler->onBegin=[&](ParamID id){c->setParamNormalized(id,.37);};handler->edits.clear();check(c->requestApplySoundState(before),"request before reentrant host begin");process();ack();handler->onBegin={};
    check(c->getParamNormalized(1)==.37 && handler->edits.empty() && !c->canUndoLastPreset(),"reentrant begin automation stays newer and gesture closes without stale perform");
    // A newer controller automation value arriving before an old applied ACK must win.
    check(c->requestApplySoundState(desired),"request before controller race");process();c->setParamNormalized(1,.61);handler->edits.clear();ack();
    check(c->getParamNormalized(1)==.61,"late applied ACK preserves newer controller automation");
    check(handler->edits.empty() && !c->canUndoLastPreset(),"late ACK cannot echo stale preset back into host automation");
    ParameterChanges automation(2);int32 qi=0,pi=0;auto* q=automation.addParameterData(1,qi);q->addPoint(0,.61,pi);process(&automation);ack();
    check(c->readCompleteSoundState(actual) && actual.targets[1]==.61 && actual.seed==desired.seed,"automation wins while full nonparameter state persists");
    // Same-block sample offsets still run after the atomic preset.
    check(c->requestApplySoundState(before),"request before audio automation race");automation.clearQueue();q=automation.addParameterData(1,qi);q->addPoint(0,.2,pi);q->addPoint(63,.7,pi);q->addPoint(127,.83,pi);process(&automation);ack();
    check(c->readCompleteSoundState(actual) && actual.targets[1]==.83 && actual.seed==before.seed && actual.configurationCount==before.configurationCount,"same-block automation overrides parameter, never mixes config/seed");
    check(!c->canUndoLastPreset(),"automation invalidates preset undo");
    // A host restore supersedes a pending UI preset transaction.
    check(c->requestApplySoundState(desired),"request before host restore");auto restore=before;restore.targets[1]=.42;restore.seed=1111;auto bytes=just::encodeState(restore,registry);MemoryStream stream;int32 written=0;stream.write(bytes.data(),bytes.size(),&written);stream.seek(0,IBStream::kIBSeekSet,nullptr);
    check(p->setState(&stream)==kResultOk,"host restore staged");process();ack();check(c->readPresetTransaction()==just::PresetTransactionStatus::rejected && c->readCompleteSoundState(actual) && just::sameSoundState(actual,restore,registry),"restore wins and preset is rejected without mixed state");
    auto bad=desired;bad.configurationCount=1;bad.configurations[0]={17,.125};check(!c->requestApplySoundState(bad),"module without config validator rejects unapproved configuration changes");bad=desired;bad.plugin.words[0]^=1;check(!c->requestApplySoundState(bad),"wrong UID rejected");bad=desired;bad.configurationCount=65;check(!c->requestApplySoundState(bad),"configuration bound rejects");
    check(handler->starts==handler->ends && handler->groups==handler->finishes,"all host notifications balanced");
    // Output is a new independent host ID, with sample-offset automation after
    // the limiter. Check actual Processor samples, not only an engine setter.
    auto fresh=just::initialState(before.plugin,registry);
    check(c->requestApplySoundState(fresh),"restore new transparent defaults");process();ack();
    check(p->setActive(false)==kResultOk&&p->setupProcessing(setup)==kResultOk&&p->setActive(true)==kResultOk,"fresh delay/envelope for independent output probe");
    input.fill(.5);for(unsigned n=0;n<10;++n)process();
    automation.clearQueue();q=automation.addParameterData(just::limiter::output,qi);
    q->addPoint(17,.5,pi);q->addPoint(63,0,pi);q->addPoint(127,1,pi);process(&automation);ack();
    double gainDb=0;
    // The common continuous-automation contract interpolates from offset -1
    // to the named points. The engine then applies its 5ms dB smoother.
    for(unsigned i=0;i<128;++i){const double normalized=i<=17?1-.5*double(i+1)/18:i<=63?.5-.5*double(i-17)/46:double(i-63)/64;
        gainDb+=(-36+36*normalized-gainDb)/240;
        check(std::abs(output[i]-.5*std::pow(10.,gainDb/20))<1e-14,"Output ID9 exact host interpolation and smoothing");}
    check(c->readCompleteSoundState(actual)&&actual.targets[just::limiter::output]==1&&actual.targets[just::limiter::ceiling]==1,"post gain state does not move Ceiling");
    // This verifies the SDK edit path; it is not OS/native interaction evidence.
    ParameterInfo versionInfo{};check(c->getParameterInfo(10,versionInfo)==kResultOk && versionInfo.id==just::limiter::algorithmVersion && !(versionInfo.flags&ParameterInfo::kCanAutomate),"ID10 exposed and deliberately nonautomatable");
    just::VstEditSink editSink(*c);auto unchangedTargets=actual;
    for(double version:{0.,1.}){
        const auto starts=handler->starts,ends=handler->ends;handler->edits.clear();
        check(editSink.beginEdit(just::limiter::algorithmVersion),"algorithm edit begins");check(editSink.performEdit(just::limiter::algorithmVersion,version),"algorithm edit reaches actual controller");editSink.endEdit(just::limiter::algorithmVersion);
        check(handler->starts==starts+1&&handler->ends==ends+1&&handler->edits.size()==1&&handler->edits[0].first==10&&handler->edits[0].second==version,"one balanced ID10 host gesture only");
        automation.clearQueue();q=automation.addParameterData(just::limiter::algorithmVersion,qi);q->addPoint(0,version,pi);process(&automation);ack();for(unsigned n=0;n<8;++n)process();
        MemoryStream saved;check(p->getState(&saved)==kResultOk,"save actual processor version");just::SoundState sound;check(just::decodeState(reinterpret_cast<const std::uint8_t*>(saved.getData()),saved.getSize(),before.plugin,registry,sound)==just::StateResult::ok,"decode actual processor sound");check(sound.targets[10]==version&&c->getParamNormalized(10)==version,"processor/controller version agree");for(unsigned id=0;id<10;++id)check(sound.targets[id]==unchangedTargets.targets[id],"algorithm edit preserves each old target");
    }
    p->setActive(false);p->disconnect(c);c->disconnect(p);c->setComponentHandler(nullptr);c->terminate();p->terminate();std::cout<<"PASS "<<checks<<" actual-module complete-state/undo/host/automation conflicts (factory catalog absent)\n";
}
