#include "TestModule.hpp"
#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <windows.h>
#endif
using namespace Steinberg;using namespace Steinberg::Vst;
static thread_local bool inProcess=false;
static unsigned allocations=0,releases=0,checks=0;
void* operator new(std::size_t n){if(inProcess)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(inProcess && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void check(bool b,const char* s){++checks;if(!b){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
class ExchangeHost final:public HostApplication,public IDataExchangeHandler {
    struct Slot {alignas(32) std::array<std::uint8_t,sizeof(just::RuntimeTelemetrySnapshot)> data{};std::atomic<unsigned> state{0};};
    std::array<Slot,4> slots;
    DataExchangeUserContextID context=0;IDataExchangeReceiver* receiver=nullptr;
    bool open=false;
public:
    unsigned locks=0,drops=0,opens=0,closes=0;
    void setReceiver(IDataExchangeReceiver* r){receiver=r;}
    tresult PLUGIN_API queryInterface(const TUID iid,void** object) override {
        if(!object)return kInvalidArgument;
        if(FUnknownPrivate::iidEqual(iid,IDataExchangeHandler::iid)){*object=static_cast<IDataExchangeHandler*>(this);addRef();return kResultOk;}
        return HostApplication::queryInterface(iid,object);
    }
    uint32 PLUGIN_API addRef() override{return HostApplication::addRef();}
    uint32 PLUGIN_API release() override{return HostApplication::release();}
    tresult PLUGIN_API openQueue(IAudioProcessor*,uint32 size,uint32 count,uint32 alignment,DataExchangeUserContextID c,DataExchangeQueueID* id) override {
        check(!inProcess && !open && receiver && size==sizeof(just::RuntimeTelemetrySnapshot) && count==4 && alignment==32,"direct queue prepared off audio thread");
        context=c;*id=1;open=true;++opens;TBool background=true;receiver->queueOpened(context,size,background);check(!background,"receiver requests main-thread delivery");return kResultOk;
    }
    tresult PLUGIN_API closeQueue(DataExchangeQueueID id) override {
        check(!inProcess && open && id==1,"direct close off audio thread");open=false;++closes;receiver->queueClosed(context);for(auto& s:slots)s.state=0;return kResultOk;
    }
    tresult PLUGIN_API lockBlock(DataExchangeQueueID id,DataExchangeBlock* b) override {
        if(!open || id!=1 || !inProcess)return kInvalidArgument;
        ++locks;for(uint32 i=0;i<4;++i){unsigned free=0;if(slots[i].state.compare_exchange_strong(free,1)){*b={slots[i].data.data(),sizeof(just::RuntimeTelemetrySnapshot),i};return kResultOk;}}
        ++drops;return kOutOfMemory;
    }
    tresult PLUGIN_API freeBlock(DataExchangeQueueID id,DataExchangeBlockID b,TBool send) override {
        if(!open || id!=1 || b>=4 || !inProcess || slots[b].state!=1)return kInvalidArgument;
        slots[b].state.store(send?2:0,std::memory_order_release);return kResultOk;
    }
    void pump(){
        std::array<DataExchangeBlock,4> blocks{};std::array<unsigned,4> indexes{};unsigned count=0;
        for(unsigned i=0;i<4;++i)if(slots[i].state.load(std::memory_order_acquire)==2){indexes[count]=i;blocks[count++]={slots[i].data.data(),sizeof(just::RuntimeTelemetrySnapshot),i};}
        if(count)receiver->onDataExchangeBlocksReceived(context,count,blocks.data(),false);
        for(unsigned i=0;i<count;++i)slots[indexes[i]].state.store(0,std::memory_order_release);
    }
};
static void pumpLegacy(){
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,.08,false);
#else
    auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(80);
    while(std::chrono::steady_clock::now()<end){MSG m;while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
#endif
}
static std::vector<std::uint8_t> state(just::Processor& p){MemoryStream s;check(p.getState(&s)==kResultOk,"complete state saved");auto* b=reinterpret_cast<std::uint8_t*>(s.getData());return {b,b+s.getSize()};}
static void exercise(HostApplication& host,ExchangeHost* direct){
    auto p=owned(new just::Processor);auto c=owned(new just::Controller);
    FUnknownPtr<IDataExchangeReceiver> receiver(static_cast<IEditController*>(c.get()));check(bool(receiver),"controller exposes IDataExchangeReceiver");
    if(direct)direct->setReceiver(receiver);
    check(p->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"initialize runtime");
    check(p->connect(c)==kResultOk && c->connect(p)==kResultOk,"connect runtime");
    ProcessSetup setup{kRealtime,kSample64,2048,48000};check(p->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk,"prepare runtime");
    auto initial=state(*p);
    std::array<double,2048> inL{},inR{},outL{},outR{};inL.fill(.25);inR.fill(-.5);
    double* inputs[]={inL.data(),inR.data()};double* outputs[]={outL.data(),outR.data()};
    AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers64=inputs;out.channelBuffers64=outputs;
    ProcessContext ctx{};ctx.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;ctx.tempo=143;ctx.projectTimeMusic=42;
    ProcessData data{};data.numSamples=2048;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&ctx;
    auto render=[&](){inProcess=true;auto r=p->process(data);inProcess=false;check(r==kResultOk && allocations==0 && releases==0,"telemetry process has zero C++ new/delete");check(inL==outL && inR==outR,"telemetry cannot change audio");ctx.projectTimeSamples+=2048;};
    just::RuntimeTelemetrySnapshot f;check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::unavailable,"no invented meter before delivery");
    for(unsigned i=0;i<24;++i)render();
    if(direct){check(direct->locks==24 && direct->drops==20,"four bounded direct blocks saturate and drop");direct->pump();}else pumpLegacy();
    check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::fresh,"actual delivered telemetry fresh");
    check(f.bpm==143 && f.ppq==42 && (f.validFields&just::telemetryTempo) && (f.flags&just::telemetryFlagPlaying),"actual context BPM/PPQ/transport");
    check(f.inputPeak==.5 && f.outputPeak==.5 && !(f.validFields&just::telemetryGainReduction) && !(f.validFields&just::telemetrySync),"legacy fields valid, unsupported effect fields unknown");
    auto saved=f;
    auto malformed=f;malformed.sequence+=100;malformed.version=99;DataExchangeBlock block{&malformed,sizeof(malformed),0};
    c->onDataExchangeBlocksReceived(f.queueContext,1,&block,false);
    malformed=f;malformed.sequence+=100;malformed.gainReductionDb=std::numeric_limits<double>::quiet_NaN();c->onDataExchangeBlocksReceived(f.queueContext,1,&block,false);
    malformed=f;malformed.sequence+=100;malformed.plugin.words[0]^=1;c->onDataExchangeBlocksReceived(f.queueContext,1,&block,false);
    malformed=f;malformed.sequence+=100;malformed.session+=1;c->onDataExchangeBlocksReceived(f.queueContext,1,&block,false);
    malformed=f;malformed.sequence+=100;malformed.validFields|=1ull<<60;c->onDataExchangeBlocksReceived(f.queueContext,1,&block,false);
    malformed=f;malformed.sequence+=100;c->onDataExchangeBlocksReceived(f.queueContext,1,&block,true);
    check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::fresh && f.sequence==saved.sequence,"wrong version/UID/session/mask/nonfinite/background frames transactional");
    // Drain any legacy backlog, then stop delivery long enough to prove expiry.
    if(!direct)pumpLegacy();std::this_thread::sleep_for(std::chrono::milliseconds(550));
    check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::stale && !f.validFields && !f.flags,"stale cached values have no valid fields");
    render();std::this_thread::sleep_for(std::chrono::milliseconds(600));
    if(direct)direct->pump();else pumpLegacy();
    check(c->readRuntimeTelemetry(f)!=just::TelemetryAvailability::fresh && !f.validFields,"stalled audio plus main dispatch cannot revive old scalar BPM or meters");
    just::test::trace.extendedTelemetry=true;ctx.state=0;data.processContext=nullptr;render();
    if(direct)direct->pump();else pumpLegacy();
    check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::fresh && !(f.validFields&just::telemetryTempo) && !(f.validFields&just::telemetryPpq) && !(f.validFields&just::telemetryTransport),"missing host context clears validity");
    check((f.validFields&just::telemetryGainReduction) && f.gainReductionDb==6 && f.effectiveRateHz==2.25 && (f.flags&just::telemetryFlagSidechainMissing),"optional module fields delivered only with mask");
    check(state(*p)==initial,"bridge preserves complete sound state");
    saved=f;check(p->setActive(false)==kResultOk && c->readRuntimeTelemetry(f)==just::TelemetryAvailability::unavailable,"deactivation/queue close invalidate telemetry");
    malformed=saved;malformed.sequence+=100;block.data=&malformed;c->onDataExchangeBlocksReceived(saved.queueContext,1,&block,false);check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::unavailable,"late block after close rejected");
    check(p->setActive(true)==kResultOk,"reactivate prepared queue");render();if(direct)direct->pump();else pumpLegacy();
    check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::fresh && f.queueContext!=saved.queueContext,"new activation has distinct queue context");
    c->queueClosed(saved.queueContext);check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::fresh,"old queue close cannot clear new queue");
    p->setActive(false);p->disconnect(c);c->disconnect(p);check(c->readRuntimeTelemetry(f)==just::TelemetryAvailability::unavailable,"disconnect invalidates");
    c->terminate();p->terminate();just::test::trace.extendedTelemetry=false;
}
int main(){HostApplication oldHost;exercise(oldHost,nullptr);ExchangeHost newHost;exercise(newHost,&newHost);check(newHost.opens==newHost.closes && newHost.opens==2,"direct queue lifetime balanced");std::cout<<"PASS "<<checks<<" new-host/legacy bounded telemetry checks; process C++ new/delete 0\n";}
