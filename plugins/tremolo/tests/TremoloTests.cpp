#include "../Dsp.hpp"
#include "../EditorModel.hpp"
#include "../AmountDisplay.hpp"
#include "common/parameters/Automation.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include <iostream>
#include <vector>
#include <new>
#include <cstdlib>

static bool audioScope=false;
static std::size_t allocations=0,deallocations=0;
void* operator new(std::size_t n){if(audioScope)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(audioScope)++deallocations;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

using namespace just;
namespace t=just::tremolo;
static unsigned assertions=0;
static void check(bool condition,const char* message){++assertions;if(!condition){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
static void near(double a,double b,double epsilon,const char* message){check(std::abs(a-b)<=epsilon,message);}
static SoundState init(){return initialState(pluginIdentities[3].processor,t::registry);}
static void set(SoundState& s,ParamID id,double v){s.targets[t::registry.index(id)]=t::spec(id).toNormalized(v);}
template<class Sample> static void run(t::TremoloEngine& engine,std::vector<Sample>& left,std::vector<Sample>& right,ProcessContext c={},bool inPlace=true) {
    AudioBlock<Sample> b;b.samples=left.size();b.inputChannels=b.outputChannels=right.empty()?1:2;
    b.inputs={left.data(),right.empty()?nullptr:right.data()};b.outputs={left.data(),right.empty()?nullptr:right.data()};
    std::vector<Sample> copyLeft,copyRight;
    if(!inPlace){copyLeft=left;copyRight=right;b.inputs={copyLeft.data(),copyRight.empty()?nullptr:copyRight.data()};}
    audioScope=true;engine.process(b,c);engine.endBlock();audioScope=false;
}
static void prepare(t::TremoloEngine& e,SoundState s,double fs=48000,unsigned channels=2){
    check(e.prepare({fs,2048,channels,channels}),"prepare");
    e.reset(ResetReason::firstActivation);e.applyTargets(s,0);
}
static void mappingTests() {
    check(t::registry.valid(),"parameter registry");
    const ParamID golden[]={0,0x1000,0x1001,0x1100,0x1101,0x1200,0x1201,0x1202,0x1300,0x1301,0x1302,0x1303,0x1304,0x1400};
    for(std::size_t n=0;n<t::registry.count;++n){
        const auto& p=t::parameters[n];check(p.id==golden[n],"golden numeric ID");
        for(double normalized:{0.0,0.25,0.5,0.75,1.0}){
            const double expected=p.stepCount?std::round(normalized*p.stepCount)/p.stepCount:normalized;
            near(p.toNormalized(p.toPhysical(normalized)),expected,1e-12,"normalized roundtrip");
        }
    }
    check(std::strcmp(t::shapes[3],"Saw Up")==0 && std::strcmp(t::timeModes[2],"On Play")==0,"enum ordinals");
    for(double d:{0.0,0.5,1.0})for(double m:{0.0,0.5,1.0}){
        double destination=0.123;
        bool success=t::depthForAmount(t::amountDb(d,m),m,destination);
        check(success==(m>0),"Amount Mix0 disabled");
        if(success)near(destination,d,2e-14,"Amount inverse exact");
        else near(destination,0.123,0,"Amount failure preserves Depth");
    }
    near(t::amountDb(0.5,1),-6.020599913279624,1e-13,"exact Init Amount");
    double depth=0.4;check(!t::depthForAmount(-INFINITY,0.5,depth),"inf invalid for Mix<1");
    check(!t::depthForAmount(-7,0.5,depth),"out of range Amount rejected");
    check(t::depthForAmount(-INFINITY,1,depth) && depth==1,"UI inf maps to finite Depth");
    char label[32];t::formatAmount(0.5,1,label,sizeof(label));check(std::strcmp(label,"-6.02")==0,"display rounding");
    t::formatAmount(1,1,label,sizeof(label));check(std::strcmp(label,"-inf")==0,"infinity display");
    for(unsigned ordinal=0;ordinal<30;++ordinal)check(t::divisionBeats(ordinal,7,8)>0,"Division finite");
    near(t::divisionBeats(3),0.5,0,"eighth note");
    near(t::divisionBeats(7,7,8),3.5,0,"7/8 bar");
    near(t::divisionBeats(8,3,4),6,0,"3/4 two bars");
    near(t::divisionBeats(13),0.75,0,"dotted eighth");
    near(t::divisionBeats(23),1.0/3,1e-15,"triplet eighth");
}
template<class Sample> static void matrixTests() {
    for(double fs:{44100.0,48000.0,96000.0,192000.0})for(unsigned channels:{1u,2u})
    for(unsigned block:{0u,16u,17u,32u,64u,128u,256u,731u,1024u,2048u}) {
        for(bool dryByDepth:{true,false}){
            auto s=init();set(s,dryByDepth?t::depth:t::mix,0);
            t::TremoloEngine e;prepare(e,s,fs,channels);
            std::vector<Sample> l(block),r(channels==2?block:0);
            for(unsigned n=0;n<block;++n){l[n]=Sample(1.4*std::sin(n*0.7));if(channels==2)r[n]=Sample(-l[n]);}
            auto beforeL=l,beforeR=r;run(e,l,r,{},block%2==0);
            check(l==beforeL && r==beforeR,"Depth0/Mix0 bit exact, including >0dBFS");
            check(e.latencySamples()==0 && e.tailSamples().kind==TailKind::none,"zero PDC/tail");
        }
    }
    for(unsigned shape=0;shape<5;++shape) {
        auto s=init();set(s,t::shape,shape);set(s,t::depth,100);set(s,t::mix,100);
        t::TremoloEngine e;prepare(e,s);
        std::vector<Sample> l(12000,Sample(0.5)),r=l;run(e,l,r);
        double low=1,high=0;
        for(unsigned i=0;i<l.size();++i){check(l[i]>=0 && l[i]<=0.5,"unipolar waveform bounds");near(l[i],r[i],0,"Separation0 equal envelopes");low=std::min(low,double(l[i]));high=std::max(high,double(l[i]));}
        if(shape==0 || shape==1 || shape==2)near(low,0,1e-7,"waveform trough silence");
        if(shape==2)check(low<1e-5,"square lower than -100dBFS");
        check(high>0.49,"waveform high");
    }
    auto s=init();set(s,t::stereoPhase,180);
    t::TremoloEngine e;prepare(e,s);
    std::vector<Sample> l(12000,1),r=l;run(e,l,r);
    for(unsigned n=0;n<l.size();++n)near((double(l[n])+r[n])*0.5,0.75,std::is_same<Sample,float>::value?6e-8:1e-14,"sine180 expected mono mean");
    for(double duty:{0.05,0.5,0.95})near(t::edgePhase(50,40,duty,true),0.45*std::min(duty,1-duty),1e-15,"edge45percent cap");
    check(t::edgePhase(0.2,4,0.5,true)>0,"no hard edge");
}
static void timingTests(){
    for(auto endpoints:{std::array<double,2>{359,1},std::array<double,2>{1,359}}){
        auto phaseState=init();set(phaseState,t::phase,endpoints[0]);set(phaseState,t::rateHz,0.05);
        t::TremoloEngine phaseEngine;prepare(phaseEngine,phaseState,48000,1);
        std::vector<double> one(1,1),none;run(phaseEngine,one,none);
        set(phaseState,t::phase,endpoints[1]);phaseEngine.applyTargets(phaseState,0);
        double delta=endpoints[0]==359?2:-2;
        for(unsigned n=0;n<240;++n){
            one[0]=1;run(phaseEngine,one,none);
            double expected=t::fraction((n+1)*0.05/48000+(endpoints[0]+delta*(n+1)/240)/360);
            near(t::circularDifference(phaseEngine.runtimeStatus().phase,expected),0,2e-14,"359/1 shortest circular transition");
        }
    }
    auto s=init();set(s,t::sync,1);set(s,t::timeMode,1);
    ProcessContext c;c.tempoValid=c.ppqValid=c.timeSignatureValid=c.playing=true;c.bpm=120;c.ppq=-0.125;
    t::TremoloEngine e;prepare(e,s);
    std::vector<double> l(1024,1),r=l;run(e,l,r,c);
    near(l[0],0.75,1e-14,"negative PPQ floor");
    for(unsigned i=0;i<l.size();++i)near(l[i],0.75+0.25*std::cos(2*t::pi*(-0.25+i/12000.0)),2e-14,"120BPM eighth 250ms");
    c.ppq+=1024.0*2/48000;c.bpm=240;l.assign(1024,1);r=l;run(e,l,r,c);
    near(e.runtimeStatus().effectiveRate,8,0,"tempo change immediate");
    c.tempoValid=false;c.ppqValid=false;l.assign(1,1);r=l;run(e,l,r,c);
    auto status=e.runtimeStatus();check(status.tempoUnavailable && status.positionUnavailable,"sync fallback status");
    near(status.effectiveRate,8,0,"last valid BPM fallback");
    t::TremoloEngine fallback;prepare(fallback,s);run(fallback,l,r);
    near(fallback.runtimeStatus().effectiveRate,4,0,"initial120BPM fallback");
    set(s,t::sync,0);fallback.applyTargets(s,0);run(fallback,l,r);
    check(fallback.runtimeStatus().transportNeedsSync,"Transport requires Sync saved mode preserved");
    set(s,t::timeMode,2);t::TremoloEngine onPlay;prepare(onPlay,s);
    l.assign(1234,1);r=l;run(onPlay,l,r);
    c={};c.playing=true;l.assign(1,1);r=l;run(onPlay,l,r,c);
    near(onPlay.runtimeStatus().phase,0,0,"On Play reset phase");
    c.seek=true;run(onPlay,l,r,c);near(onPlay.runtimeStatus().phase,4.0/48000,1e-16,"On Play seek does not reset");
    auto free=init();t::TremoloEngine longRun;prepare(longRun,free,48000,1);
    l.assign(2048,1);r.clear();constexpr std::uint64_t samples=48000ull*600;
    for(std::uint64_t offset=0;offset<samples;offset+=2048){const auto count=std::min<std::uint64_t>(2048,samples-offset);l.resize(count);std::fill(l.begin(),l.end(),1);run(longRun,l,r);}
    const double last=longRun.runtimeStatus().phase;
    const double error=std::abs(t::circularDifference(last,t::fraction((samples-1)*4.0/48000)))*48000/4;
    check(error<1,"4Hz600sec phase error below1sample");std::cout<<"timingErrorSamples="<<error<<"\n";
}
struct Queue final:EventSource {
    std::array<ParamEvent,5> points{{{t::depth,0,0},{t::depth,1,31},{t::depth,0.25,63},{t::shape,0.5,16},{t::shape,0,48}}};
    int32_t queueCount()const noexcept override{return 2;}
    ParamID parameterID(int32_t q)const noexcept override{return q==0?t::depth:t::shape;}
    int32_t pointCount(int32_t q)const noexcept override{return q==0?3:2;}
    bool point(int32_t q,int32_t i,ParamEvent& e)const noexcept override{e=points[i+(q==0?0:3)];return true;}
};
static void stateAndAutomationTests(){
    auto state=init();for(std::size_t i=0;i<t::registry.count;++i)state.targets[i]=0.37;state.seed=123;
    auto bytes=encodeState(state,t::registry);SoundState restored;
    check(decodeState(bytes.data(),bytes.size(),state.plugin,t::registry,restored)==StateResult::ok,"state decode");
    check(restored.targets==state.targets && restored.seed==123,"complete hidden state preserved");
    auto saved=restored;
    check(decodeState(bytes.data(),bytes.size()-1,state.plugin,t::registry,restored)==StateResult::malformed,"truncated rejected");
    check(restored.targets==saved.targets,"transactional malformed restore");
    auto s=init();Queue queue;Automation automation(t::registry);automation.begin(&queue,s,64);
    t::TremoloEngine e;prepare(e,s);double input=1,output=0;
    AudioBlock<double> b;b.inputChannels=b.outputChannels=b.samples=1;b.inputs[0]=&input;b.outputs[0]=&output;
    double previous=1,maximumJump=0;
    for(int sample=0;sample<64;++sample){
        audioScope=true;automation.evaluate(sample,s);e.applyTargets(s,sample);
        ProcessContext context;context.blockSampleOffset=sample;e.process(b,context);audioScope=false;
        check(std::isfinite(output) && output>=0 && output<=1,"sample automation bounded");
        maximumJump=std::max(maximumJump,std::abs(output-previous));previous=output;
        if(sample==16)near(s.targets[t::registry.index(t::shape)],0.5,0,"shape exact event");
        if(sample==31)near(s.targets[t::registry.index(t::depth)],1,0,"depth middle point used");
        if(sample==48)near(s.targets[t::registry.index(t::shape)],0,0,"shape later event used");
    }
    near(s.targets[t::registry.index(t::depth)],0.25,0,"last depth point");
    check(maximumJump<0.01,"automation trajectory switching");
    // Zero-sample flush updates target without moving the LFO.
    auto before=e.runtimeStatus().phase;set(s,t::rateHz,8);e.applyTargets(s,0);b.samples=0;e.process(b,{});
    near(e.runtimeStatus().phase,before,0,"zero block phase preserved");
    // Invalid direct targets are refused, rather than poisoning smoothers.
    s.targets[t::registry.index(t::depth)]=NAN;e.applyTargets(s,0);b.samples=1;e.process(b,{});
    check(std::isfinite(output),"invalid target isolation");
    input=INFINITY;e.process(b,{});near(output,0,0,"nonfinite input isolated");
    input=std::numeric_limits<double>::denorm_min();e.process(b,{});near(output,0,0,"denormal flush");
    set(s,t::inputGain,12);set(s,t::outputGain,6);
    set(s,t::bypass,1);e.applyTargets(s,0);input=0.8;
    for(int n=0;n<300;++n)e.process(b,{});
    near(output,input,0,"true dry bypass with unity trims");
}
struct FakeEditor {
    SoundState sound=init();EditorViewState view{};unsigned begins=0,writes=0,ends=0;
    static double read(void* p,ParamID id){auto& f=*static_cast<FakeEditor*>(p);return f.sound.targets[t::registry.index(id)];}
    static bool begin(void* p,ParamID){++static_cast<FakeEditor*>(p)->begins;return true;}
    static bool perform(void* p,ParamID id,double value){auto& f=*static_cast<FakeEditor*>(p);++f.writes;f.sound.targets[t::registry.index(id)]=value;return true;}
    static void end(void* p,ParamID){++static_cast<FakeEditor*>(p)->ends;}
    EditorServices services(){return {this,&view,read,begin,perform,end};}
};
static void editorTests(){
    FakeEditor f;set(f.sound,t::shape,4);set(f.sound,t::duty,70);set(f.sound,t::mix,50);
    const auto baseline=f.sound;
    {set(f.sound,t::phase,360);t::EditorModel editor(f.services());(void)editor.value(t::phase);
        check(f.sound.targets[t::registry.index(t::phase)]==1 && f.writes==0,"Phase alias preserves normalized1");}
    f.sound=baseline;
    t::TremoloEngine reference,withEditor;prepare(reference,baseline);prepare(withEditor,baseline);
    for(unsigned iteration=0;iteration<16;++iteration){
        f.view.advanced=!f.view.advanced;
        {t::EditorModel editor(f.services());(void)editor.value(t::duty);(void)editor.amount();}
        std::vector<double> a(173,1),b=a,ar=a,br=a;run(reference,a,ar);run(withEditor,b,br);
        check(a==b && ar==br,"view switch/close/reopen audio bit identical");
        check(f.sound.targets==baseline.targets && f.writes==0,"view never writes hidden state");
    }
    {
        t::EditorModel editor(f.services());check(editor.writeAmount(-3),"Amount gesture");
        check(f.sound.targets[t::registry.index(t::mix)]==baseline.targets[t::registry.index(t::mix)],"Amount only writes Depth");
    }
    check(f.begins==1 && f.ends==1,"close balances gesture");
    set(f.sound,t::mix,0);double depth=f.sound.targets[t::registry.index(t::depth)];
    {t::EditorModel editor(f.services());check(!editor.writeAmount(-3) && !editor.amountEditable(),"Mix0 disabled");
        check(!editor.separationEditable(),"unknown mono context does not fake stereo edit");}
    near(f.sound.targets[t::registry.index(t::depth)],depth,0,"disabled Amount preserves Depth");
}
static void amountDisplayTests(){
    FakeEditor a,b;set(a.sound,t::mix,100);set(b.sound,t::mix,50);
    t::AmountDisplay first(a.services()),second(b.services());
    char text[128];first.format(.5,DisplayContext::simple,text,sizeof(text));
    check(std::strcmp(text,"-6.0")==0,"Amount uses exact saved Mix and one decimal display");
    second.format(.5,DisplayContext::simple,text,sizeof(text));
    check(std::strcmp(text,"-2.5")==0,"Amount display context stays per instance");
    double coordinate=0;
    check(first.parse("-3.25 dB",coordinate),"decimal Amount with unit parses");
    near(t::amountDb(1-coordinate,1),-3.25,1e-14,"Amount decimal inverse is not display-quantized");
    check(second.parse("-3.25",coordinate),"same text uses second instance Mix");
    near(t::amountDb(1-coordinate,.5),-3.25,1e-14,"second Amount inverse uses its own Mix");
    const auto original=a.sound;
    auto controls=first.controlServices();
    check(controls.beginEdit(controls.owner,t::depth),"Amount adapter begins original Depth gesture");
    check(controls.performEdit(controls.owner,t::depth,.8),"Amount adapter submits UI coordinate");
    controls.endEdit(controls.owner,t::depth);
    near(a.sound.targets[t::registry.index(t::depth)],.2,1e-15,"upward Amount coordinate decreases original Depth");
    for(unsigned i=0;i<t::registry.count;++i)if(t::parameters[i].id!=t::depth)
        near(a.sound.targets[i],original.targets[i],0,"Amount adapter preserves all other host targets");
    check(a.begins==1 && a.writes==1 && a.ends==1,"Amount adapter forwards one balanced host gesture");
    check(first.parse("−∞",coordinate) && coordinate==0,"full wet infinite Amount maps finite Depth1");
    check(!second.parse("-inf",coordinate),"partial wet infinite Amount rejected");
    check(!first.parse("3",coordinate) && !first.parse("NaN",coordinate) && !first.parse("-2junk",coordinate),"invalid Amount text rejected");
    set(b.sound,t::mix,0);check(!second.parse("-3",coordinate),"Mix0 rejects Amount edit");
    const auto before=a.sound;const auto writes=a.writes;
    first.format(.123456789,DisplayContext::editing,text,sizeof(text));
    for(unsigned i=0;i<600;++i)(void)controls.readTarget(controls.owner,t::depth);
    check(a.sound.targets==before.targets && a.writes==writes,"display and repeated refresh reads never write sound");
    check(first.controlSpec().id==t::depth && t::spec(t::depth).maximum==100,"UI display spec leaves registered Depth mapping intact");
    set(a.sound,t::sync,1);set(a.sound,t::division,23);
    check(!hiddenParametersCustom(t::registry,a.sound,t::simpleControls,std::size(t::simpleControls)),"visible Free/Sync and Division do not falsely mark Advanced Custom");
}
int main(){
    mappingTests();matrixTests<float>();matrixTests<double>();timingTests();stateAndAutomationTests();editorTests();amountDisplayTests();
    check(allocations==0 && deallocations==0,"audio scope C++ new/delete zero");
    std::cout<<"PASS "<<assertions<<" checks; C++ allocations="<<allocations<<", deallocations="<<deallocations<<"\n";
}
