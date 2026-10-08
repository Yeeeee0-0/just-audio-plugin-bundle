#include "Engine.hpp"
#include "EditorModel.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <new>
using namespace just;using namespace just::eq;
static bool realtime=false;
void* operator new(std::size_t n){if(realtime){std::cerr<<"RT allocation\n";std::abort();}if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static int checks=0;
static void check(bool ok,const char* s){++checks;if(!ok){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
static SoundState init(){return initialState({},registry);}
static void set(SoundState& s,std::size_t b,Field f,double v){auto i=index(b,f);s.targets[i]=parameters[i].toNormalized(v);}
static void band(SoundState& s,std::size_t b,Target t,double gainDb,Shape shape=Shape::bell){set(s,b,enabled,1);set(s,b,target,int(t));set(s,b,gain,gainDb);set(s,b,type,int(shape));}
struct Audio {
    std::vector<double> l,r,ol,orr,sc;
    explicit Audio(std::size_t n):l(n),r(n),ol(n),orr(n),sc(n){}
    AudioBlock<double> block(bool mono=false,bool external=false){return {{l.data(),r.data()},{ol.data(),orr.data()},mono?1u:2u,mono?1u:2u,static_cast<std::uint32_t>(l.size()),0,{external?sc.data():nullptr,nullptr},external?1u:0u,0};}
    void tones(double fs=48000){for(std::size_t i=0;i<l.size();++i){double m=0.2*std::sin(2*pi*1000*i/fs),s=0.13*std::sin(2*pi*2100*i/fs);l[i]=m+s;r[i]=m-s;sc[i]=std::sin(2*pi*1000*i/fs);}}
};
static double rms(const std::vector<double>& v,std::size_t start){double e=0;for(std::size_t i=start;i<v.size();++i)e+=v[i]*v[i];return std::sqrt(e/(v.size()-start));}
static void render(EqEngine& e,Audio& a,bool mono=false,bool sc=false){realtime=true;e.process(a.block(mono,sc),{});e.endBlock();realtime=false;}
static EqEngine* engine(const SoundState& s,double fs=48000,bool mono=false){auto* e=new EqEngine;check(e->prepare({fs,65536,mono?1u:2u,mono?1u:2u}),"prepare");realtime=true;e->applyTargets(s,0);realtime=false;return e;}
struct Sink {
    SoundState s=init();EditorViewState view;int writes=0,starts=0,ends=0;
    EditorServices services(){return {this,&view,[](void* p,ParamID id){auto& s=*static_cast<Sink*>(p);return s.s.targets[registry.index(id)];},[](void* p,ParamID){++static_cast<Sink*>(p)->starts;return true;},[](void* p,ParamID id,double v){auto& s=*static_cast<Sink*>(p);s.s.targets[registry.index(id)]=v;++s.writes;return true;},[](void* p,ParamID){++static_cast<Sink*>(p)->ends;}};}
};
int main(){
    check(registry.valid() && registry.count==195 && moduleDefinition().valid(),"183 legacy + 12 appended extension parameters/module");
    check(id(0,target)==102 && id(11,target)==454 && parameters[0].id==0,"literal stable field/bypass IDs");
    for(auto fs:{44100.,48000.,96000.,192000.}) {
        Audio a(32768);a.tones(fs);auto s=init();auto e=std::unique_ptr<EqEngine>(engine(s,fs));render(*e,a);
        check(a.l==a.ol && a.r==a.orr,"Init bit exact both channels/rates");
        for(int sh=0;sh<6;++sh)for(double hz:{20.,1000.,20000.})for(double qv:{0.1,0.7071067811865476,20.})for(double g:{-24.,0.,24.}) {
            FilterBank f;f.update(Shape(sh),hz,g,qv,4,fs);
            for(int j=0;j<f.count;++j){auto c=f.sections[j].c;check(std::abs(c.a2)<1 && 1+c.a1+c.a2>0 && 1-c.a1+c.a2>0,"endpoint poles inside unit circle");}
        }
    }
    for(auto t:{Target::stereo,Target::mid,Target::side}) {
        Audio a(32768);a.tones();auto s=init();band(s,0,t,6);auto e=std::unique_ptr<EqEngine>(engine(s));render(*e,a);
        double residual=0,midIn=0,midOut=0,sideIn=0,sideOut=0;
        for(std::size_t i=8192;i<a.l.size();++i) {
            double mi=(a.l[i]+a.r[i])*0.5,mo=(a.ol[i]+a.orr[i])*0.5,si=(a.l[i]-a.r[i])*0.5,so=(a.ol[i]-a.orr[i])*0.5;
            midIn+=mi*mi;midOut+=mo*mo;sideIn+=si*si;sideOut+=so*so;
            if(t==Target::mid)residual=std::max(residual,std::abs(si-so));if(t==Target::side)residual=std::max(residual,std::abs(mi-mo));
        }
        if(t!=Target::stereo)check(residual<2e-15,"opposite field untouched <-290 dB");
        if(t!=Target::side)check(std::abs(10*std::log10(midOut/midIn)-6)<0.002,"Mid tone +6 dB");
        FilterBank f;f.update(Shape::bell,1000,6,0.7071067811865476,1,48000);
        if(t!=Target::mid)check(std::abs(10*std::log10(sideOut/sideIn)-f.db(2100,48000))<0.01,"Side tone matches actual coefficients");
    }
    for(int sh=0;sh<6;++sh)for(int slopeV=0;slopeV<5;++slopeV) {
        Audio a(32768);for(std::size_t i=0;i<a.l.size();++i)a.l[i]=a.r[i]=std::sin(2*pi*2300*i/48000);
        auto s=init();band(s,0,Target::stereo,9,Shape(sh));set(s,0,slope,slopeV);auto e=std::unique_ptr<EqEngine>(engine(s));render(*e,a);
        FilterBank f;f.update(Shape(sh),1000,9,0.7071067811865476,slopeV,48000);
        check(std::abs(20*std::log10(rms(a.ol,8192)/rms(a.l,8192))-f.db(2300,48000))<0.01,"all shape/slope measured response agreement");
        check(a.ol==a.orr,"Stereo identical channel processing");
    }
    {
        Audio a(32768);a.tones();auto s=init();band(s,0,Target::side,24);auto e=std::unique_ptr<EqEngine>(engine(s,48000,true));render(*e,a,true);check(a.l==a.ol,"Side mono bit exact");
        s.targets[0]=1;s.targets[1]=parameters[1].toNormalized(24);s.targets[2]=parameters[2].toNormalized(-12);e.reset(engine(s));render(*e,a);check(a.l==a.ol && a.r==a.orr,"bypass skips input/output gains and all EQ");
    }
    {
        auto s=init();band(s,0,Target::mid,6);band(s,1,Target::side,-9);band(s,2,Target::stereo,3);Audio a(32768);a.tones();auto e=std::unique_ptr<EqEngine>(engine(s));render(*e,a);
        double im=0,om=0,is=0,os=0;
        for(std::size_t i=8192;i<a.l.size();++i){im+=std::pow((a.l[i]+a.r[i])/2,2);om+=std::pow((a.ol[i]+a.orr[i])/2,2);is+=std::pow((a.l[i]-a.r[i])/2,2);os+=std::pow((a.ol[i]-a.orr[i])/2,2);}
        check(std::abs(10*std::log10(om/im)-9)<0.002,"mixed Stereo+Mid cascade");
        FilterBank f,g;f.update(Shape::bell,1000,-9,0.7071067811865476,1,48000);g.update(Shape::bell,1000,3,0.7071067811865476,1,48000);
        check(std::abs(10*std::log10(os/is)-f.db(2100,48000)-g.db(2100,48000))<0.01,"mixed Stereo+Side cascade");
        for(int i=0;i<4000;++i){set(s,0,target,i%3);set(s,0,type,i%6);set(s,0,frequency,i%2?20:20000);set(s,0,q,i%2?0.1:20);realtime=true;e->applyTargets(s,i);e->process({{a.l.data(),a.r.data()},{a.ol.data(),a.orr.data()},2,2,1,0},{});realtime=false;check(std::isfinite(a.ol[0]) && std::isfinite(a.orr[0]),"rapid topology/frequency automation finite/no allocations");}
        Audio invalid(64);invalid.l[0]=std::numeric_limits<double>::quiet_NaN();invalid.r[1]=std::numeric_limits<double>::infinity();render(*e,invalid);for(auto v:invalid.ol)check(std::isfinite(v),"invalid inputs isolated");
    }
    {
        auto s=init();band(s,0,Target::stereo,0);set(s,0,dynamicEnabled,1);set(s,0,range,12);set(s,0,threshold,-30);set(s,0,knee,0);set(s,0,attack,0.5);set(s,0,release,10);
        Audio a(32768);a.tones();auto e=std::unique_ptr<EqEngine>(engine(s));render(*e,a);check(rms(a.ol,8192)<rms(a.l,8192)*0.6,"dynamic attenuation active");
        set(s,0,source,1);e.reset(engine(s));render(*e,a);check(a.ol==a.l && a.orr==a.r,"missing external SC does not fall back");render(*e,a,false,true);check(rms(a.ol,8192)<rms(a.l,8192)*0.6,"external SC attenuation");
        check(downwardGain(-12,-24,0,6)==-6 && downwardGain(-50,-24,6,6)==0,"bounded 2:1 detector law");
    }
    {
        const ParameterRegistry legacy{parameters,183};auto old=initialState({},legacy);
        old.seed=0x123456789;old.configurationCount=2;old.configurations[0]={77,4.5};old.configurations[1]={91,-2};
        for(std::size_t i=0;i<183;++i)old.targets[i]=double(i%17)/16;
        auto bytes=encodeState(old,legacy);SoundState loaded;
        check(decodeState(bytes.data(),bytes.size(),{},registry,loaded)==StateResult::ok,"decode exact 183-param legacy state");
        check(encodeState(loaded,legacy)==bytes,"all 183 hidden values, seed and configuration bytes preserved");
        for(std::size_t b=0;b<12;++b){check(physical(loaded,index(b,slopeExtension))==0,"missing extension defaults to Legacy");
            for(int v=0;v<5;++v){set(loaded,b,slope,v);check(effectiveSlope(loaded,b)==v,"old automation uses original five ordinals");}
            set(loaded,b,slopeExtension,2);for(int v=0;v<5;++v){set(loaded,b,slope,v);check(effectiveSlope(loaded,b)==6 && physical(loaded,index(b,slopeExtension))==2,"old slope automation cannot mutate explicit extension");}}
        Sink sink;EditorModel model;model.connect(sink.services());model.create(1000);model.write(type,3);model.write(q,2);
        for(int choice=0;choice<8;++choice){model.writeSlopeChoice(choice);check(model.slopeChoice()==choice,"eight UI slopes resolve to reviewed legacy/extension IDs");}
        model.writeSlopeChoice(6);auto savedQ=model.value(q);model.wheel(-1);check(model.slopeChoice()==5 && model.value(slopeExtension)==0 && model.value(slope)==4 && model.value(q)==savedQ,"cut wheel changes real slope and clears extension for legacy choice without Q edit");
        model.write(type,0);auto savedSlope=model.slopeChoice();model.wheel(2);check(model.value(q)>savedQ && model.slopeChoice()==savedSlope,"non-cut wheel changes Q only");
        model.write(slopeExtension,3);check(model.duplicate() && model.value(slopeExtension)==3,"duplicate retains complete extension state");
        model.write(enabled,0);model.create(900);check(model.value(slopeExtension)==0,"new band resets extension to Legacy with frozen defaults");
        check(sink.starts==sink.ends,"slope and wheel gestures balanced");
    }
    for(double fs:{44100.,48000.,96000.,192000.}){
        auto state=init();band(state,0,Target::stereo,0,Shape::highPass);auto e=std::unique_ptr<EqEngine>(engine(state,fs));
        Audio a(64);a.tones(fs);
        for(int n=0;n<1200;++n){set(state,0,type,n%2?3:4);set(state,0,slopeExtension,n%4);set(state,0,slope,n%5);
            realtime=true;e->applyTargets(state,0);e->process(a.block(),{});realtime=false;
            for(double sample:a.ol)check(std::isfinite(sample) && std::abs(sample)<4,"extended slopes switch with bounded crossfade and no RT allocation");}
    }
    {
        auto s=init();for(std::size_t i=0;i<registry.count;++i)s.targets[i]=(i%13)/12.0;auto bytes=encodeState(s,registry);SoundState restored;check(decodeState(bytes.data(),bytes.size(),{},registry,restored)==StateResult::ok && restored.targets==s.targets,"all hidden/field parameters round-trip");
        auto oldRegistry=ParameterRegistry{parameters,1};auto old=encodeState(init(),oldRegistry);check(decodeState(old.data(),old.size(),{},registry,restored)==StateResult::ok && restored.targets==init().targets,"foundation state fills new Init");
        Sink sink;EditorModel model;model.connect(sink.services());model.create(800);auto before=sink.s;int writes=sink.writes;
        check(model.value(gain)==0 && model.value(type)==0 && model.value(target)==0 && model.value(dynamicEnabled)==0 && std::abs(model.value(q)-0.7071067811865476)<1e-14,"empty-space creation is neutral Bell/Stereo with default Q/dynamics");
        for(int i=0;i<20;++i){sink.view.advanced=!sink.view.advanced;model.refresh();model.response(1000);}check(sink.writes==writes && sink.s.targets==before.targets,"view/paint/selection zero sound writes");
        model.hidePanel();model.refresh();check(!model.panelVisible && sink.s.targets==before.targets && sink.writes==writes,"hiding local panel never resets hidden sound values");model.select(0);check(model.panelVisible && sink.s.targets==before.targets,"reselecting node is view only");
        model.write(target,2);check(physical(sink.s,index(0,target))==2 && physical(sink.s,index(1,target))==0,"field choice edits selected band only");
        model.beginDrag(0);model.drag(1500,-2);model.cancelDrag();check(sink.starts==sink.ends,"complete/cancelled host gestures");
        check(model.parse(frequency,"1.5k") && std::abs(model.value(frequency)-1500)<1e-9,"unit text entry");
        for(int f=0;f<3;++f)for(int g=f+1;g<3;++g)check(fieldStyles[f].rgb!=fieldStyles[g].rgb && fieldStyles[f].dash!=fieldStyles[g].dash && std::strcmp(fieldStyles[f].node,fieldStyles[g].node),"three independent visual cues");
    }
    {
        auto s=init();band(s,0,Target::mid,4);band(s,1,Target::side,-3);set(s,0,dynamicEnabled,1);
        Audio full(16384),split(16384);full.tones();split.tones();auto a=std::unique_ptr<EqEngine>(engine(s)),b=std::unique_ptr<EqEngine>(engine(s));render(*a,full);
        std::size_t at=0,n=0;const std::size_t sizes[]={16,32,64,128,1024,37};
        while(at<split.l.size()) {auto count=std::min(sizes[n++%6],split.l.size()-at);AudioBlock<double> block{{split.l.data()+at,split.r.data()+at},{split.ol.data()+at,split.orr.data()+at},2,2,static_cast<std::uint32_t>(count),0};realtime=true;b->applyTargets(s,0);b->process(block,{});b->endBlock();realtime=false;at+=count;}
        check(full.ol==split.ol && full.orr==split.orr,"variable block boundaries preserve deterministic dynamic/field audio");
        std::array<float,1024> l,r,ol{},orr{},il,ir;for(std::size_t i=0;i<l.size();++i){l[i]=float(std::sin(i*0.11));r[i]=float(std::cos(i*0.13));}il=l;ir=r;
        a.reset(engine(s));b.reset(engine(s));AudioBlock<float> out{{l.data(),r.data()},{ol.data(),orr.data()},2,2,1024,0},inplace{{il.data(),ir.data()},{il.data(),ir.data()},2,2,1024,0};
        realtime=true;a->process(out,{});b->process(inplace,{});realtime=false;
        check(il==ol && ir==orr,"float32 in-place processing equals separate buffers");
        s.targets[1]=parameters[1].toNormalized(24);s.targets[2]=parameters[2].toNormalized(24);a.reset(engine(s));l.fill(std::numeric_limits<float>::max());r=l;
        realtime=true;a->process(out,{});realtime=false;check(std::all_of(ol.begin(),ol.end(),[](float x){return std::isfinite(x);}),"float32 representational overflow is isolated");
    }
    {
        // Bypass must complete its declared 5ms ramp, without being restarted by
        // identical targets delivered at every sample by a host automation lane.
        for(double fs:{44100.,48000.,96000.,192000.}){
            auto s=init();s.targets[1]=parameters[1].toNormalized(6);auto e=std::unique_ptr<EqEngine>(engine(s,fs));
            double input=0.25,left=0,right=0;AudioBlock<double> one{{&input,&input},{&left,&right},2,2,1,0};
            int samples=int(std::lround(fs*0.005));s.targets[0]=1;double initial=0.25*std::pow(10.0,6/20.0),last=initial;
            for(int n=0;n<samples;++n){realtime=true;e->applyTargets(s,n);e->process(one,{});realtime=false;double expected=initial+(input-initial)*double(n+1)/samples;check(std::abs(left-expected)<2e-14,"5ms bypass waveform ramp");check(std::abs(left-last)<=std::abs(initial-input)/samples+2e-14,"bypass has no extra one-sample jump");last=left;}
            check(e->observe(0).wet==0 && left==input && right==input,"bypass is exactly dry at declared duration");
            s.targets[0]=0;for(int n=0;n<samples;++n){realtime=true;e->applyTargets(s,n);e->process(one,{});realtime=false;}check(e->observe(0).wet==1 && std::abs(left-initial)<1e-14,"unbypass exact endpoint");
            s.targets[0]=1;realtime=true;e->applyTargets(s,0);e->process(one,{});realtime=false;double previous=e->observe(0).wet;s.targets[0]=0;realtime=true;e->applyTargets(s,1);e->process(one,{});realtime=false;check(std::abs(e->observe(0).wet-(previous+(1-previous)/samples))<1e-14,"bypass reversal starts from current ramp value");
        }
    }
    {
        // Observe the actual control values consumed by render, and also compare
        // the audible automation transient with an unchanged processor.
        for(double fs:{44100.,48000.,96000.,192000.}){
            auto s=init();band(s,0,Target::stereo,0);set(s,0,dynamicEnabled,1);set(s,0,range,18);set(s,0,threshold,-80);set(s,0,knee,24);set(s,0,attack,0.5);set(s,0,release,10);
            auto actual=std::unique_ptr<EqEngine>(engine(s,fs)),reference=std::unique_ptr<EqEngine>(engine(s,fs));
            double input=0,left=0,right=0,refLeft=0,refRight=0;AudioBlock<double> one{{&input,&input},{&left,&right},2,2,1,0},refOne{{&input,&input},{&refLeft,&refRight},2,2,1,0};
            int warm=int(fs/5),span=int(std::lround(fs*0.01));
            for(int n=0;n<warm;++n){input=0.2*std::sin(2*pi*1000*n/fs);actual->process(one,{});reference->process(refOne,{});}
            set(s,0,range,0);set(s,0,threshold,0);set(s,0,knee,0);set(s,0,attack,200);set(s,0,release,2000);double maxDeltaStep=0,previousDelta=0,firstDelta=0;
            for(int n=0;n<span;++n){input=0.2*std::sin(2*pi*1000*(warm+n)/fs);realtime=true;actual->applyTargets(s,n);actual->process(one,{});reference->process(refOne,{});realtime=false;
                auto v=actual->observe(0);double decay=std::exp(-double(n+1)/(fs*0.01));
                check(std::abs(v.range-18*decay)<2e-11 && std::abs(v.threshold+80*decay)<2e-11 && std::abs(v.knee-24*decay)<2e-11,"dynamic dB controls follow declared 10ms one-pole under repeated events");
                check(std::abs(std::log(v.attack)-(std::log(200)+(std::log(0.5)-std::log(200))*decay))<2e-12 && std::abs(std::log(v.release)-(std::log(2000)+(std::log(10)-std::log(2000))*decay))<2e-12,"Attack/Release controls smooth logarithmically for 10ms");
                check(v.dynamicGain>=-v.range-1e-12 && v.dynamicGain<=0,"dynamic gain stays inside smoothed Range");
                double delta=left-refLeft;if(n==0)firstDelta=std::abs(delta);maxDeltaStep=std::max(maxDeltaStep,std::abs(delta-previousDelta));previousDelta=delta;
            }
            check(firstDelta<0.001 && maxDeltaStep<0.05,"simultaneous extreme control automation has bounded audible transient");
            std::cout<<"DYNAMIC_STEP fs="<<fs<<" first_delta="<<firstDelta<<" max_delta_step="<<maxDeltaStep<<"\n";
        }
    }
    {
        auto s=init();for(std::size_t b=0;b<12;++b){band(s,b,Target(int(b%3)),3);set(s,b,dynamicEnabled,1);set(s,b,frequency,100+100*b);}
        auto e=std::unique_ptr<EqEngine>(engine(s));Audio a(128);a.tones();std::vector<double> us;
        for(int n=0;n<1200;++n){auto start=std::chrono::steady_clock::now();render(*e,a);double t=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();if(n>=200)us.push_back(t);}
        std::sort(us.begin(),us.end());std::cout<<"CPU 48k/128/12 dynamic bands p99_us="<<us[us.size()*99/100]<<" block_us=2666.667\n";
    }
    std::cout<<"PASS "<<checks<<" EQ checks\n";
}
