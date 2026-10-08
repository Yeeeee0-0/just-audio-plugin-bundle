#include "plugins/gate/GateEngine.hpp"
#include <iostream>
#include <cstdlib>
#include <random>
#include <chrono>
#include <vector>
#include <limits>
using namespace just;
namespace g=just::gate;
static unsigned checks=0,allocations=0,releases=0;
static thread_local bool processing=false;
void* operator new(std::size_t n){if(processing)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(processing && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void check(bool condition,const char* label){++checks;if(!condition){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
void near(double a,double b,double tolerance,const char* label){check(std::abs(a-b)<=tolerance,label);}
SoundState init(){return initialState({{7,6,5,4}},g::registry);}
void set(SoundState& s,ParamID id,double physical){s.targets[g::registry.index(id)]=g::spec(id).toNormalized(physical);}
template<class T> std::array<T,2> one(g::GateEngine& e,T left,T right=0,bool stereo=false,const T* sc=nullptr,unsigned scChannels=0,std::uint64_t scSilence=0) {
    T l=left,r=right,ol=0,orr=0;
    AudioBlock<T> b;b.inputChannels=b.outputChannels=stereo?2:1;b.samples=1;
    b.inputs={&l,&r};b.outputs={&ol,&orr};b.sidechain={sc,sc};b.sidechainChannels=scChannels;b.sidechainSilenceFlags=scSilence;
    processing=true;e.process(b,{});processing=false;return {ol,orr};
}
void prepared(g::GateEngine& e,SoundState s,double rate=48000,unsigned channels=1,unsigned sc=0) {
    PrepareSpec p;p.sampleRate=rate;p.inputChannels=p.outputChannels=channels;p.sidechainChannels=sc;
    check(e.prepare(p),"valid preparation");processing=true;e.applyTargets(s,0);processing=false;
}
void mappings() {
    // Golden literal IDs and physical points: not generated from UI order.
    constexpr std::uint32_t ids[]={0,100,110,111,112,113,120,121,122,123,130,131,132,133,134,135,136,140,141,150};
    constexpr double mid[]={0,1,-45,45,4.47213595499958,12,6,3.162277660168379,500,100,1,1,0,1,200,1,4472.13595499958,0,0,5};
    check(g::registry.valid() && g::parameterCount==20,"complete valid registry");
    for(unsigned i=0;i<20;++i) {
        check(g::parameters[i].id==ids[i],"literal golden ID");
        const auto& p=g::parameters[i];
        // Boolean midpoint quantizes upward; bypass midpoint is also On.
        near(p.toPhysical(0.5),i==0?1:mid[i],1e-10,"golden normalized midpoint");
        near(p.toPhysical(0),p.minimum,1e-10,"lower endpoint");near(p.toPhysical(1),p.maximum,1e-10,"upper endpoint");
        for(double n:{0.,.01,.25,.5,.75,.99,1.}) {
            const double expected=p.stepCount?std::round(n*p.stepCount)/p.stepCount:n;
            near(p.toNormalized(p.toPhysical(n)),expected,1e-12,"mapping roundtrip");
        }
    }
    check(g::spec(100).enumLabels[0]==std::string("Gate") && g::spec(100).enumLabels[1]==std::string("Expand") && g::spec(100).enumLabels[2]==std::string("Duck"),"mode ordinals");
    near(g::spec(123).toNormalized(100),0.5,1e-12,"Release Init normalization");
    near(g::spec(110).toNormalized(-80),1./9,1e-12,"Threshold Init normalization");
    auto s=init();set(s,g::mode,2);set(s,g::ratio,17);set(s,g::hold,875);set(s,g::scHPHz,1293);
    s.seed=119;s.configurationCount=1;s.configurations[0]={987,10};auto bytes=encodeState(s,g::registry);
    SoundState restored;check(decodeState(bytes.data(),bytes.size(),s.plugin,g::registry,restored)==StateResult::ok,"full sound state restore");
    check(s.targets==restored.targets && restored.seed==119 && restored.configurationCount==1,"hidden/inactive/config targets preserved");
    auto corrupt=bytes;corrupt.pop_back();auto before=restored;
    check(decodeState(corrupt.data(),corrupt.size(),s.plugin,g::registry,restored)==StateResult::malformed && before.targets==restored.targets,"transactional truncated restore");
    auto initial=init();near(g::target(initial,g::mode),1,0,"V2 Expand Init");near(g::target(initial,g::hold),50,1e-12,"saved inactive Hold");
}
void envelopeAndTrigger() {
    for(double fs:{44100.,48000.,96000.,192000.})for(double ms:{1.,5.,100.,200.}) {
        g::GainEnvelope e;e.reset(-24);const auto n=unsigned(std::llround(fs*ms/1000));const double a=std::exp(-1/(fs*ms*.001));
        for(unsigned i=0;i<n;++i)e.tick(0,a,unsigned(std::ceil(fs*.0005)));
        near((e.value()+24)/24,1-std::exp(-1),0.005,"63.2 percent attack/release definition");
        for(unsigned i=0;i<20*n;++i)e.tick(0,a,unsigned(std::ceil(fs*.0005)));
        near(e.value(),0,0,"finite finish reaches exact endpoint");
        e.reset(0);for(unsigned i=0;i<n;++i)e.tick(-24,a,24);
        near(-e.value()/24,1-std::exp(-1),0.005,"negative dB step time constant");
    }
    g::Trigger t;check(t.tick(-39,-40,-43,2400,false,true),"upper threshold triggers");
    check(t.tick(-44,-40,-43,2400,false,false) && t.remaining()==2400,"opening starts Hold without halting opening");
    for(unsigned i=0;i<2399;++i)check(t.tick(-50,-40,-43,9000,false,false),"held for captured Hold");
    check(!t.tick(-50,-40,-43,9000,false,false),"Hold expires at exact sample offset");
    check(!t.tick(-42,-40,-43,9000,false,false),"closing ignores hysteresis zone");
    check(t.tick(-39,-40,-43,2400,false,false),"retrigger cancels closing");
    t.tick(-50,-40,-43,2400,false,false);for(unsigned i=0;i<10;++i)t.tick(-50,-40,-43,0,false,false);
    check(t.tick(-42,-40,-43,0,false,false) && t.remaining()==0,"Hold cancels at close threshold without crossing open");
    t.tick(-50,-40,-43,2400,false,false);check(t.remaining()==2400,"new fall captures full Hold again");
    for(double x:{-80.,-46.,-43.,-40.,-37.,-34.,0.}) {
        double expected=0;
        if(x<-43)expected=2*(x+40);else if(x<-37)expected=-(x+37)*(x+37)/6;
        near(g::expansion(x,-40,3,6,24),std::clamp(expected,-24.,0.),1e-12,"independent soft-knee formula");
    }
    for(double edge:{-43.,-37.}) {
        const auto f=[&](double x){return g::expansion(x,-40,3,6,90);};
        near((f(edge)-f(edge-.0001))/.0001,(f(edge+.0001)-f(edge))/.0001,.0001,"soft-knee continuous derivative");
    }
}
template<class T> void audioMatrix() {
    for(double fs:{44100.,48000.,96000.,192000.})for(unsigned channels:{1u,2u}) {
        auto s=init();set(s,g::range,0);set(s,g::scHPEnabled,1);set(s,g::scLPEnabled,1);
        g::GateEngine e;prepared(e,s,fs,channels);
        std::array<T,2048> left{},right{},outLeft{},outRight{};
        for(unsigned i=0;i<2048;++i){left[i]=T(4*std::sin(i*.27));right[i]=-left[i];}
        for(unsigned n:{0u,1u,16u,17u,32u,64u,128u,256u,731u,1024u,2048u}) {
            AudioBlock<T> b;b.inputs={left.data(),right.data()};b.outputs={outLeft.data(),outRight.data()};b.inputChannels=b.outputChannels=channels;b.samples=n;
            processing=true;e.process(b,{});e.endBlock();processing=false;
            for(unsigned i=0;i<n;++i)check(outLeft[i]==left[i] && (channels==1 || outRight[i]==right[i]),"Range zero exact unity/over-0dBFS unfiltered");
            b.outputs={left.data(),right.data()};processing=true;e.process(b,{});processing=false;
            for(unsigned i=0;i<n;++i)check(left[i]==outLeft[i],"in-place exact unity");
            b.inputSilenceFlags=3;processing=true;e.process(b,{});processing=false;
            for(unsigned i=0;i<n;++i)check(left[i]==0 && (channels==1 || right[i]==0),"host silence flags honored");
            for(unsigned i=0;i<2048;++i){left[i]=T(4*std::sin(i*.27));right[i]=-left[i];}
        }
        check(e.latencySamples()==0 && e.tailSamples().kind==TailKind::none,"zero lookahead latency and no tail");
        left[0]=std::numeric_limits<T>::quiet_NaN();right[0]=std::numeric_limits<T>::infinity();
        AudioBlock<T> b;b.inputs={left.data(),right.data()};b.outputs={outLeft.data(),outRight.data()};b.inputChannels=b.outputChannels=channels;b.samples=2048;
        processing=true;e.process(b,{});processing=false;
        for(unsigned i=0;i<2048;++i)check(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]),"nonfinite contamination isolated");
    }
    auto s=init();set(s,g::ratio,1);g::GateEngine e;prepared(e,s);
    for(unsigned i=0;i<500;++i)check(one(e,T(.0001))[0]==T(.0001),"Ratio one exact unity from first sample");
}
void steadyAndSidechain() {
    for(unsigned mode:{0u,1u,2u})for(double level:{-80.,-60.,-45.,-40.,-35.,-20.}) {
        auto s=init();set(s,g::mode,mode);set(s,g::detector,0);set(s,g::threshold,-40);set(s,g::hold,0);set(s,g::release,5);
        g::GateEngine e;prepared(e,s);double last=0,x=g::linear(level);
        for(unsigned i=0;i<5000;++i)last=one(e,x)[0];
        const double expected=mode==0?(level>-40?0:-24):mode==1?g::expansion(level,-40,2,6,24):(level>-40?-24:0);
        near(g::decibels(last/x),expected,.001,"Gate/Expand/Duck steady bounded attenuation");
    }
    auto s=init();set(s,g::mode,0);set(s,g::threshold,-40);set(s,g::detector,0);set(s,g::scSource,1);
    g::GateEngine missing;prepared(missing,s,48000,1,0);
    for(unsigned i=0;i<300;++i)near(one(missing,.5)[0],.5,1e-15,"missing external starts as unattenuated main");
    check(missing.diagnostics().sidechainMissing && !missing.diagnostics().sidechainSilent,"missing distinguished from silent");
    g::GateEngine silent;prepared(silent,s,48000,1,1);double zero=0;
    for(unsigned i=0;i<10000;++i)one(silent,.5,0.,false,&zero,1,1);
    near(silent.diagnostics().gainDb,-24,.001,"connected silent SC closes Gate");
    check(!silent.diagnostics().sidechainMissing && silent.diagnostics().sidechainSilent,"connected silence truth");
    for(unsigned i=0;i<1000;++i)one(silent,.5);
    near(silent.diagnostics().gainDb,0,1e-12,"disconnect smoothly returns unity");
    double active=.1;check(g::decibels(one(silent,.5,0.,false,&active,1)[0]/.5)>-.1,"first restored trigger loses less than 0.1 dB");
    for(unsigned i=0;i<2000;++i)one(silent,.5,0.,false,&active,1);
    near(silent.diagnostics().gainDb,0,1e-12,"SC recovery opens Gate");
    set(s,g::scSource,0);g::GateEngine stereo;prepared(stereo,s,48000,2);
    for(unsigned i=0;i<3000;++i){auto out=one(stereo,.1,-.1,true);near(out[0],-out[1],1e-15,"identical linked gain for antiphase audio");}
    near(stereo.diagnostics().gainDb,0,0,"antiphase detector does not cancel");
    set(s,g::mode,1);set(s,g::threshold,-20);set(s,g::knee,0);prepared(stereo,s,48000,2);
    for(unsigned i=0;i<15000;++i){auto out=one(stereo,.01,.0001,true);near(out[0]/.01,out[1]/.0001,1e-12,"one-side larger detector applies identical gain");}
}
void blocksAndAutomation() {
    constexpr unsigned n=30000;std::vector<double> audio(n),reference(n),split(n);
    for(unsigned i=0;i<n;++i)audio[i]=std::sin(i*.17)*std::exp(-double(i%3000)/400)*.3;
    auto state=init();set(state,g::threshold,-35);set(state,g::mode,0);set(state,g::detector,0);
    g::GateEngine a,b;prepared(a,state);prepared(b,state);
    auto render=[&](g::GateEngine& e,double* dest,unsigned offset,unsigned count) {
        AudioBlock<double> block;block.inputChannels=block.outputChannels=1;block.samples=count;block.inputs[0]=audio.data()+offset;block.outputs[0]=dest+offset;
        ProcessContext context;context.blockSampleOffset=offset;processing=true;e.process(block,context);e.endBlock();processing=false;
    };
    for(unsigned i=0;i<n;++i)render(a,reference.data(),i,1);
    for(unsigned i=0;i<n;){unsigned count=std::min(unsigned(17+(i*7919)%731),n-i);render(b,split.data(),i,count);i+=count;}
    check(reference==split,"Hold/envelope independent of block partition");
    g::GateEngine automated;prepared(automated,state);std::mt19937 random(20261002);std::uniform_real_distribution<double> normalized(0,1);
    for(unsigned i=0;i<20000;++i) {
        auto index=unsigned(random()%g::parameterCount);state.targets[index]=normalized(random);
        if(i%29==0)state.targets[g::registry.index(g::mode)]=double(random()%3)/2;
        if(i%31==0)state.targets[g::registry.index(g::threshold)]=std::numeric_limits<double>::quiet_NaN();
        processing=true;automated.applyTargets(state,i);processing=false;
        auto out=one(automated,std::sin(i*.13)*4);check(std::isfinite(out[0]),"sample automation random/NaN remains finite");
    }
    check(allocations==0 && releases==0,"all audio/apply/endBlock scopes have zero C++ allocation/release");
}
void transportAndModeCases() {
    auto s=init();set(s,g::mode,0);set(s,g::detector,0);set(s,g::threshold,-40);set(s,g::hold,50);set(s,g::release,5);
    g::GateEngine e;prepared(e,s);
    for(unsigned i=0;i<1000;++i)one(e,g::linear(-39));
    near(e.diagnostics().gainDb,0,0,"real Gate finishes opening exactly");
    for(unsigned i=0;i<100;++i)one(e,g::linear(i%2?-41:-42));
    check(e.diagnostics().gatePhase==g::Phase::Open,"threshold jitter inside hysteresis never closes open Gate");
    one(e,g::linear(-44));check(e.diagnostics().holdRemaining==2400,"real DSP captures 50 ms Hold");
    set(s,g::hold,987);processing=true;e.applyTargets(s,0);processing=false;
    for(unsigned i=0;i<100;++i)one(e,g::linear(-44));
    check(e.diagnostics().holdRemaining==2300,"Hold automation cannot restart a captured countdown");
    one(e,g::linear(-42));check(e.diagnostics().holdRemaining==0,"second nearby hit cancels Hold in hysteresis zone");
    set(s,g::hold,50);processing=true;e.applyTargets(s,0);processing=false;
    one(e,g::linear(-44));set(s,g::bypass,1);processing=true;e.applyTargets(s,0);processing=false;
    double output=0;for(unsigned i=0;i<480;++i)output=one(e,g::linear(-44))[0];
    check(e.diagnostics().holdRemaining==1920,"soft bypass keeps real Hold running");
    near(output,g::linear(-44),0,"fully bypassed raw input is exact");
    double input=g::linear(-44),out=0;AudioBlock<double> block;block.inputs[0]=&input;block.outputs[0]=&out;block.inputChannels=block.outputChannels=block.samples=1;
    ProcessContext seek;seek.seek=true;processing=true;e.process(block,seek);processing=false;
    check(e.diagnostics().gatePhase==g::Phase::Closed && e.diagnostics().holdRemaining==0 && std::isfinite(out),"seek clears runtime Hold safely without changing saved targets");
    near(g::target(s,g::hold),50,0,"seek preserves explicit saved Hold");
    // Reprepare uses restored values and new Fs, never constructor Init.
    set(s,g::bypass,0);set(s,g::mode,1);set(s,g::threshold,-25);set(s,g::ratio,4);set(s,g::range,36);set(s,g::release,17);
    g::GateEngine fresh;prepared(e,s,96000);prepared(fresh,s,96000);
    for(unsigned i=0;i<2000;++i){const double x=std::sin(i*.013)*.03;check(one(e,x)==one(fresh,x),"Fs reprepare matches a fresh engine with the same full targets");}
    for(unsigned mode=0;mode<3;++mode){
        set(s,g::mode,mode);set(s,g::scSource,1);set(s,g::range,24);set(s,g::threshold,-40);set(s,g::release,5);
        g::GateEngine side;prepared(side,s,48000,1,1);double zero=0;
        for(unsigned i=0;i<20;++i)near(one(side,.2)[0],.2,1e-15,"all modes preserve audio when External SC missing");
        for(unsigned i=0;i<10000;++i)one(side,.2,0.,false,&zero,1,1);
        near(side.diagnostics().gainDb,mode==2?0:-24,.001,"connected silent SC follows each mode rather than missing-SC policy");
    }
    for(unsigned mode=0;mode<3;++mode){
        set(s,g::mode,mode);set(s,g::scSource,0);set(s,g::hold,0);set(s,g::range,24);set(s,g::ratio,2);set(s,g::knee,0);
        g::GateEngine single,dbl;prepared(single,s);prepared(dbl,s);float x=.001f;
        float a=0;double b=0;for(unsigned i=0;i<10000;++i){a=one(single,x)[0];b=one(dbl,double(x))[0];}
        near(double(a),double(float(b)),1e-12,"nonunity float32 result matches float64 after I/O quantization");
    }
}
void filtersAndTiming() {
    constexpr double rate=48000,cutoff=2000;
    const double tangent=std::tan(3.141592653589793*cutoff/rate),c=tangent/(1+tangent);
    g::DetectorFilter filter;double energy=0;
    for(unsigned i=0;i<48000;++i){const double y=filter.tick(std::sin(2*3.141592653589793*cutoff*i/rate),c,c,0,1);if(i>=24000)energy+=y*y;}
    near(10*std::log10(energy/12000),-3.0102999566,1e-8,"TPT LP minus 3 dB at physical cutoff");
    filter.reset();energy=0;
    for(unsigned i=0;i<48000;++i){const double y=filter.tick(std::sin(2*3.141592653589793*cutoff*i/rate),c,c,1,0);if(i>=24000)energy+=y*y;}
    near(10*std::log10(energy/12000),-3.0102999566,1e-8,"TPT HP minus 3 dB at physical cutoff");
    std::array<double,128> left{},right{},outL{},outR{};for(unsigned i=0;i<128;++i){left[i]=std::sin(i*.4);right[i]=-left[i];}
    auto s=init();set(s,g::scHPEnabled,1);set(s,g::scLPEnabled,1);set(s,g::scSource,1);
    g::GateEngine e;prepared(e,s,48000,2,2);
    AudioBlock<double> b;b.inputChannels=b.outputChannels=b.sidechainChannels=2;b.samples=128;b.inputs={left.data(),right.data()};b.outputs={outL.data(),outR.data()};b.sidechain=b.inputs;
    std::vector<double> timings;timings.reserve(1500);
    for(unsigned i=0;i<1600;++i){auto start=std::chrono::steady_clock::now();processing=true;e.process(b,{});e.endBlock();processing=false;auto elapsed=std::chrono::steady_clock::now()-start;if(i>=100)timings.push_back(std::chrono::duration<double,std::micro>(elapsed).count());}
    std::sort(timings.begin(),timings.end());const auto p99=timings[1485];
    std::cout<<"MEASURE core 48kHz/128/stereo external SC+HP+LP/RMS: P99="<<p99<<" us, "<<100*p99/(128./48000*1e6)<<"% block; planning budget=5% (measurement, not DAW certification)\n";
}
int main(){mappings();envelopeAndTrigger();audioMatrix<float>();audioMatrix<double>();steadyAndSidechain();blocksAndAutomation();transportAndModeCases();filtersAndTiming();std::cout<<"PASS Gate core checks="<<checks<<"; seed=20261002; f32/f64; 44.1/48/96/192kHz; mono/stereo; C++ allocations="<<allocations<<", releases="<<releases<<'\n';}
