#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "common/ui/DisplayFormat.hpp"
#include "common/ui/AnalysisView.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <iostream>
#include <thread>
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
static void check(bool b,const char* text){++checks;if(!b){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
static void pump(double seconds=.04){
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);
#else
    auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(int(seconds*1000));while(std::chrono::steady_clock::now()<end){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
#endif
}
static void checkAuditionPhase(just::Controller& controller,just::AuditionPhase expected,double rate,const char* label){
#if defined(_WIN32)
    // WM_TIMER is dispatched by the host message queue. One fixed 40 ms pump
    // is not a completion barrier for a 33 ms timer on a loaded Windows runner.
    // Await the actual publication with a bound shorter than the 250 ms lease;
    // do not renew the lease, run another audio block, or synthesize the ACK.
    const auto start=std::chrono::steady_clock::now();
    const auto deadline=start+std::chrono::milliseconds(150);
    while(controller.readAuditionStatus().phase!=expected && std::chrono::steady_clock::now()<deadline)pump(.004);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Windows audition dispatch rate="<<rate<<" expected="<<int(expected)<<" actual="<<int(controller.readAuditionStatus().phase)<<" wait_ms="<<elapsed<<" label="<<label<<std::endl;
#endif
    check(controller.readAuditionStatus().phase==expected,label);
}
static std::vector<std::uint8_t> saved(just::Processor& p){MemoryStream s;check(p.getState(&s)==kResultOk,"save complete state");auto* b=reinterpret_cast<std::uint8_t*>(s.getData());return {b,b+s.getSize()};}
static void formatTest(){
    using namespace just;auto spec=moduleDefinition().parameters.specs[1];spec.minimum=-60;spec.maximum=12;spec.initial=0;spec.unit="dB";spec.title="Gain";
    const auto precise=spec.toNormalized(-10.123456789);char buffer[128];formatDisplay(spec,precise,DisplayContext::simple,{},buffer,sizeof(buffer));check(std::strcmp(buffer,"-10.1")==0,"ordinary simple one decimal");
    TextEditSession edit;auto original=edit.begin(spec,{},precise);double value=0;check(!edit.changed(spec,{},original.c_str(),value),"unchanged exact editing emits no target");check(edit.changed(spec,{},"-10.98765",value) && std::abs(spec.toPhysical(value)+10.98765)<1e-12,"precise text preserved");
    spec.minimum=-24;spec.maximum=36;const double init=spec.toNormalized(0),physical=spec.toPhysical(init);
    for(auto unit:{"dB","dBFS","dBTP"})for(auto context:{DisplayContext::simple,DisplayContext::advanced}){
        spec.unit=unit;formatDisplay(spec,init,context,{},buffer,sizeof(buffer));check(!std::strcmp(buffer,"0.0"),"dB Init roundtrip noise displays one-decimal zero");
        formatDisplay(spec,spec.toNormalized(-.000001),context,{},buffer,sizeof(buffer));check(!std::strcmp(buffer,"0.0"),"rounded dB negative zero suppressed");
    }
    check(spec.toPhysical(init)==physical && init==.4,"format leaves exact normalized and physical targets unchanged");
    original=edit.begin(spec,{},init);check(!edit.changed(spec,{},original.c_str(),value),"zero-neighbourhood precise unchanged Return is still no edit");
    DisplayPolicy explicitPrecision;explicitPrecision.decimals=4;formatDisplay(spec,spec.toNormalized(.0123),DisplayContext::simple,explicitPrecision,buffer,sizeof(buffer));check(!std::strcmp(buffer,"0.0123"),"module explicit dB precision retained");
    spec.minimum=.01;spec.maximum=10;spec.initial=.24;spec.title="Rate";spec.unit="Hz";formatDisplay(spec,spec.toNormalized(.24),DisplayContext::simple,{},buffer,sizeof(buffer));check(!std::strcmp(buffer,"0.24"),"slow rate retains precision");
    spec.minimum=.000001;formatDisplay(spec,spec.toNormalized(.000012),DisplayContext::simple,{},buffer,sizeof(buffer));check(std::strtod(buffer,nullptr)>0 && std::abs(std::strtod(buffer,nullptr)-.000012)<1e-10,"tiny positive Hz is never rounded to zero");
    DisplayPolicy mute;mute.muteAtMinimum=true;check(parseDisplay(spec,mute,"-∞",value) && value==0,"finite mute sentinel");
    MeterHold hold;AnalysisWindow w;w.header.session=w.header.epoch=w.header.sequence=1;w.channels[2].peak=.8;hold.observe(w);hold.reset();hold.observe(w);check(hold.outputPeak==0,"meter reset cannot replay same sample");w.header.sequence++;w.channels[2].peak=.3;hold.observe(w);check(hold.outputPeak==.3,"meter reset resumes next frame");
}
static void exercise(double rate){
    using just::AnalysisAvailability;HostApplication host;auto p=owned(new just::Processor);auto c=owned(new just::Controller);auto reference=owned(new just::Processor);
    check(p->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk,"initialize v2 twins");
    check(p->connect(c)==kResultOk && c->connect(p)==kResultOk,"connect v2");ProcessSetup setup{kRealtime,kSample64,4096,rate};
    check(p->setupProcessing(setup)==kResultOk && reference->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"prepare v2 twins");
    c->editorAttached();
    std::array<double,4096> l{},r{},outL{},outR{},refL{},refR{};double* ins[]={l.data(),r.data()},*outs[]={outL.data(),outR.data()},*refs[]={refL.data(),refR.data()};
    AudioBusBuffers input{},output{},refOutput{};input.numChannels=output.numChannels=refOutput.numChannels=2;input.channelBuffers64=ins;output.channelBuffers64=outs;refOutput.channelBuffers64=refs;
    ProcessContext context{};context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid;context.tempo=120;
    ProcessData data{};data.numSamples=512;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.processContext=&context;
    auto render=[&](bool twin=true){inProcess=true;auto status=p->process(data);inProcess=false;check(status==kResultOk,"v2 process success");if(twin){auto rd=data;rd.outputs=&refOutput;inProcess=true;status=reference->process(rd);inProcess=false;check(status==kResultOk && std::equal(outL.begin(),outL.begin()+data.numSamples,refL.begin()) && std::equal(outR.begin(),outR.begin()+data.numSamples,refR.begin()),"analysis exact twin audio");}context.projectTimeSamples+=data.numSamples;};
    auto initial=saved(*p);for(unsigned block=0;block<12;++block){for(int i=0;i<data.numSamples;++i){l[i]=.5*std::sin(2*3.14159265358979323846*double(context.projectTimeSamples+i)*1000/rate);r[i]=-l[i];}if(block==1)l[17]=.99;render();pump(.015);}
    just::AnalysisCursor cursor;just::AnalysisBatch batch;check(c->readAnalysis(cursor,batch)==AnalysisAvailability::fresh && batch.count,"real envelope reaches UI");bool transient=false,gr=false,anti=false;
    do {for(unsigned i=0;i<batch.count;++i){auto& w=batch.windows[i];transient|=w.channels[0].peak>=.99;gr|=(w.effectFields&just::analysisReduction) && w.reductionDb>4;anti|=w.correlationValid && w.correlation[0]<-.9;check(w.header.sampleRate==rate && w.header.endSample>w.header.startSample,"source window metadata");}check(c->readAnalysis(cursor,batch)==AnalysisAvailability::fresh,"history availability");}while(batch.count);
    check(transient && gr && anti,"transient GR and anti-phase survive real stream");
    just::SpectrumSnapshot spectrum;pump(.03);check(c->readSpectrum(spectrum)==AnalysisAvailability::fresh,"worker FFT reaches controller");double peak=0;for(float a:spectrum.amplitude[0])peak=std::max(peak,double(a));check(peak>.3,"anti-phase stereo spectrum cannot cancel");
    just::SampleFrame raw;check(c->readSamples(raw)==AnalysisAvailability::fresh && raw.count==1024,"synchronous short samples");check(saved(*p)==initial,"analysis keeps state bytes");
    std::this_thread::sleep_for(std::chrono::milliseconds(550));check(c->readAnalysis(cursor,batch)==AnalysisAvailability::stale,"no callback becomes stale");
    render();std::this_thread::sleep_for(std::chrono::milliseconds(600));pump();
    check(c->readAnalysis(cursor,batch)!=AnalysisAvailability::fresh,"audio stop plus stalled main timer cannot revive queued data");
    // Restore fresh source, then apply a complete state and undo with actual audio ACK.
    render();pump();just::SoundState before;check(c->readCompleteSoundState(before),"authoritative full state available");just::SoundState desired;just::moduleDefinition().factoryPresets[0].build(before.plugin,desired);
    check(c->requestApplySoundState(desired) && c->readPresetTransaction()==just::PresetTransactionStatus::pending,"accepted is pending");pump();check(c->readPresetTransaction()==just::PresetTransactionStatus::pending,"stopped callbacks cannot fake applied");
    render(false);pump();check(c->readPresetTransaction()==just::PresetTransactionStatus::applied,"audio applied ACK");just::SoundState actual;check(c->readCompleteSoundState(actual) && just::sameSoundState(actual,desired,just::moduleDefinition().parameters) && actual.seed==123456 && actual.configurationCount==1,"all targets seed config applied");
    check(c->canUndoLastPreset() && c->undoLastPreset(),"one preset undo accepted");render(false);pump();check(c->readCompleteSoundState(actual) && just::sameSoundState(before,actual,just::moduleDefinition().parameters) && !c->canUndoLastPreset(),"undo restores full before and consumes slot");
    auto malformed=desired;malformed.targets[1]=std::numeric_limits<double>::quiet_NaN();check(!c->requestApplySoundState(malformed),"bad state rejected transactionally");
    check(c->requestApplySoundState(desired),"second preset accepted");render(false);pump();c->setParamNormalized(1,.45);check(!c->canUndoLastPreset(),"subsequent user edit invalidates last preset undo");
    // A command does not alter sound state and expires even without UI/main timer progress.
    auto preAudition=saved(*p);auto token=c->beginAudition(1);check(token!=0,"audition begin token");render(false);pump();checkAuditionPhase(*c,just::AuditionPhase::active,rate,"audition audio ACK");
    check(c->renewAudition(token),"audition renew accepted");for(unsigned i=0;i<unsigned(std::ceil(rate*.3/data.numSamples));++i)render(false);pump();checkAuditionPhase(*c,just::AuditionPhase::ended,rate,"audio lease expires with stalled main thread");check(saved(*p)==preAudition,"audition cannot serialize into sound state");
    check(c->beginAudition(999)==0,"unsupported target rejected");token=c->beginAudition(1);render(false);pump();c->endAudition(token);render(false);pump();check(c->readAuditionStatus().phase==just::AuditionPhase::ended,"release disables audition");
    token=c->beginAudition(1);check(token!=0,"queue a fresh begin before total pause");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));render(false);
    check(outL[0]!=l[0] || outL[1]!=l[1],"expired initial begin never switches DSP into audition when audio resumes before timer");
    pump();check(c->readAuditionStatus().phase==just::AuditionPhase::ended && !c->renewAudition(token),"expired pending token ends without fresh lease");
    c->editorRemoved();check(c->readAnalysis(cursor,batch)==AnalysisAvailability::unavailable,"close unsubscribes/invalidate");p->setActive(false);reference->setActive(false);p->disconnect(c);c->disconnect(p);c->terminate();p->terminate();reference->terminate();
}
int main(){formatTest();for(double rate:{44100.,48000.,96000.})exercise(rate);check(allocations==0 && releases==0,"zero process C++ allocation/release");std::cout<<"PASS "<<checks<<" runtime-v2 checks; process new/delete 0\n";}
