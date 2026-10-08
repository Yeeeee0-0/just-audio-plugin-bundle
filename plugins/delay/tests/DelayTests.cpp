#include "../DelayEngine.hpp"
#include "../EditorModel.hpp"
#include "../PreviewLayout.hpp"
#include "common/vst3/Module.hpp"
#include <iostream>
#include <random>
#include <cstdlib>
#include <chrono>
using namespace just;
using namespace just::delay;
static thread_local bool realtime=false;
static unsigned allocations=0,releases=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void check(bool b,const char* message){if(!b){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
struct WetCapture final:AnalysisTap {
    std::array<EffectAnalysisSample,2048> samples{};std::uint32_t count=0;
    void pushSample(std::uint32_t offset,const EffectAnalysisSample& sample) noexcept override {
        if(offset<samples.size())samples[offset]=sample;
        ++count;
    }
};
bool close(double a,double b,double tolerance=1e-10){return std::abs(a-b)<=tolerance;}
SoundState init(){return initialState({},registry);}
struct Render {std::vector<double> left,right;};
Render run(DelayEngine& e,const std::vector<double>& left,const std::vector<double>& right,ProcessContext context={},int channels=2,int outputs=2,std::uint64_t silence=0,int blockSize=64){
    Render r{std::vector<double>(left.size()),std::vector<double>(left.size())};
    std::size_t at=0;
    while(at<left.size()){
        auto count=std::uint32_t(std::min(std::size_t(blockSize),left.size()-at));
        AudioBlock<double> b{{left.data()+at,right.empty()?nullptr:right.data()+at},{r.left.data()+at,r.right.data()+at},std::uint32_t(channels),std::uint32_t(outputs),count,silence};
        realtime=true;e.process(b,context);e.endBlock();realtime=false;at+=count;
    }
    return r;
}
void prepare(DelayEngine& e,SoundState s,double rate=48000,int in=2,int out=2){check(e.prepare({rate,2048,std::uint32_t(in),std::uint32_t(out),0,SampleFormat::float64,false}),"prepare");e.applyTargets(s,0);}
double energy(const std::vector<double>& v,std::size_t begin,std::size_t count){double sum=0;for(auto n=begin;n<std::min(begin+count,v.size());++n)sum+=v[n]*v[n];return sum;}
int main(int argc,char** argv){
    if(argc==2 && std::strcmp(argv[1],"--registry")==0){
        std::cout.precision(17);
        for(const auto& p:parameters){
            std::cout<<p.id<<"|"<<p.stableKey<<"|"<<p.minimum<<"|"<<p.maximum<<"|"<<p.initial<<"|"<<(p.mapping==Mapping::linear?"linear":"logarithmic")<<"|"<<p.stepCount<<"|"<<p.unit<<"|"<<p.applicableModes<<"|"<<(p.transition==Transition::continuous?"continuous":p.transition==Transition::discrete?"discrete":"preparedConfiguration")<<"|"<<p.smoothingMs<<"|";
            if(p.enumLabels)for(unsigned i=0;i<=p.stepCount;++i){if(i)std::cout<<",";std::cout<<p.enumLabels[i];}std::cout<<"\n";
        }return 0;
    }
    check(registry.valid() && moduleDefinition().valid(),"valid parameter/module registry");
    check(moduleDefinition().editorSizeLimits().minimumWidth==640 && moduleDefinition().editorSizeLimits().minimumHeight==480,"Delay outer editor minimum fits echo card and three controls");
    check(registry.count==45 && spec(bypass).id==0,"explicit 45-entry parameter registry");
    check(moduleDefinition().simpleControlCount==3,"approved Simple has three controls");
    for(std::size_t i=0;i<moduleDefinition().simpleControlCount;++i)for(std::size_t j=0;j<moduleDefinition().simpleControls[i].parameterCount;++j)
        check(moduleDefinition().simpleControls[i].parameters[j]!=highCut,"High Cut stays Advanced");
    for(int w:{600,640,680,880,1080,1120,1880})for(int h:{356,370,460,516,680,1090}){
        auto layout=PreviewLayout::fit(w,h);
        check(layout.graph.x>=0 && layout.graph.width>0 && layout.graph.x+layout.graph.width<=w,"echo card fits content width");
        check(layout.graph.y+layout.graph.height<layout.ping.y,"card and Ping Pong do not overlap");
        for(const auto& cell:layout.cells)check(cell.x>=0 && cell.x+cell.width<=w && cell.y>layout.ping.y+layout.ping.height && cell.y+cell.height<=h,"all control hit areas fit below Ping Pong");
        if(layout.note.height)check(layout.cells[0].y+layout.cells[0].height<=layout.note.y,"status note stays below controls");
    }
    {
        auto state=init();PingPongRoute toggle;
        for(int prior:{0,1}){set(state,route,prior);toggle.observe(state);auto before=state.targets;
            check(toggle.next(state)==2 && state.targets==before,"Ping Pong On only proposes existing route");
            set(state,route,2);before=state.targets;
            check(toggle.next(state)==prior && state.targets==before,"Ping Pong Off remembers Stereo or Dual without writes");
        }
        set(state,route,0);toggle.observe(state);set(state,route,1);toggle.observe(state);set(state,route,2);
        check(toggle.next(state)==1,"observed host Dual update becomes return route");
    }

    {double v=0.5;check(parseDisplay(wetLevel,"-∞ dB",v) && v==0,"UI infinity alias maps to finite exact mute");check(!parseDisplay(feedback,"-∞",v),"infinity alias never accepted by feedback");}
    for(auto& p:parameters)for(double x:{0.0,0.17,0.5,0.93,1.0}){
        double y=p.toNormalized(p.toPhysical(x));double expect=p.stepCount?std::round(x*p.stepCount)/p.stepCount:x;
        check(close(y,expect,1e-12),"mapping endpoints and round trips");
    }
    check(close(noteBeats(4),1) && close(noteBeats(11),1.5) && close(noteBeats(18),2.0/3),"stable straight/dotted/triplet ordinals");
    {auto s=init();set(s,route,2);check(simpleTimeEditable(s),"Ping Pong keeps symmetric Time directly editable");set(s,timeR,375);check(!simpleTimeEditable(s),"asymmetric Ping Pong Time is Custom");}
    {
        DelayEngine measured,reference;auto state=init();set(state,route,2);set(state,mix,100);
        set(state,timeL,10);set(state,timeR,15);set(state,feedback,50);
        prepare(measured,state);prepare(reference,state);
        std::array<double,2048> inL{},inR{},outL{},outR{},refL{},refR{};inL[0]=inR[0]=1;
        double* inputs[]={inL.data(),inR.data()},*outputs[]={outL.data(),outR.data()},*referenceOut[]={refL.data(),refR.data()};
        AudioBlock<double> block{{inputs[0],inputs[1]},{outputs[0],outputs[1]},2,2,2048};
        AudioBlock<double> baseline{{inputs[0],inputs[1]},{referenceOut[0],referenceOut[1]},2,2,2048};
        WetCapture capture;ProcessContext context;context.analysis=&capture;
        realtime=true;measured.process(block,context);measured.endBlock();reference.process(baseline,{});reference.endBlock();realtime=false;
        check(allocations==0 && releases==0,"wet sink allocates nothing on audio thread");
        check(capture.count==2048 && outL==refL && outR==refR,"wet analysis leaves audio bit-identical");
        for(unsigned i=0;i<2048;++i) {
            check((capture.samples[i].validFields&(analysisWet|analysisDelay))==(analysisWet|analysisDelay),"wet and actual delay fields valid");
            check(close(capture.samples[i].wet[0],outL[i]) && close(capture.samples[i].wet[1],outR[i]),"wet contribution equals fully wet output");
        }
        check(outL[495]>.9 && outR[495]==0 && outR[1215]>.05 && outL[1215]==0,"captured real Ping Pong alternation and timing");
        set(state,mix,0);measured.applyTargets(state,0);inL.fill(0);inR.fill(0);block.inputSilenceFlags=3;
        for(int n=0;n<4;++n){capture.count=0;realtime=true;measured.process(block,context);measured.endBlock();realtime=false;}
        for(unsigned i=0;i<2048;++i)check(capture.samples[i].wet[0]==0 && capture.samples[i].wet[1]==0,"Mix zero publishes zero wet contribution");
    }
    {
        DelayEngine e;auto s=init();set(s,mix,0);prepare(e,s);
        std::vector<double> l(1200),r(1200);std::mt19937 generator(20261002);
        for(std::size_t i=0;i<l.size();++i){l[i]=double(generator()%2000)/1000-1;r[i]=-l[i];}
        auto o=run(e,l,r);
        for(std::size_t i=0;i<l.size();++i)check(o.left[i]==(i<driveLatency?0:l[i-driveLatency]),"Mix0 exact aligned dry");
        set(s,bypass,1);set(s,input,12);set(s,output,-24);e.applyTargets(s,0);run(e,std::vector<double>(1000),std::vector<double>(1000));
        o=run(e,l,r);for(std::size_t i=driveLatency;i<l.size();++i)check(o.left[i]==l[i-driveLatency],"bypass ignores trims with same PDC");
        check(e.latencySamples()==15,"fixed oversampling PDC");
    }
    {
        DelayEngine e;auto s=init();set(s,mix,100);set(s,feedback,0);set(s,timeL,10);set(s,timeR,10);prepare(e,s);
        std::vector<double> l(1500),r(1500);l[0]=1;r[0]=0.25;
        auto o=run(e,l,r);check(close(o.left[495],1) && close(o.right[495],0.25),"integer delay impulse plus PDC");
        check(energy(o.left,0,495)==0 && energy(o.left,496,1000)<1e-20,"wet only no direct leak");
        e.reset(ResetReason::explicitReset);e.applyTargets(s,0);o=run(e,std::vector<double>(1500),std::vector<double>(1500));
        check(energy(o.left,0,1500)==0,"O1 reset hides all old ring samples");
    }
    {
        DelayEngine e;auto s=init();set(s,route,2);set(s,mix,100);set(s,feedback,50);set(s,timeL,100);set(s,timeR,150);prepare(e,s);
        std::vector<double> l(24000),r(24000);l[0]=r[0]=1;
        auto o=run(e,l,r);
        check(close(o.left[4815],1) && close(o.right[4815],0),"Ping-Pong sums stereo to left first");
        check(energy(o.right,12015,300)>0.02 && energy(o.left,12015,300)<1e-10,"second echo right at L+R time");
        check(energy(o.left,16815,300)>0.001 && energy(o.right,16815,300)<1e-10,"third echo left");
        set(s,start,1);prepare(e,s);o=run(e,l,r);
        check(close(o.right[7215],1) && close(o.left[7215],0),"right start uses right delay");
        set(s,route,1);set(s,crossfeed,0);prepare(e,s);r.assign(r.size(),0);o=run(e,l,r);
        check(energy(o.right,0,o.right.size())==0,"Dual zero crossfeed isolates channels");
    }
    for(int ordinal:{4,11,18}){
        DelayEngine e;auto s=init();set(s,syncL,1);set(s,syncR,1);set(s,noteL,ordinal);set(s,noteR,ordinal);set(s,feedback,0);set(s,mix,100);prepare(e,s);
        ProcessContext c;c.tempoValid=true;c.bpm=120;
        std::size_t arrival=std::size_t(std::llround(noteBeats(ordinal)*24000))+driveLatency;
        std::vector<double> l(arrival+100),r(arrival+100);l[0]=1;auto o=run(e,l,r,c);
        check(close(o.left[arrival],1,1e-9),"120 BPM straight/dotted/triplet actual impulse");
    }
    {
        DelayEngine e;auto s=init();set(s,syncL,1);set(s,noteL,4);set(s,localTempo,100);set(s,mix,100);set(s,feedback,0);prepare(e,s);
        std::vector<double> l(30000),r(30000);l[0]=1;ProcessContext c;c.tempoValid=true;c.bpm=std::numeric_limits<double>::quiet_NaN();
        auto o=run(e,l,r,c);check(close(o.left[28815],1),"invalid host BPM uses saved fallback");
        Diagnostics d;check(e.readDiagnostics(d) && d.syncUnavailable && close(d.effectiveTimeL,600),"fallback diagnostics");
        set(s,noteL,13);set(s,localTempo,40);e.applyTargets(s,0);run(e,std::vector<double>(20000),std::vector<double>(20000));
        check(e.readDiagnostics(d) && d.syncClamped && close(d.effectiveTimeL,8000),"sync capacity clamp is measured");
        check(e.tailSamples().samples>48000*8,"tail not truncated to display length");
    }
    {
        // Output-only taps must never change main feedback; independent network copies.
        DelayEngine a,b;auto s=init();set(s,mix,100);set(s,timeL,20);set(s,timeR,20);prepare(a,s);
        set(s,tap3Enabled,1);set(s,tap3Level,-120);set(s,tap4Enabled,1);set(s,tap4Level,-120);prepare(b,s);
        std::vector<double> l(8000),r(8000);l[0]=0.8;
        auto x=run(a,l,r),y=run(b,l,r);check(x.left==y.left && x.right==y.right,"silent auxiliary taps leave network identical");
        prepare(a,s);set(s,tap3Level,0);set(s,tap3Time,50);set(s,tap3Pan,100);set(s,feedback,0);prepare(b,s);
        y=run(b,l,r);check(close(y.right[2415],0.8),"auxiliary tap correct independent arrival/pan");
    }
    {
        DelayEngine e;auto s=init();set(s,mix,100);set(s,timeL,2);set(s,timeR,2);set(s,feedback,95);prepare(e,s);
        std::vector<double> l(100000),r(100000);l[0]=1e100;r[0]=-1e100;l[1]=std::numeric_limits<double>::infinity();r[1]=std::numeric_limits<double>::quiet_NaN();
        auto o=run(e,l,r);for(auto v:o.left)check(std::isfinite(v) && std::abs(v)<100,"extreme source/feedback bounded finite");
        Diagnostics d;check(e.readDiagnostics(d) && d.protectionCount>0 && d.invalidInput,"protection activation observed by diagnostics");
        set(s,freeze,1);e.applyTargets(s,0);check(e.tailSamples().kind==TailKind::infinite,"Freeze infinite tail");
        o=run(e,std::vector<double>(480000),std::vector<double>(480000),{},2,2,3);
        for(auto v:o.left)check(std::isfinite(v) && std::abs(v)<100,"Freeze silence feedback remains finite");
        set(s,freeze,0);e.applyTargets(s,0);check(e.tailSamples().kind==TailKind::finite,"Freeze exit restores finite tail");
        check(value(s,feedback)==95,"Freeze never rewrites saved feedback");
    }
    {
        auto s=init();set(s,timeR,375);set(s,route,2);set(s,syncL,1);set(s,freeze,1);set(s,tap4Level,-19);
        auto bytes=encodeState(s,registry);SoundState restored;
        check(decodeState(bytes.data(),bytes.size(),{},registry,restored)==StateResult::ok && restored.targets==s.targets,"all hidden parameters preserved in complete state");
        auto before=restored;bytes.resize(bytes.size()-1);
        check(decodeState(bytes.data(),bytes.size(),{},registry,restored)==StateResult::malformed && before.targets==restored.targets,"malformed restore transactional");
        check(!simpleTimeEditable(s),"Custom Time locks route/sync/asymmetric state");
    }
    {
        struct Fake {
            SoundState sound=init();EditorViewState view{};unsigned begin=0,writes=0,end=0;
        } fake;
        EditorServices services{&fake,&fake.view,
            [](void* p,ParamID id){return static_cast<Fake*>(p)->sound.targets[registry.index(id)];},
            [](void* p,ParamID){++static_cast<Fake*>(p)->begin;return true;},
            [](void* p,ParamID id,double v){auto* f=static_cast<Fake*>(p);f->sound.targets[registry.index(id)]=v;++f->writes;return true;},
            [](void* p,ParamID){++static_cast<Fake*>(p)->end;}};
        {Gesture g(services);check(g.begin(timeL) && g.update(spec(timeL).toNormalized(500)),"paired Simple Time gesture");}
        check(value(fake.sound,timeL)==value(fake.sound,timeR) && fake.begin==2 && fake.writes==2 && fake.end==2,"paired gesture closes both host queues");
        set(fake.sound,timeR,375);
        {Gesture g(services);check(!g.begin(timeL),"unequal Time UI cannot overwrite hidden values");}
        fake.view.advanced=true;
        {Gesture g(services);check(g.begin(timeL) && g.update(spec(timeL).toNormalized(250)),"Advanced L/R independent edit");}
        check(close(value(fake.sound,timeR),375),"single L gesture preserves right time");
        fake.view.advanced=false;set(fake.sound,route,0);set(fake.sound,timeL,250);set(fake.sound,timeR,250);
        const unsigned initialWrites=fake.writes;
        {
            ControlServicesAdapter adapter(services,timeL);auto rotary=adapter.services();
            check(rotary.beginEdit(rotary.owner,timeL) && rotary.performEdit(rotary.owner,timeL,spec(timeL).toNormalized(450)),"shared rotary adapter edits paired Simple Time");
            rotary.endEdit(rotary.owner,timeL);
            check(value(fake.sound,timeL)==value(fake.sound,timeR) && fake.writes==initialWrites+2,"shared rotary emits both existing time queues");
            check(rotary.beginEdit(rotary.owner,timeL),"shared rotary begins fresh paired gesture");
            set(fake.sound,timeR,375);const unsigned writes=fake.writes;
            check(!rotary.performEdit(rotary.owner,timeL,.7) && fake.writes==writes,"host asymmetric change cancels drag without overwriting right target");
            check(fake.begin==fake.end,"adapter balances both queues on cancellation");
            check(!rotary.beginEdit(rotary.owner,timeR),"control cannot edit another parameter");
        }
        auto before=fake.sound;
        for(int n=0;n<20;++n){fake.view.advanced=!fake.view.advanced;auto state=editorTargets(services);check(state.targets==before.targets,"view reads never write/reset hidden state");}
    }
    {
        // Render exact same path with contiguous vs irregular blocks and in-place buffers.
        auto s=init();set(s,route,2);set(s,timeL,1.7);set(s,timeR,2.3);set(s,drive,12);set(s,modDepth,0.1);set(s,duckAmount,12);
        DelayEngine a,b;prepare(a,s);prepare(b,s);
        std::vector<double> l(8000),r(8000);for(std::size_t i=0;i<l.size();++i){l[i]=0.2*std::sin(0.017*i);r[i]=0.13*std::cos(0.043*i);}
        auto x=run(a,l,r,{},2,2,0,2048),y=run(b,l,r,{},2,2,0,17);
        check(x.left==y.left && x.right==y.right,"realtime/offline block segmentation identical");
        prepare(b,s);auto il=l,ir=r;
        AudioBlock<double> block{{il.data(),ir.data()},{il.data(),ir.data()},2,2,std::uint32_t(il.size()),0};
        realtime=true;b.process(block,{});b.endBlock();realtime=false;
        check(il==x.left && ir==x.right,"stereo in-place safe");
    }
    for(double rate:{44100.,48000.,96000.,192000.})for(int channels:{1,2})for(bool single:{false,true}){
        DelayEngine e;auto s=init();set(s,timeL,1);set(s,timeR,1);set(s,mix,100);set(s,feedback,95);prepare(e,s,rate,channels,channels);
        std::array<double,2048> ld{},rd{},od{},pd{};std::array<float,2048> lf{},rf{},of{},pf{};
        ld[0]=lf[0]=0.25;
        for(int size:{0,16,64,17,256,731,1024}){
            realtime=true;
            if(single)e.process(AudioBlock<float>{{lf.data(),rf.data()},{of.data(),pf.data()},unsigned(channels),unsigned(channels),unsigned(size),0},{});
            else e.process(AudioBlock<double>{{ld.data(),rd.data()},{od.data(),pd.data()},unsigned(channels),unsigned(channels),unsigned(size),0},{});
            e.endBlock();realtime=false;
        }
        for(double x:od)check(std::isfinite(x),"float64 matrix finite");for(float x:of)check(std::isfinite(x),"float32 matrix finite");
    }
    {
        // Freeze energy after the transition is held by integer reads and identity/swap.
        DelayEngine e;auto s=init();set(s,timeL,2);set(s,timeR,2);set(s,feedback,80);set(s,mix,100);prepare(e,s,8000);
        std::vector<double> source(8000),zero(8000);for(std::size_t i=0;i<source.size();++i)source[i]=0.15*std::sin(2*pi*500*i/8000);
        run(e,source,zero);set(s,freeze,1);e.applyTargets(s,0);run(e,source,zero);
        auto first=run(e,zero,zero),second=run(e,zero,zero);
        double a=energy(first.left,100,7900)+energy(first.right,100,7900),b=energy(second.left,100,7900)+energy(second.right,100,7900);
        check(a>1e-6 && std::abs(a-b)<a*1e-8,"settled Freeze preserves captured loop energy without growth");
    }
    {
        // Dense target/time/tempo changes at maximum feedback exercise safety beyond steady impulses.
        DelayEngine e;auto s=init();set(s,feedback,95);set(s,mix,100);set(s,drive,24);set(s,tap3Enabled,1);set(s,tap4Enabled,1);set(s,tap3Level,0);set(s,tap4Level,0);set(s,modDepth,10);prepare(e,s);
        std::array<double,64> source{},left{},right{};ProcessContext c;c.tempoValid=true;
        auto startClock=std::chrono::steady_clock::now();double worstUs=0;
        for(int block=0;block<1500;++block){
            for(int i=0;i<64;++i)source[i]=0.3*std::sin((block*64+i)*0.17);
            set(s,timeL,block%2?1:8000);set(s,timeR,block%3?3:7999);set(s,route,block%3);set(s,highPass,block%2?20:2000);set(s,highCut,block%2?500:20000);set(s,modRate,block%2?0.01:10);set(s,timeChange,block%2);c.bpm=block%3?40:240;
            auto before=std::chrono::steady_clock::now();
            realtime=true;e.applyTargets(s,block*64);e.process(AudioBlock<double>{{source.data(),source.data()},{left.data(),right.data()},2,2,64,0},c);e.endBlock();realtime=false;
            double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count();worstUs=std::max(worstUs,us);
            for(double v:left)check(std::isfinite(v) && std::abs(v)<1000,"dense time/route/filter automation bounded");
        }
        double totalMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-startClock).count();
        std::cout<<"Stress fixture 1500x64 @48k: total "<<totalMs<<"ms, slowest observed callback "<<worstUs<<"us; not a DAW scheduling guarantee\n";
    }
    check(allocations==0 && releases==0,"process/endBlock has zero C++ new/delete");
    std::cout<<"PASS Delay DSP: routing/impulses/sync/taps/Freeze/state/gestures/PDC, rates 44.1-192k float32/64, block invariance/in-place, RT allocations0\n";
}
