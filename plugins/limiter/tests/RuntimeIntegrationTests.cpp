#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "common/vst3/StateStreams.hpp"
#include "common/vst3/AnalysisTransport.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#ifdef JUST_TEST_LIMITER
#include "plugins/limiter/HistoryModel.hpp"
#include "plugins/limiter/Parameters.hpp"
#else
#include "plugins/eq/EditorModel.hpp"
#endif
#include <CoreFoundation/CoreFoundation.h>
#include <iostream>
#include <thread>
#include <chrono>
static thread_local bool inAudio=false;static unsigned allocations=0,releases=0,checks=0;
void* operator new(std::size_t n){if(inAudio)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(inAudio&&p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool b,const char* s){++checks;if(!b){std::cerr<<"FAIL "<<s<<'\n';std::exit(1);}}
static void pump(double seconds=.015){CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);}
static auto save(just::Processor& p){MemoryStream s;check(p.getState(&s)==kResultOk,"save");auto b=(std::uint8_t*)s.getData();return std::vector<std::uint8_t>(b,b+s.getSize());}
// Non-audio test transport: forwards genuine processor messages unchanged,
// with explicit delivery delays/drops to exercise each consumer independently.
class DeliveryBridge final:public FObject,public IConnectionPoint {
public:
 just::Controller* receiver=nullptr;bool dropRaw=false,holdAnalysis=false;
 unsigned rawDropped=0;std::vector<IPtr<IMessage>> held;
 tresult PLUGIN_API connect(IConnectionPoint*) override{return kResultOk;}
 tresult PLUGIN_API disconnect(IConnectionPoint*) override{return kResultOk;}
 tresult PLUGIN_API notify(IMessage* m) override {
  if(m && m->getMessageID() && !std::strcmp(m->getMessageID(),just::analysisMessageID)){
   if(holdAnalysis){held.emplace_back(m);return kResultOk;}
   const void* bytes=nullptr;uint32 size=0;
   if(dropRaw && m->getAttributes()->getBinary("Data",bytes,size)==kResultOk && size==sizeof(just::AnalysisMessage)){
    just::AnalysisMessage data;std::memcpy(&data,bytes,size);if(data.kind==just::AnalysisKind::samples){++rawDropped;return kResultOk;}
   }
  }return receiver->notify(m);
 }
 void deliverBacklog(){auto messages=std::move(held);held.clear();for(auto& m:messages)receiver->notify(m);}
 OBJ_METHODS(DeliveryBridge,FObject)
 DEFINE_INTERFACES
  DEF_INTERFACE(IConnectionPoint)
 END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};

static void exercise(double fs){
 HostApplication host;auto p=owned(new just::Processor),ref=owned(new just::Processor);auto c=owned(new just::Controller);auto bridge=owned(new DeliveryBridge);bridge->receiver=c;
 check(p->initialize(&host)==kResultOk&&ref->initialize(&host)==kResultOk&&c->initialize(&host)==kResultOk,"initialize actual module");check(p->connect(bridge)==kResultOk&&c->connect(p)==kResultOk,"connect actual P/C");
 ProcessSetup setup{kRealtime,kSample64,4096,fs};check(p->setupProcessing(setup)==kResultOk&&ref->setupProcessing(setup)==kResultOk,"prepare actual module");
 auto state=just::initialState(just::pluginIdentities[JUST_PLUGIN_INDEX].processor,just::moduleDefinition().parameters);
#ifdef JUST_TEST_LIMITER
 state.targets[just::limiter::mode]=0;state.targets[just::limiter::lookahead]=0;
 state.targets[just::limiter::ceiling]=just::limiter::parameters[just::limiter::ceiling].toNormalized(-1);
#else
 auto set=[&](std::size_t b,just::eq::Field f,double v){auto i=just::eq::index(b,f);state.targets[i]=just::eq::parameters[i].toNormalized(v);};
 set(0,just::eq::enabled,1);set(0,just::eq::frequency,fs*32/2048);set(0,just::eq::gain,6);set(0,just::eq::q,1);
 set(1,just::eq::enabled,1);set(1,just::eq::target,1);set(1,just::eq::frequency,2200);set(1,just::eq::gain,-4);
#endif
 auto encoded=just::encodeState(state,just::moduleDefinition().parameters);for(auto target:{p.get(),ref.get()}){MemoryStream s(encoded.data(),encoded.size());check(target->setState(&s)==kResultOk,"seed state");}
 MemoryStream cs(encoded.data(),encoded.size());c->setComponentState(&cs);
 check(p->setActive(true)==kResultOk&&ref->setActive(true)==kResultOk,"activate");p->setProcessing(true);ref->setProcessing(true);c->editorAttached();
 std::array<double,4096> l{},r{},a{},b{},ra{},rb{};double* ins[]={l.data(),r.data()},*out[]={a.data(),b.data()},*rout[]={ra.data(),rb.data()};AudioBusBuffers input{},output{},routput{};input.numChannels=output.numChannels=routput.numChannels=2;input.channelBuffers64=ins;output.channelBuffers64=out;routput.channelBuffers64=rout;
 ProcessData data{};data.numSamples=1024;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;
 ProcessContext pc{};pc.state=ProcessContext::kPlaying;pc.sampleRate=fs;data.processContext=&pc;
 std::uint64_t position=0;bool silence=false,pulse=false;
 auto render=[&](bool compare=true,const char* comparison="analysis/UI subscriber leaves audio bit exact"){for(int i=0;i<data.numSamples;++i){double x=std::sin(2*3.14159265358979323846*32*(position+i)/2048);
#ifdef JUST_TEST_LIMITER
 x*=2;
#else
 x*=.1;
#endif
 l[i]=pulse?(i==17?3.0:0.0):silence?0:x;r[i]=-l[i];}
  pc.projectTimeSamples=position;auto rd=data;rd.outputs=&routput;inAudio=true;auto ok=p->process(data);auto rok=ref->process(rd);inAudio=false;check(ok==kResultOk&&rok==kResultOk,"process");
  if(compare)for(int i=0;i<data.numSamples;++i)check(a[i]==ra[i]&&b[i]==rb[i],comparison);position+=data.numSamples;
 };
 for(int block=0;block<80;++block){render();if(block%4==3)pump();}pump(.05);
 just::AnalysisCursor cursor;just::AnalysisBatch batch;unsigned windows=0;double maxOutput=0,maxInput=0,maxGR=0;bool grValid=false;
 while(c->readAnalysis(cursor,batch)==just::AnalysisAvailability::fresh&&batch.count){for(unsigned i=0;i<batch.count;++i){auto& w=batch.windows[i];++windows;check(w.header.sampleRate==fs&&w.header.latencySamples==p->getLatencySamples()&&(w.header.flags&just::analysisInputAligned),"actual Fs/PDC and aligned common I/O");check(w.header.endSample>w.header.startSample,"measured source clock");maxInput=std::max(maxInput,w.channels[0].peak);maxOutput=std::max(maxOutput,w.channels[2].peak);if(w.effectFields&just::analysisReduction){grValid=true;maxGR=std::max(maxGR,w.reductionDb);}}}
 check(windows>4,"actual module windows delivered");
#ifdef JUST_TEST_LIMITER
 check(grValid&&maxGR>6,"actual limiter per-sample GR producer is valid");check(maxInput>1.9&&maxOutput<=std::pow(10.,-1./20)*(1+1e-12),"input output use same absolute scale");
 just::limiter::HistoryModel model;just::EditorServices services{};services.owner=c.get();services.readAnalysis=[](void* v,just::AnalysisCursor& cur,just::AnalysisBatch& batch){return static_cast<just::Controller*>(v)->readAnalysis(cur,batch);};model.refresh(services);check(model.current()&&model.outputHeld&&model.grHold.reductionValid,"module consumer receives real history and holds");model.resetOutput();model.resetGR();model.refresh(services);check(!model.outputHeld&&!model.grHold.reductionValid,"same sequence never reappears after UI reset");render();pump(.03);model.refresh(services);check(model.outputHeld&&model.grHold.reductionValid,"new measured sequence resumes holds");
#else
 just::SpectrumSnapshot spectrum;check(c->readSpectrum(spectrum)==just::AnalysisAvailability::fresh,"actual EQ spectrum delivered");check(std::abs(spectrum.amplitude[0][32]-.1)<.002,"anti-phase stereo input power and calibrated shared scale");check(spectrum.amplitude[1][32]>.19&&spectrum.amplitude[1][32]<.21,"actual EQ output spectrum contains measured 6dB boost");
 auto saved=save(*p);auto token=c->beginAudition(just::eq::id(0,just::eq::frequency));check(token&&c->readAuditionStatus().phase==just::AuditionPhase::pending,"Solo begin only queues pending");render(false);pump(.06);check(c->readAuditionStatus().phase==just::AuditionPhase::active,"actual EQ engine audio ACK");
 for(int n=0;n<16;++n){check(c->renewAudition(token),"live token renew");render(false);pump(.005);}check(c->readAuditionStatus().phase==just::AuditionPhase::active,"renew does not restart/expire active band");c->endAudition(token);render(false);render();pump(.05);check(c->readAuditionStatus().phase==just::AuditionPhase::ended&&save(*p)==saved,"release restores exact normal EQ and unchanged saved state");
 token=c->beginAudition(just::eq::id(0,just::eq::frequency));render(false);for(int n=0;n<int(fs*.27/data.numSamples)+2;++n)render(false);render();pump(.05);check(c->readAuditionStatus().phase==just::AuditionPhase::ended,"common audio lease expires while main thread stalls");check(!c->renewAudition(token),"expired token cannot restart audition");
 token=c->beginAudition(just::eq::id(0,just::eq::frequency));std::this_thread::sleep_for(std::chrono::milliseconds(350));render(true,"expired queued first Solo begin must leave resumed audio bit exact");pump(.06);check(c->readAuditionStatus().phase!=just::AuditionPhase::active,"first begin expired while audio was stopped cannot start on resumed audio");
 token=c->beginAudition(just::eq::id(0,just::eq::frequency));render(false);c->editorRemoved();render(false);render();pump();check(save(*p)==saved,"close safely restores audio without changing state");c->editorAttached();
#endif
 // Isolate one sub-window transient after an observed unsubscribe/restart.
 c->editorRemoved();silence=true;render();c->editorAttached();pulse=true;render();pulse=false;
 for(int n=0;n<12;++n){render();if(n%4==3)pump();}pump(.05);
 just::AnalysisCursor impulseCursor;unsigned impulseWindows=0;while(c->readAnalysis(impulseCursor,batch)==just::AnalysisAvailability::fresh&&batch.count){for(unsigned i=0;i<batch.count;++i){const auto& w=batch.windows[i];if(w.channels[0].peak==3){++impulseWindows;auto at=p->getLatencySamples()+17;check(w.header.startSample<=at&&w.header.endSample>at,"one-sample input transient follows exact PDC source time");}}}
 check(impulseWindows==1,"one-sample transient is captured exactly once despite slower UI");
 silence=true;pc.state=0;for(int block=0;block<40;++block){render();if(block%4==3)pump();}pump(.05);just::AnalysisWindow last;bool got=false;while(c->readAnalysis(cursor,batch)==just::AnalysisAvailability::fresh&&batch.count){last=batch.windows[batch.count-1];got=true;}
 check(got&&last.channels[0].peak==0&&!(last.header.flags&just::analysisPlaying)&&(last.header.flags&just::analysisTransportKnown),"real silence and host stop marked separately");
 pump(.65);check(c->readAnalysis(cursor,batch)==just::AnalysisAvailability::stale,"drained stream becomes stale without new callbacks");
 for(int n=0;n<100;++n)render();std::this_thread::sleep_for(std::chrono::milliseconds(650));pump(.06);just::AnalysisCursor backlogCursor;check(c->readAnalysis(backlogCursor,batch)!=just::AnalysisAvailability::fresh,"consuming stopped audio backlog never revives freshness");just::SpectrumSnapshot stoppedSpectrum;check(c->readSpectrum(stoppedSpectrum)!=just::AnalysisAvailability::fresh,"old samples consumed after stall never revive spectrum");
 bridge->holdAnalysis=true;for(int n=0;n<16;++n){render();pump(.015);}check(bridge->held.size()>4,"captured real undelivered analysis messages");std::this_thread::sleep_for(std::chrono::milliseconds(650));bridge->holdAnalysis=false;bridge->deliverBacklog();just::AnalysisCursor delayedCursor;check(c->readAnalysis(delayedCursor,batch)!=just::AnalysisAvailability::fresh,"delayed real message delivery cannot revive stopped envelope");just::SampleFrame samples;check(c->readSamples(samples)!=just::AnalysisAvailability::fresh && !samples.count,"delayed raw data remains unavailable to UI");
 for(int n=0;n<16;++n){render();pump(.015);}pump(.03);check(c->readSamples(samples)==just::AnalysisAvailability::fresh,"new raw stream recovers");
 bridge->dropRaw=true;for(int n=0;n<40;++n){render();pump(.02);}just::AnalysisCursor independentCursor;check(bridge->rawDropped>4 && c->readAnalysis(independentCursor,batch)==just::AnalysisAvailability::fresh,"genuine envelope stays live while raw delivery alone is interrupted");check(c->readSamples(samples)==just::AnalysisAvailability::stale && !samples.count,"raw stream does not borrow newer envelope freshness");check(c->readSpectrum(stoppedSpectrum)==just::AnalysisAvailability::stale,"FFT does not borrow newer envelope freshness");bridge->dropRaw=false;
 c->editorRemoved();p->setProcessing(false);ref->setProcessing(false);p->setActive(false);ref->setActive(false);p->disconnect(bridge);c->disconnect(p);c->terminate();p->terminate();ref->terminate();
}
int main(){for(double fs:{44100.,48000.,96000.})exercise(fs);check(!allocations&&!releases,"zero C++ allocation/deallocation during real module process");std::cout<<"PASS "<<checks<<" actual-module runtime integration checks; RT new/delete 0; observed stopped backlog remains stale; EQ delayed begin checked when applicable\n";}
