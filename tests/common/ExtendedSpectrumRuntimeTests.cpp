// Real common Processor/Controller, SDK transport/timers and FFT worker. No
// native view attachment, NSApplication, product install or preference files.
#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include <CoreFoundation/CoreFoundation.h>
#include <iostream>
static thread_local bool inAudio=false;static unsigned allocations=0,releases=0,checks=0;
void* operator new(std::size_t n){if(inAudio)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(inAudio && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
namespace just {
namespace {
constexpr ParameterSpec specs[]={
    {"fixture.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
    {"fixture.gain",1,"Gain","",0,1,.6,Mapping::linear,0,true,"all",Transition::continuous,0}};
class FixtureEngine final:public Engine {
    double gain=.6;
    template<class S> void render(AudioBlock<S> b) noexcept {for(unsigned c=0;c<b.outputChannels;++c)for(unsigned i=0;i<b.samples;++i)b.outputs[c][i]=S(gain*b.inputs[c][i]);}
public:
    bool prepare(const PrepareSpec& s) override{return s.valid();}void reset(ResetReason) noexcept override{}
    void applyTargets(const SoundState& s,std::int32_t) noexcept override{gain=s.targets[0]>=.5?1:s.targets[1];}
    void process(AudioBlock<float> b,const ProcessContext&) noexcept override{render(b);}
    void process(AudioBlock<double> b,const ProcessContext&) noexcept override{render(b);}
    std::uint32_t latencySamples() const noexcept override{return 0;}
    Tail tailSamples() const noexcept override{return {TailKind::finite,0};}
    bool readTelemetry(Telemetry&) noexcept override{return false;}
};
Engine* create(){return new FixtureEngine;}
}
ModuleDefinition& analyzerFixture(){static ModuleDefinition m{{specs,2},create};return m;}
const ModuleDefinition& moduleDefinition(){return analyzerFixture();}
}
class Handler final:public FObject,public IComponentHandler,public IComponentHandler2 {
public:
    unsigned dirty=0,edits=0;
    tresult PLUGIN_API beginEdit(ParamID) override{++edits;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue) override{++edits;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{++edits;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    tresult PLUGIN_API setDirty(TBool value) override{dirty+=value;return kResultOk;}
    tresult PLUGIN_API requestOpenEditor(FIDString) override{return kResultFalse;}
    tresult PLUGIN_API startGroupEdit() override{return kResultOk;}
    tresult PLUGIN_API finishGroupEdit() override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) DEF_INTERFACE(IComponentHandler2) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
template<class T> static std::vector<char> saved(T& object){MemoryStream s;check(object.getState(&s)==kResultOk,"serialize state");return {s.getData(),s.getData()+s.getSize()};}
static auto uiChunk(int version,const just::EditorViewState& v,const just::AnalyzerPreferences& p={},int tag=just::analyzerPreferenceTag,int schema=1,int length=24){
    MemoryStream m;IBStreamer s(&m,kLittleEndian);s.writeInt32(version);s.writeInt32(v.width);s.writeInt32(v.height);s.writeDouble(v.scale);s.writeBool(v.advanced);
    if(version>=2){s.writeInt32(static_cast<int>(v.language));s.writeBool(v.backgroundEnabled);s.writeBool(v.reduceMotion);s.writeBool(v.lowPerformance);s.writeInt32(v.backgroundFps);s.writeDouble(v.renderScale);}
    if(version>=3){s.writeInt32(tag);s.writeInt32(schema);s.writeInt32(length);s.writeInt32(p.rangeDb);s.writeDouble(p.tiltDbPerOctave);s.writeInt32(p.fftSize);s.writeInt32(static_cast<int>(p.response));s.writeInt32(static_cast<int>(p.source));}
    return std::vector<char>(m.getData(),m.getData()+m.getSize());
}
static tresult restore(just::Controller& c,std::vector<char> bytes){MemoryStream s(bytes.data(),bytes.size());return c.setState(&s);}
static void preferences(){
    using namespace just;HostApplication host;auto& module=analyzerFixture();module.extendedSpectrum=false;
    auto legacy=owned(new Controller);check(legacy->initialize(&host)==kResultOk,"legacy controller init");AnalyzerPreferences prefs;
    check(!legacy->supportsExtendedSpectrum() && !legacy->readAnalyzerPreferences(prefs) && !legacy->writeAnalyzerPreferences(prefs),"legacy capability stays disabled");
    check(saved(*legacy)==uiChunk(2,legacy->viewState),"other-module UI v2 bytes retain exact old encoding");
    check(restore(*legacy,uiChunk(3,legacy->viewState))==kResultFalse,"legacy controller rejects unsupported module extension");
    module.extendedSpectrum=true;auto c=owned(new Controller);auto h=owned(new Handler);
    check(c->initialize(&host)==kResultOk && c->setComponentHandler(h)==kResultOk,"extended controller init");
    check(c->readAnalyzerPreferences(prefs) && prefs==AnalyzerPreferences{} && prefs.rangeDb==120 && c->viewState.language==UiLanguage::english,"fresh analyzer/UI defaults");
    c->setParamNormalized(1,.37123456789);const auto target=c->getParamNormalized(1);
    prefs={120,0,8192,AnalyzerResponse::slow,AnalyzerSource::post};check(c->writeAnalyzerPreferences(prefs) && h->dirty==1 && !h->edits,"UI preferences mark host dirty without parameter gestures");
    check(c->writeAnalyzerPreferences(prefs) && h->dirty==1,"unchanged preference write is a no-op");
    c->viewState.language=UiLanguage::chinese;c->viewState.visualsPaused=true;c->viewState.visualResumeGeneration=71;
    auto v3=saved(*c);check(v3==uiChunk(3,c->viewState,prefs),"bounded tagged version1 module extension is explicitly encoded");
    auto reopened=owned(new Controller);check(reopened->initialize(&host)==kResultOk && restore(*reopened,v3)==kResultOk,"reopen UI v3");AnalyzerPreferences actual;
    check(reopened->readAnalyzerPreferences(actual) && actual==prefs && saved(*reopened)==v3,"all five preferences and Chinese selection roundtrip exactly");
    for(unsigned storedRange:{60u,90u,120u}){auto stored=prefs;stored.rangeDb=storedRange;
        check(restore(*reopened,uiChunk(3,reopened->viewState,stored))==kResultOk && reopened->readAnalyzerPreferences(actual) && actual.rangeDb==storedRange,"explicit saved UIv3 range survives new120dB default");}
    for(std::size_t size=0;size<v3.size();++size){auto truncated=v3;truncated.resize(size);check(restore(*c,truncated)==kResultFalse && saved(*c)==v3,"every truncated UI chunk rejects transactionally");}
    for(auto bad:{uiChunk(3,c->viewState,prefs,7),uiChunk(3,c->viewState,prefs,analyzerPreferenceTag,2),uiChunk(3,c->viewState,prefs,analyzerPreferenceTag,1,1000000),uiChunk(4,c->viewState,prefs)})check(restore(*c,bad)==kResultFalse && saved(*c)==v3,"unknown tag/schema/version or unbounded length rejected");
    for(unsigned field=0;field<5;++field){auto bad=prefs;if(field==0)bad.rangeDb=91;if(field==1)bad.tiltDbPerOctave=std::numeric_limits<double>::quiet_NaN();if(field==2)bad.fftSize=16384;if(field==3)bad.response=static_cast<AnalyzerResponse>(3);if(field==4)bad.source=static_cast<AnalyzerSource>(-1);
        check(!c->writeAnalyzerPreferences(bad) && restore(*c,uiChunk(3,c->viewState,bad))==kResultFalse && saved(*c)==v3,"invalid value rejects both setter and UI restore transactionally");}
    check(c->viewState.visualsPaused && c->viewState.visualResumeGeneration==71 && h->dirty==1,"failed restores preserve pause identity and host dirty count");
    for(int version:{1,2}){check(restore(*c,uiChunk(version,c->viewState))==kResultOk && c->readAnalyzerPreferences(actual) && actual==AnalyzerPreferences{},"old UI state restores approved analyzer defaults");check(c->writeAnalyzerPreferences(prefs),"reselect nondefaults");}
    const auto dirty=h->dirty;check(restore(*c,v3)==kResultOk && h->dirty==dirty && c->getParamNormalized(1)==target && !h->edits,"host restore does not dirty host or write sound targets");
    legacy->terminate();reopened->terminate();c->setComponentHandler(nullptr);c->terminate();
}
static void pump(double seconds=.012){CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);}
static void runtime(){
    using namespace just;HostApplication host;auto p=owned(new Processor),reference=owned(new Processor);auto c=owned(new Controller);
    check(p->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"real runtime init");
    check(p->connect(c)==kResultOk && c->connect(p)==kResultOk,"real processor/controller transport connection");
    ProcessSetup setup{kRealtime,kSample64,1024,48000};check(p->setupProcessing(setup)==kResultOk && reference->setupProcessing(setup)==kResultOk,"prepare real processors");
    check(p->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"activate processors without native UI");c->editorAttached();
    const auto sound=saved(*p);const auto latency=p->getLatencySamples(),tail=p->getTailSamples();
    std::array<double,1024> left{},right{},a{},b{},ra{},rb{};double* ins[]={left.data(),right.data()},*outs[]={a.data(),b.data()},*refs[]={ra.data(),rb.data()};
    AudioBusBuffers input{},output{},refout{};input.numChannels=output.numChannels=refout.numChannels=2;input.channelBuffers64=ins;output.channelBuffers64=outs;refout.channelBuffers64=refs;
    ProcessData d{};d.numSamples=1024;d.symbolicSampleSize=kSample64;d.numInputs=d.numOutputs=1;d.inputs=&input;d.outputs=&output;
    std::uint64_t position=0;
    auto render=[&](unsigned blocks){for(unsigned n=0;n<blocks;++n){for(unsigned i=0;i<1024;++i){left[i]=.2*std::sin(2*3.14159265358979323846*64*(position+i)/4096);right[i]=-left[i];}
        auto rd=d;rd.outputs=&refout;inAudio=true;auto ok=p->process(d),rok=reference->process(rd);inAudio=false;check(ok==kResultOk && rok==kResultOk,"real audio process");
        for(unsigned i=0;i<1024;++i)check(a[i]==ra[i] && b[i]==rb[i],"analysis/preferences leave audio bit exact to unsubscribed reference");position+=1024;pump();}};
    ExtendedSpectrumSnapshot spectrum;
    auto ready=[&](unsigned size){for(unsigned attempt=0;attempt<40;++attempt){if(c->readExtendedSpectrum(spectrum)==AnalysisAvailability::fresh && spectrum.fftSize==size)return true;pump(.004);}return false;};
    render(12);check(ready(4096) && std::abs(spectrum.amplitude[0][64]-.2)<.001 && std::abs(spectrum.amplitude[1][64]-.12)<.001,"real 1024-frame transport feeds calibrated 4096 input/output FFT");
    AnalyzerPreferences prefs;check(c->readAnalyzerPreferences(prefs),"read live preferences");prefs.fftSize=8192;prefs.response=AnalyzerResponse::slow;
    check(c->writeAnalyzerPreferences(prefs) && c->readExtendedSpectrum(spectrum)==AnalysisAvailability::unavailable,"live config immediately invalidates old spectrum");
    render(12);check(ready(8192) && spectrum.hopSize==1024 && spectrum.hasEnvelope && std::abs(spectrum.amplitude[0][128]-.2)<.001,"live 8192 overlaps on existing worker with full source envelope");
    auto generation=spectrum.presentationGeneration;c->setVisualsPaused(true);render(4);check(c->readExtendedSpectrum(spectrum)==AnalysisAvailability::unavailable,"live bypass presentation pause drops measurements");
    c->setVisualsPaused(false);render(4);check(c->readExtendedSpectrum(spectrum)!=AnalysisAvailability::fresh,"resume does not reuse pre-bypass window");render(6);
    check(ready(8192) && spectrum.presentationGeneration!=generation,"real post-resume full spectrum recovers");
    c->editorRemoved();render(1);c->editorAttached();render(12);check(ready(8192),"close/reopen restarts one worker with retained analyzer preferences");
    check(saved(*p)==sound && p->getLatencySamples()==latency && p->getTailSamples()==tail,"full sound state/PDC/tail unchanged");
    c->editorRemoved();p->setActive(false);reference->setActive(false);p->disconnect(c);c->disconnect(p);c->terminate();p->terminate();reference->terminate();
}
int main(){preferences();runtime();check(!allocations && !releases,"no C++ new/delete during real audio processing");std::cout<<"PASS "<<checks<<" module UI preferences and real common runtime/transport/FFT checks; RT new/delete 0; no native attachment\n";}
