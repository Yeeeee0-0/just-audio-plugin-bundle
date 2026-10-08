#include "plugins/fake_stereo/Dsp.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <new>
#include <fstream>
using namespace just;
static thread_local bool realtime=false;
static unsigned allocations=0,deallocations=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++deallocations;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
static void check(bool ok,const char* message){if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static std::uint64_t seed=20261002;
static double noise(){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return double(seed>>11)/9007199254740992.0*2-1;}
template<class T> struct Buffer {
    std::array<T,2048> l{},r{},yl{},yr{};
    AudioBlock<T> block(unsigned ni=2,unsigned no=2,unsigned n=2048,bool inPlace=false){
        return {{{l.data(),ni==2?r.data():nullptr}},{{inPlace?l.data():yl.data(),no==2?(inPlace?r.data():yr.data()):nullptr}},ni,no,n};
    }
};
template<class T> void matrix() {
    Buffer<T> b;
    double worst=0;unsigned cases=0;
    for(auto fs:{44100.,48000.,96000.,192000.})for(unsigned ni:{1u,2u})for(unsigned no:{1u,2u}) {
        if(ni==2 && no==1)continue;
        for(double w:{0.,60.,200.})for(double k:{0.,100.,200.})for(double m:{0.,37.,100.})for(bool character:{false,true})
        for(unsigned stimulus=0;stimulus<5;++stimulus)for(auto trims:{std::array<double,2>{0,0},{-24,12},{12,-24},{12,12}}) {
            stereo::Core core;check(core.prepare({fs,2048,ni,no}),"prepare layouts/rates");
            stereo::Targets t;t.width=w;t.existingSide=k;t.mix=m;t.diffuse=character;t.inputDb=trims[0];t.outputDb=trims[1];
            core.setTargets(t);
            const double gain=std::pow(10.,t.inputDb/20)*std::pow(10.,t.outputDb/20);
            unsigned position=0;
            for(unsigned n:{0u,16u,17u,32u,64u,128u,256u,731u,1024u,2048u}) {
                for(unsigned i=0;i<n;++i) {
                    const auto sample=position+i;
                    double left=noise()*4,right=noise()*4;
                    if(stimulus==1)right=left; // Identical double mono.
                    if(stimulus==2)right=-left; // Existing anti-phase Side only.
                    if(stimulus==3){left=sample==31?4:0;right=sample==79?-2:0;}
                    if(stimulus==4) {
                        const double duration=.12,time=sample/fs,ratio=std::min(20000.,.45*fs)/20.;
                        const double phase=2*3.14159265358979323846*20*duration*(std::pow(ratio,time/duration)-1)/std::log(ratio);
                        left=4*std::sin(phase);right=2*std::sin(phase+.7);
                    }
                    b.l[i]=T(left);b.r[i]=T(right);
                }
                realtime=true;core.process(b.block(ni,no,n),{});realtime=false;
                for(unsigned i=0;i<n;++i) {
                    const double mid=gain*(ni==1?double(b.l[i]):.5*double(b.l[i])+.5*double(b.r[i]));
                    const double folded=no==1?double(b.yl[i]):.5*double(b.yl[i])+.5*double(b.yr[i]);
                    const double relative=std::abs(mid-folded)/std::max({1.,std::abs(mid),std::abs(double(b.yl[i])),std::abs(double(b.yr[i]))});
                    worst=std::max(worst,relative);
                    check(relative<(sizeof(T)==4?1e-6:1e-12),"normalized Mid identity");
                    if(trims[0]==0 && trims[1]==0 && (m==0 || (w==0 && k==100))) {
                        check(std::abs(double(b.yl[i])-double(b.l[i]))<1e-6,"dry left");
                        if(no==2)check(std::abs(double(b.yr[i])-double(ni==1?b.l[i]:b.r[i]))<1e-6,"dry right/layout");
                    }
                }
                position+=n;
            }
            ++cases;
        }
    }
    std::cout<<"PASS "<<(sizeof(T)==4?"float32":"float64")<<" matrix "<<cases<<" cases (noise/double-mono/anti-phase/impulse/sweep, four trim pairs); worst normalized Mid error "<<worst<<'\n';
}
void inPlaceAndIsolation() {
    Buffer<double> b;stereo::Core a,c;check(a.prepare({}) && c.prepare({}),"prepare inplace");
    for(unsigned i=0;i<2048;++i){b.l[i]=noise();b.r[i]=noise();}
    auto origL=b.l,origR=b.r;
    a.setTargets({});c.setTargets({});a.process(b.block(),{});c.process(b.block(2,2,2048,true),{});
    check(b.l==b.yl && b.r==b.yr,"in-place equals out-of-place");
    b.l=origL;b.r=origR;b.l[7]=std::numeric_limits<double>::quiet_NaN();b.r[9]=std::numeric_limits<double>::infinity();
    a.reset(ResetReason::explicitReset);a.process(b.block(),{});
    for(unsigned i=0;i<2048;++i)check(std::isfinite(b.yl[i]) && std::isfinite(b.yr[i]),"NaN/Inf isolated");
    check(a.telemetry.invalidInput,"invalid input telemetry");
    a.reset(ResetReason::explicitReset);c.reset(ResetReason::explicitReset);b.l.fill(1);b.r.fill(1);
    auto silent=b.block();silent.inputSilenceFlags=3;a.process(silent,{});
    const auto silentLeft=b.yl,silentRight=b.yr;
    b.l.fill(0);b.r.fill(0);c.process(b.block(),{});
    check(silentLeft==b.yl && silentRight==b.yr,"silent flags equal actual zero input");
    std::cout<<"PASS inplace, nonfinite isolation, silence flags\n";
}
void sideIntegrityAndHighOff() {
    Buffer<double> b;stereo::Targets t;t.width=200;t.existingSide=130;t.highEnabled=true;t.lowHz=500;t.highHz=1000;
    for(double fs:{44100.,48000.,96000.,192000.}) {
        stereo::Core side;check(side.prepare({fs,2048,2,2}),"original Side prepare");side.setTargets(t);
        for(unsigned i=0;i<2048;++i){b.l[i]=noise();b.r[i]=-b.l[i];}
        side.process(b.block(),{});
        for(unsigned i=0;i<2048;++i)check(std::abs(b.yl[i]-1.3*b.l[i])<1e-12 && std::abs(b.yr[i]-1.3*b.r[i])<1e-12,"original Side never enters generated-side filters");
        stereo::Core a,c;check(a.prepare({fs,2048,1,2}) && c.prepare({fs,2048,1,2}),"HighCut Off prepare");
        t={};t.highEnabled=false;t.highHz=1000;a.setTargets(t);t.highHz=20000;c.setTargets(t);
        for(unsigned block=0;block<10;++block) {
            for(unsigned i=0;i<2048;++i)b.l[i]=noise();
            a.process(b.block(1,2),{});const auto left=b.yl,right=b.yr;c.process(b.block(1,2),{});
            check(left==b.yl && right==b.yr,"High Cut Off is exact LP bypass independent of retained cutoff/Fs");
        }
        t.width=200;t.existingSide=130;t.highEnabled=true;t.lowHz=500;t.highHz=1000;
    }
    std::cout<<"PASS untouched original Side and exact HighCut Off at four rates\n";
}
template<class T> void sampleFormatOverflow() {
    Buffer<T> b;stereo::Core core;check(core.prepare({}),"sample format overflow prepare");
    stereo::Targets t;t.inputDb=t.outputDb=12;t.width=t.existingSide=200;core.setTargets(t);
    b.l.fill(std::numeric_limits<T>::max());b.r.fill(-std::numeric_limits<T>::max());
    realtime=true;core.process(b.block(),{});realtime=false;
    for(unsigned i=0;i<2048;++i)check(std::isfinite(b.yl[i]) && std::isfinite(b.yr[i]),"finite sample-format limits after maximum gain");
    check(core.telemetry.invalidInput,"unrepresentable output is reported");
    b.l.fill(0);b.r.fill(0);core.process(b.block(),{});
    for(unsigned i=0;i<2048;++i)check(std::isfinite(b.yl[i]) && std::isfinite(b.yr[i]),"format overflow does not poison subsequent silence");
    std::cout<<"PASS "<<(sizeof(T)==4?"float32":"float64")<<" unrepresentable output isolation\n";
}
void frequencyAndTail(const char* logPath) {
    Buffer<double> b;stereo::Core core;stereo::Targets t;t.width=100;
    check(core.prepare({48000,2048,1,2}),"low protect prepare");core.setTargets(t);
    double midPower=0,sidePower=0;unsigned position=0;
    for(unsigned block=0;block<100;++block) {
        for(unsigned i=0;i<2048;++i)b.l[i]=std::sin(2*3.14159265358979323846*50*(position++)/48000);
        core.process(b.block(1,2),{});
        if(block>20)for(unsigned i=0;i<2048;++i){const double side=.5*b.yl[i]-.5*b.yr[i];sidePower+=side*side;midPower+=b.l[i]*b.l[i];}
    }
    check(sidePower>midPower*1e-8,"generated Side is present, not a pass-through placeholder");
    const double db=10*std::log10(sidePower/midPower);check(db<=-18,"50Hz generated Side <= -18dB");
    std::cout<<"PASS low protection 50Hz generated Side/Mid "<<db<<" dB\n";
    std::ofstream log;if(logPath){log.open(logPath);check(bool(log),"tail log open");
        log<<"fs,character,block,low_hz,high_enabled,high_hz,input_db,output_db,width,coalescing,last_nonzero,start,end_exclusive,observed_samples,peak\n";log.precision(17);}
    double worst=0;unsigned cases=0;
    // Explicit absolute clock: last nonzero input is sample 31. Observe EVERY
    // sample from last+ceil(2*Fs) up to last+ceil(3*Fs), including partial blocks.
    for(double fs:{44100.,48000.,96000.,192000.})for(bool character:{false,true})
    for(unsigned blockSize:{16u,17u,32u,64u,128u,256u,731u,1024u,2048u})
    for(unsigned filterCase=0;filterCase<4;++filterCase)for(bool dense:{false,true}) {
        check(core.prepare({fs,2048,1,2}),"tail prepare");t={};t.diffuse=character;t.width=200;
        t.lowHz=filterCase<2?30:500;t.highEnabled=filterCase==1 || filterCase==2;t.highHz=filterCase==1?1000:20000;
        t.inputDb=t.outputDb=12;core.setTargets(t);
        constexpr unsigned lastInput=31;
        const unsigned start=lastInput+unsigned(std::ceil(2*fs)),end=lastInput+unsigned(std::ceil(3*fs));
        unsigned position=0,observed=0;double peak=0;
        while(position<end) {
            const unsigned n=std::min(blockSize,end-position);
            for(unsigned i=0;i<n;++i)b.l[i]=position+i<=lastInput?((position+i)%2?.7:-1.):0.;
            stereo::Targets target=t;ProcessContext context;
            if(dense && position<fs*.15) {
                target.diffuse=((position/blockSize)%2)?!character:character;
                target.lowHz=(position/blockSize)%2?30:500;target.highEnabled=(position/blockSize)%3;
                target.highHz=(position/blockSize)%2?1000:20000;
                context.seek=(position/blockSize)%5==0;
            }
            auto audio=b.block(1,2,n);if(position>lastInput)audio.inputSilenceFlags=1;
            realtime=true;core.setTargets(target);core.process(audio,context);realtime=false;
            for(unsigned i=0;i<n;++i)if(position+i>=start) {
                ++observed;peak=std::max({peak,std::abs(b.yl[i]),std::abs(b.yr[i])});
                check(std::isfinite(b.yl[i]) && std::isfinite(b.yr[i]),"finite entire tail observation interval");
            }
            position+=n;
        }
        check(observed==end-start,"tail interval has no skipped samples");
        check(peak<1e-5 && core.tail()==unsigned(std::ceil(2*fs)),"complete [2s,3s) tail interval at extreme trims/filter/transitions");
        worst=std::max(worst,peak);++cases;
        if(log)log<<fs<<','<<(character?"Diffuse":"Tight")<<','<<blockSize<<','<<t.lowHz<<','<<t.highEnabled<<','<<t.highHz
            <<",12,12,200,"<<dense<<','<<lastInput<<','<<start<<','<<end<<','<<observed<<','<<peak<<'\n';
    }
    std::cout<<"PASS "<<cases<<" full tail intervals [lastInput+2s,lastInput+3s); worst peak "<<worst<<" (<1e-5)\n";
}
template<class T> void automationSeekAndRepeat() {
    Buffer<T> b;stereo::Core a,c;check(a.prepare({}) && c.prepare({}),"automation prepare");
    stereo::Targets t;
    for(unsigned n=0;n<6000;++n) {
        if(n%23==0){t.width=n%2?0:200;t.existingSide=(n%3)*100;t.mix=(n%5)*25;t.diffuse=!t.diffuse;
            t.lowHz=n%2?30:500;t.highEnabled=!t.highEnabled;t.highHz=n%2?1000:20000;t.swap=!t.swap;t.mono=!t.mono;}
        ProcessContext context;context.seek=n%127==0;
        b.l[0]=noise();b.r[0]=noise();a.setTargets(t);c.setTargets(t);
        realtime=true;a.process(b.block(2,2,1),context);realtime=false;
        auto yl=b.yl[0],yr=b.yr[0];c.process(b.block(2,2,1),context);
        check(yl==b.yl[0] && yr==b.yr[0],"repeatable dense changes/seeks");
        check(std::abs(.5*double(yl)+.5*double(yr)-(.5*double(b.l[0])+.5*double(b.r[0])))<(sizeof(T)==4?1e-6:1e-12),"Mid during dense character/filter/seek fades");
    }
    // A queued final target must converge without another host event.
    t={};t.mono=true;a.setTargets(t);b.l.fill(.7);b.r.fill(-.1);
    a.process(b.block(),{});a.process(b.block(),{});
    for(unsigned i=0;i<2048;++i)check(b.yl[i]==b.yr[i],"Mono Check settles to equal channels");
    t.bypass=true;t.inputDb=t.outputDb=12;a.setTargets(t);a.process(b.block(),{});a.process(b.block(),{});
    for(unsigned i=0;i<2048;++i)check(std::abs(double(b.yl[i])-double(T(.7)))<(sizeof(T)==4?1e-6:1e-12) && std::abs(double(b.yr[i])-double(T(-.1)))<(sizeof(T)==4?1e-6:1e-12),"bypass ignores stored mono/trims");
    std::cout<<"PASS "<<(sizeof(T)==4?"float32":"float64")<<" automation, coalesced transitions, seeks, bit-identical repeatability, mono and soft bypass\n";
}
void longStress() {
    Buffer<double> b;stereo::Core core;
    for(bool character:{false,true}) {
        check(core.prepare({48000,2048,2,2}),"stress prepare");stereo::Targets t;
        t.diffuse=character;t.width=t.existingSide=200;t.inputDb=t.outputDb=12;core.setTargets(t);
        unsigned remaining=48000*600;
        while(remaining) {
            const unsigned n=std::min(remaining,2048u);
            t.lowHz=remaining%8192?30:500;t.highEnabled=true;t.highHz=remaining%16384?1000:20000;core.setTargets(t);
            for(unsigned i=0;i<n;++i){b.l[i]=noise();b.r[i]=noise();}
            realtime=true;core.process(b.block(2,2,n),{});realtime=false;
            for(unsigned i=0;i<n;++i)check(std::isfinite(b.yl[i]) && std::isfinite(b.yr[i]) && std::abs(b.yl[i])<512 && std::abs(b.yr[i])<512,"10minute stable full-scale noise");
            remaining-=n;
        }
    }
    std::cout<<"PASS 10 minutes full-scale noise per Character, filter endpoints and maximum trims/width\n";
}
int main(int argc,char** argv) {
    matrix<float>();matrix<double>();inPlaceAndIsolation();sampleFormatOverflow<float>();sampleFormatOverflow<double>();sideIntegrityAndHighOff();frequencyAndTail(argc>2?argv[2]:nullptr);automationSeekAndRepeat<float>();automationSeekAndRepeat<double>();
    if(argc>1 && (std::strcmp(argv[1],"--stress")==0 || std::strcmp(argv[1],"stress")==0))longStress();
    check(allocations==0 && deallocations==0,"no C++ heap calls during process");
    std::cout<<"PASS process C++ new/delete guard: "<<allocations<<'/'<<deallocations<<'\n';
}
