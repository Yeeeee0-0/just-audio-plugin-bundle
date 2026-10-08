#include "ReverbEngine.hpp"
#include "Presets.hpp"
#include <iostream>
#include <random>
#include <limits>
#include <cstdlib>
using namespace just;using namespace just::reverb;
static thread_local bool realtime=false;
static unsigned allocations=0,deallocations=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++deallocations;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct WetCapture final:AnalysisTap {
    std::array<EffectAnalysisSample,1024> samples{};std::uint32_t count=0;
    void pushSample(std::uint32_t offset,const EffectAnalysisSample& sample) noexcept override {
        if(offset<samples.size())samples[offset]=sample;
        ++count;
    }
};
static void run(ReverbEngine& e,std::vector<double>& l,std::vector<double>& r,std::vector<double>& a,std::vector<double>& b,ProcessContext context={},bool silence=false,unsigned ins=2,unsigned outs=2) {
    AudioBlock<double> block{{l.data(),r.data()},{a.data(),b.data()},ins,outs,std::uint32_t(l.size()),silence?3ull:0ull};
    realtime=true;e.process(block,context);e.endBlock();realtime=false;
}
static std::vector<double> impulse(SoundState state,double fs,double duration) {
    ReverbEngine e;check(e.prepare({fs,256,2,2,0,SampleFormat::float64,true}),"prepare impulse");e.applyTargets(state,0);
    std::vector<double> result(std::size_t(fs*duration)),l(256),r(256),a(256),b(256);
    for(std::size_t at=0;at<result.size();at+=256) {
        std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);if(at==0)l[0]=r[0]=1;
        run(e,l,r,a,b,{},at!=0);
        for(std::size_t i=0;i<std::min<std::size_t>(256,result.size()-at);++i)result[at+i]=a[i];
    }
    return result;
}
static double rt30(const std::vector<double>& ir,double fs) {
    std::vector<double> energy(ir.size());double sum=0;
    for(std::size_t i=ir.size();i-->0;){sum+=ir[i]*ir[i];energy[i]=sum;}
    check(sum>1e-10,"wet impulse exists");std::size_t t5=0,t35=0;
    for(std::size_t i=0;i<ir.size();++i){double level=10*std::log10(std::max(energy[i]/sum,1e-30));if(!t5 && level<=-5)t5=i;if(level<=-35){t35=i;break;}}
    return 2*double(t35-t5)/fs;
}
int main() {
    check(registry.valid(),"registry valid");check(registry.count==36,"explicit complete registry size");
    check(parameters[registry.index(Decay)].initial==1.2 && parameters[registry.index(Mix)].initial==20,"V2 Init");
    for(std::size_t p=0;p<std::size(presets);++p) {
        auto state=presetState(p);auto bytes=encodeState(state,registry);SoundState decoded;
        check(decodeState(bytes.data(),bytes.size(),state.plugin,registry,decoded)==StateResult::ok,"preset state roundtrip");
        check(decoded.targets==state.targets && matchingPreset(decoded)==int(p),"complete hidden preset targets");
        SoundState built;check(factoryPresets[p].build(state.plugin,built) && sameSoundState(built,state,registry),"native factory catalog restores complete preset");
    }
    {
        ReverbEngine measured,reference;SoundState state=presetState(0);set(state,Mix,100);set(state,Predelay,0);set(state,ModDepth,0);
        check(measured.prepare({48000,256}) && reference.prepare({48000,256}),"analysis pair prepare");
        measured.applyTargets(state,0);reference.applyTargets(state,0);
        std::vector<double> in(256),right(256),outL(256),outR(256),refL(256),refR(256);in[0]=right[0]=.5;
        WetCapture capture;ProcessContext context;context.analysis=&capture;
        bool sawTail=false;
        for(int block=0;block<100;++block){
            capture.count=0;run(measured,in,right,outL,outR,context,block>0);
            run(reference,in,right,refL,refR,{},block>0);
            check(outL==refL && outR==refR && capture.count==256,"analysis keeps sounding samples bit-identical");
            for(unsigned i=0;i<256;++i){
                check(capture.samples[i].validFields&analysisWet,"actual wet field valid");
                if(block>0){check(std::abs(capture.samples[i].wet[0]-outL[i])<1e-12,"wet tail equals actual fully wet output");sawTail|=std::abs(outL[i])>1e-10;}
            }
            in[0]=right[0]=0;
        }
        check(sawTail,"captured tail persists after input silence");
        set(state,Mix,0);measured.applyTargets(state,0);
        for(int block=0;block<20;++block){capture.count=0;run(measured,in,right,outL,outR,context,true);}
        for(unsigned i=0;i<256;++i)check(capture.samples[i].wet[0]==0 && capture.samples[i].wet[1]==0,"Mix zero publishes zero wet contribution");
    }
    ReverbEngine e;check(!e.prepare({0}),"invalid prepare rejected");check(e.prepare({48000,1024}),"valid prepare");
    auto s=presetState(0);set(s,Mix,0);e.applyTargets(s,0);
    std::vector<double> l(1024),r(1024),a(1024),b(1024);
    for(unsigned i=0;i<1024;++i){l[i]=std::sin(i*.13)*.2;r[i]=std::cos(i*.19)*.2;}
    run(e,l,r,a,b);check(a==l && b==r,"zero mix exact dry");
    const auto originalL=l,originalR=r;
    realtime=true;e.process(AudioBlock<double>{{l.data(),r.data()},{l.data(),r.data()},2,2,1024},{});realtime=false;
    check(l==originalL && r==originalR,"exact in-place dry endpoint");
    set(s,Mix,100);set(s,Width,0);e.applyTargets(s,0);
    for(int n=0;n<10;++n)run(e,l,r,a,b);
    check(a==b,"width zero strict wet mono");
    set(s,WetLevel,-90);e.applyTargets(s,0);for(int n=0;n<3;++n)run(e,l,r,a,b);
    for(double v:a)check(v==0,"finite wet minimum is exact mute");
    set(s,WetLevel,-89.9995);e.applyTargets(s,0);for(int n=0;n<3;++n)run(e,l,r,a,b);
    check(std::any_of(a.begin(),a.end(),[](double v){return std::abs(v)>1e-12;}),"values above wet sentinel remain ordinary dB");
    set(s,WetLevel,0);
    set(s,Bypass,1);set(s,InputTrim,12);set(s,OutputTrim,-12);e.applyTargets(s,0);
    for(int n=0;n<3;++n)run(e,l,r,a,b);
    check(a==l && b==r,"bypass returns original excluding trims");
    e.reset(ResetReason::explicitReset);s=presetState(0);set(s,Mix,100);set(s,Predelay,100);set(s,ModDepth,0);e.applyTargets(s,0);e.reset(ResetReason::explicitReset);
    auto ir=impulse(s,48000,2);std::size_t first=0;while(first<ir.size() && std::abs(ir[first])<1e-18)++first;
    check(first>=4800 && first<5000,"pre-delay and first ER arrival");
    auto shorter=s;set(shorter,Predelay,0);auto ir0=impulse(shorter,48000,2);
    for(std::size_t i=0;i<ir0.size()-4800;++i)check(std::abs(ir0[i]-ir[i+4800])<1e-12,"integer predelay shifts wet exactly");
    for(int style=0;style<3;++style)for(double decay:{.3,1.2,3.5,10.}) {
        auto rt=presetState(0);set(rt,Mix,100);set(rt,Style,style);set(rt,EarlyLate,100);set(rt,Predelay,0);set(rt,ModDepth,0);
        set(rt,Decay,decay);set(rt,LowDecay,1);set(rt,HighDecay,1);
        auto wave=impulse(rt,48000,decay*2+1);double measured=rt30(wave,48000);
        std::cout<<"RT60 style="<<style<<" target="<<decay<<" measured="<<measured<<'\n';
        check(std::abs(measured-decay)/decay<.18,"neutral fullband T30 decay tolerance");
    }
    for(double fs:{44100.,48000.,96000.,192000.}) {
        check(e.prepare({fs,1024,1,2}),"sample rate mono-to-stereo setup");s=presetState(0);set(s,Mix,100);e.applyTargets(s,0);
        std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);l[0]=1;
        run(e,l,r,a,b,{},false,1,2);l[0]=0;
        bool hasTail=false;
        for(int k=0;k<25;++k){run(e,l,r,a,b,{},true,1,2);for(auto v:a){check(std::isfinite(v),"silence flag finite tail");hasTail|=std::abs(v)>1e-10;}}
        check(hasTail,"silence flags preserve tails");
    }
    std::mt19937 random(173);std::uniform_real_distribution<double> unit(0,1);
    check(e.prepare({48000,1024}),"stress setup");s=presetState(0);e.applyTargets(s,0);
    for(int n=0;n<400;++n) {
        for(std::size_t i=0;i<registry.count;++i)s.targets[i]=unit(random);
        s.targets[0]=0;e.applyTargets(s,0);
        for(std::size_t i=0;i<l.size();++i){l[i]=(unit(random)-.5)*.1;r[i]=(unit(random)-.5)*.1;}
        if(n==3){l[7]=std::numeric_limits<double>::quiet_NaN();r[8]=std::numeric_limits<double>::infinity();}
        ProcessContext c;c.tempoValid=n%3;c.bpm=n%7?40+200*unit(random):0;
        run(e,l,r,a,b,c);for(auto v:a)check(std::isfinite(v),"random targets finite");for(auto v:b)check(std::isfinite(v),"random right finite");
    }
    check(e.latencySamples()==0,"zero PDC");set(s,Freeze,1);e.applyTargets(s,0);check(e.tailSamples().kind==TailKind::infinite,"freeze infinite host tail");
    // Freeze ignores new wet input after its ramp and does not grow without
    // input. Compare two identically prepared engines fed different dry audio.
    ReverbEngine frozenA,frozenB;frozenA.prepare({48000,1024});frozenB.prepare({48000,1024});
    auto frozenState=presetState(5);set(frozenState,Mix,100);set(frozenState,Freeze,1);frozenA.applyTargets(frozenState,0);frozenB.applyTargets(frozenState,0);
    std::vector<double> zero(1024),different(1024,.2),fa64(1024),fb64(1024),ra64(1024),rb64(1024);
    for(int n=0;n<120;++n){run(frozenA,zero,zero,fa64,ra64);run(frozenB,different,different,fb64,rb64);check(fa64==fb64 && ra64==rb64,"fully frozen network rejects new wet input");}
    frozenState=presetState(5);set(frozenState,Mix,100);set(frozenState,ModDepth,0);frozenA.applyTargets(frozenState,0);frozenA.reset(ResetReason::explicitReset);
    zero[0]=1;for(int n=0;n<40;++n){run(frozenA,zero,zero,fa64,ra64);zero[0]=0;}
    set(frozenState,Freeze,1);frozenA.applyTargets(frozenState,0);double maxEnergy=0,lateEnergy=0;
    for(int n=0;n<600;++n) {run(frozenA,zero,zero,fa64,ra64,{},true);double energy=0;for(auto v:fa64)energy+=v*v;maxEnergy=std::max(maxEnergy,energy);if(n>500)lateEnergy=std::max(lateEnergy,energy);}
    check(maxEnergy<.1 && lateEnergy<=maxEnergy,"freeze remains bounded over twelve seconds");
    frozenA.reset(ResetReason::explicitReset);run(frozenA,zero,zero,fa64,ra64,{},true);for(auto v:fa64)check(v==0,"explicit reset invalidates all ring/filter history");
    set(s,Sync,1);set(s,Division,11);e.applyTargets(s,0);ProcessContext c;c.tempoValid=true;c.bpm=10;run(e,l,r,a,b,c);
    check(e.effectiveValues().syncClamped && e.effectiveValues().predelayMs==4000,"sync capacity clamps");
    c.tempoValid=false;run(e,l,r,a,b,c);check(e.effectiveValues().syncUnavailable,"missing tempo fallback");
    std::vector<float> fl(32,.1f),fr(32,.2f),fa(32),fb(32);AudioBlock<float> f{{fl.data(),fr.data()},{fa.data(),fb.data()},2,2,32};
    realtime=true;e.process(f,{});e.reset(ResetReason::seek);e.process(AudioBlock<float>{{fl.data(),fr.data()},{fa.data(),fb.data()},2,2,0},{});realtime=false;
    for(auto v:fa)check(std::isfinite(v),"float path");
    check(allocations==0 && deallocations==0,"all audio/reset operations allocation free");
    std::cout<<"PASS 14 presets, complete state, endpoints, ER timing, RT60, rates, tails, sync, random automation, float/double and RT allocation guard\n";
}
