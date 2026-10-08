#include "plugins/fake_stereo/Dsp.hpp"
#include <iostream>
#include <cstdlib>
#include <new>
static thread_local bool realtime=false;
static unsigned allocations=0,releases=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
using namespace just;using namespace just::stereo;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
template<class T> static void render(Core& core,AudioBlock<T> b){realtime=true;core.process(b,{});realtime=false;}
static std::pair<double,double> expected(const Targets& t,double l,double r){
    l*=t.invertLeft?-1:1;r*=t.invertRight?-1:1;
    double m=t.inputMS?l:.5*l+.5*r,s=t.inputMS?r:.5*l-.5*r;
    m+=std::sin(t.asymmetry*M_PI/180)*s;
    const double angle=t.rotation*M_PI/180,mm=std::cos(angle)*m-std::sin(angle)*s;
    s=(std::sin(angle)*m+std::cos(angle)*s)*t.fieldWidth*(t.swap?-1:1)*(t.mono?0:1);
    return {mm+s,mm-s};
}
template<class T> static void test(double fs){
    std::array<T,128> l{},r{},ol{},orr{};l.fill(.6);r.fill(.2);
    AudioBlock<T> b;b.inputChannels=b.outputChannels=2;b.samples=128;b.inputs={l.data(),r.data()};b.outputs={ol.data(),orr.data()};
    const double tolerance=sizeof(T)==4?2e-6:1e-12;
    for(double width:{0.,1.,3.})for(double rotation:{-45.,0.,45.})for(double asymmetry:{-90.,0.,90.})for(unsigned route=0;route<16;++route){
        Core core;check(core.prepare({fs,128,2,2}),"prepare");Targets t;t.width=0;t.fieldWidth=width;t.rotation=rotation;t.asymmetry=asymmetry;t.inputMS=route&1;t.invertLeft=route&2;t.swap=route&4;t.mono=route&8;
        core.setTargets(t);render(core,b);auto e=expected(t,double(l[0]),double(r[0]));
        for(unsigned i=0;i<128;++i){check(std::abs(ol[i]-e.first)<tolerance && std::abs(orr[i]-e.second)<tolerance,"actual matrix, input decode and polarity match physical definition");if(width==0)check(ol[i]==orr[i],"Width0 removes ALL final side after any rotation and shear");}
    }
    Core generatedMono;generatedMono.prepare({fs,128,2,2});Targets collapsed;collapsed.width=200;collapsed.fieldWidth=0;collapsed.asymmetry=90;collapsed.rotation=-45;generatedMono.setTargets(collapsed);
    for(unsigned block=0;block<40;++block){render(generatedMono,b);check(ol==orr,"Width0 also clears the active old generator contribution after rotation");}
    Core smooth;check(smooth.prepare({fs,128,2,2}),"prepare transition");Targets t;t.width=0;smooth.setTargets(t);render(smooth,b);double priorL=ol.back(),priorR=orr.back();t.invertLeft=t.invertRight=t.inputMS=true;t.asymmetry=90;t.rotation=-45;t.fieldWidth=3;smooth.setTargets(t);
    double maxDelta=0;for(unsigned block=0;block<30;++block){render(smooth,b);for(unsigned i=0;i<128;++i){maxDelta=std::max({maxDelta,std::abs(double(ol[i])-priorL),std::abs(double(orr[i])-priorR)});priorL=ol[i];priorR=orr[i];}}
    check(maxDelta<.025,"polarity/mode/continuous transforms fade without a one-sample jump");
    t.bypass=true;smooth.setTargets(t);for(unsigned block=0;block<30;++block)render(smooth,b);
    check(ol==l && orr==r,"bypass returns original host LR with MS/polarity/width/rotation active");
    Core hot;hot.prepare({fs,128,2,2});t={};t.width=0;t.fieldWidth=3;t.rotation=-45;t.asymmetry=90;t.inputDb=t.outputDb=12;hot.setTargets(t);render(hot,b);
    check(std::abs(ol[0])>1 && std::isfinite(ol[0]) && std::isfinite(orr[0]),"strong transform remains finite and is not silently clipped/limited");
    Core mono,reference;mono.prepare({fs,128,1,2});reference.prepare({fs,128,1,2});Targets neutral;Targets ignored=neutral;ignored.inputMS=ignored.invertRight=true;mono.setTargets(ignored);reference.setTargets(neutral);
    b.inputChannels=1;std::array<T,128> rl{},rr{};auto rb=b;rb.outputs={rl.data(),rr.data()};render(mono,b);render(reference,rb);check(ol==rl && orr==rr,"mono input ignores saved MS/right polarity without changing saved targets");
}
int main(){for(double fs:{44100.,48000.,96000.,192000.}){test<float>(fs);test<double>(fs);}check(!allocations && !releases,"new transform path has zero realtime C++ allocation/release");std::cout<<"PASS "<<checks<<" transform checks: exact matrix, Width0, LR/MS, polarity, fades, raw bypass, no clipping, mono retained state\n";}
