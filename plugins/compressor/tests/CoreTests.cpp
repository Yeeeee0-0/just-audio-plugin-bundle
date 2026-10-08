#include "../dsp/Core.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
#include <limits>

static thread_local bool inAudio=false;
static unsigned allocations=0,deallocations=0,checks=0,groups=0;
void* operator new(std::size_t n) {
    if(inAudio)++allocations;
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(inAudio && p)++deallocations;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
using namespace just;
using namespace just::compressor;
static void check(bool ok,const char* label) {
    ++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}
}
static void near(double got,double expected,double tolerance,const char* label) {
    if(!std::isfinite(got) || std::abs(got-expected)>tolerance) {
        std::cerr<<"got "<<got<<" expected "<<expected<<" tolerance "<<tolerance<<"\n";
        check(false,label);
    } else ++checks;
}
static void group(const char* label) {++groups;std::cout<<"PASS "<<label<<"\n";}
struct Output {std::vector<double> left,right;MeterFrame meter;};
static Output render(const std::vector<double>& left,const std::vector<double>& right,Settings p,
                     unsigned blockSize=128,double rate=48000,const std::vector<double>* sidechain=nullptr,
                     std::uint64_t sidechainSilent=0) {
    const unsigned channelCount=right.empty()?1:2;
    Core core;PrepareSpec spec;spec.sampleRate=rate;spec.inputChannels=spec.outputChannels=channelCount;
    spec.sidechainChannels=sidechain?1:0;
    check(core.prepare(spec,p),"core prepares supported topology");
    Output result{std::vector<double>(left.size()),std::vector<double>(right.size()),{}};
    for(std::size_t offset=0;offset<left.size();offset+=blockSize) {
        AudioBlock<double> b;b.inputChannels=b.outputChannels=channelCount;
        b.samples=static_cast<unsigned>(std::min<std::size_t>(blockSize,left.size()-offset));
        b.inputs={left.data()+offset,right.empty()?nullptr:right.data()+offset};
        b.outputs={result.left.data()+offset,right.empty()?nullptr:result.right.data()+offset};
        if(sidechain){b.sidechainChannels=1;b.sidechain[0]=sidechain->data()+offset;b.sidechainSilenceFlags=sidechainSilent;}
        inAudio=true;core.setTargets(p);core.process(b);core.endBlock();inAudio=false;
        MeterFrame m;while(core.readMeter(m))result.meter=m;
    }
    check(core.latencySamples()==0 && core.tailSamples().kind==TailKind::none,"prototype zero PDC/no audio tail");
    return result;
}
static std::vector<double> sine(unsigned samples,double rate,double hz,double amplitude) {
    std::vector<double> data(samples);for(unsigned i=0;i<samples;++i)data[i]=amplitude*std::sin(2*3.14159265358979323846*hz*i/rate);return data;
}
static double magnitude(const std::vector<double>& v,std::size_t first) {
    double power=0;for(std::size_t i=first;i<v.size();++i)power+=v[i]*v[i];return std::sqrt(power/(v.size()-first));
}
int main() {
    Settings p;check(p.thresholdDb==0 && p.ratio==2 && p.attackMs==10 && p.releaseMs==100 &&
        p.detector==Detector::rms && p.style==Style::clean && p.kneeDb==6 && p.rangeDb==24 &&
        p.mix==1 && p.stereoLink==1 && !p.externalSidechain && !p.bypass,"V2 Init physical defaults");
    group("V2 defaults and zero-lookahead scope");

    for(double input=-60;input<=0;input+=0.5) {
        const double expected=input<=-24?0:std::max(-24.0,(-24+(input+24)/4)-input);
        near(reductionForLevel(input,-24,4,0,24),expected,1e-12,"hard knee independent transfer equation");
    }
    near(reductionForLevel(-24,-24,4,6,24),-0.5625,1e-12,"soft knee center");
    near(reductionForLevel(-27,-24,4,6,24),0,1e-12,"lower knee join");
    near(reductionForLevel(-21,-24,4,6,24),-2.25,1e-12,"upper knee join");
    const double epsilon=1e-5;
    for(double edge:{-27.0,-21.0}) {
        auto curve=[&](double x){return reductionForLevel(x,-24,4,6,24);};
        near((curve(edge)-curve(edge-epsilon))/epsilon,(curve(edge+epsilon)-curve(edge))/epsilon,2e-6,"soft knee first derivative");
    }
    group("hard/soft knee, continuous first derivative and Range equation");

    for(double rate:{44100.0,48000.0,96000.0}) {
        GainEnvelope clean,punch;const unsigned tau=static_cast<unsigned>(rate*0.01);
        const double a=timeCoefficient(10,rate),r=timeCoefficient(100,rate);
        double c=0,q=0;
        for(unsigned i=0;i<tau;++i){c=clean.tick(-12,a,r,false,36);q=punch.tick(-12,a,r,true,36);}
        const double elapsed=tau/rate;
        near(c,-12*(1-std::exp(-elapsed/0.01)),1e-10,"Clean attack 63.2 percent in dB domain");
        near(std::pow(10,q/20),1+(std::pow(10,-12.0/20)-1)*(1-std::exp(-elapsed/0.01)),1e-10,"Punch attack 63.2 percent in linear gain domain");
        clean.reset(-12);punch.reset(-12);
        for(unsigned i=0;i<static_cast<unsigned>(rate*0.1);++i){c=clean.tick(0,a,r,false,36);q=punch.tick(0,a,r,true,36);}
        near(c,-12/std::exp(1.0),1e-10,"Clean release 63.2 percent in dB domain");
        near(q,c,1e-12,"Punch release matches documented single dB time scale");
    }
    group("isolated Attack/Release time constants at 44.1/48/96 kHz");

    p.ratio=1;p.inputDb=0;p.makeupDb=0;
    for(double rate:{44100.0,48000.0,96000.0}) {
        const auto audio=sine(4096,rate,997,0.7);std::vector<double> reverse(audio.size());
        for(std::size_t i=0;i<audio.size();++i)reverse[i]=-audio[i];
        const auto result=render(audio,reverse,p,17,rate);
        check(result.left==audio && result.right==reverse,"Ratio=1 stereo exact double null");
        const auto mono=render(audio,{},p,128,rate);check(mono.left==audio,"Ratio=1 mono exact double null");
    }
    group("mono/stereo Ratio=1 exact sample equality");

    const std::vector<double> constant(48000,0.5);
    p={};p.thresholdDb=-18;p.ratio=4;p.kneeDb=0;p.detector=Detector::peak;
    auto steady=render(constant,{},p);
    const double inputLevel=20*std::log10(0.5);
    const double expectedGr=(-18+(inputLevel+18)/4)-inputLevel;
    near(20*std::log10(steady.left.back()/0.5),expectedGr,0.001,"audio steady-state transfer within 0.1 dB");
    near(steady.meter.gainReductionDb,expectedGr,0.001,"real compression-stage GR meter");
    p.style=Style::punch;auto punch=render(constant,{},p);
    near(punch.left.back(),steady.left.back(),1e-10,"Clean/Punch static curve identical");
    p.style=Style::clean;p.thresholdDb=-60;p.ratio=20;p.rangeDb=3;
    auto limited=render(constant,{},p);
    near(20*std::log10(limited.left.back()/0.5),-3,0.001,"Range bounds relative reduction");
    check(limited.meter.gainReductionDb>=-3.05,"meter reduction stays in stable Range");
    group("audio transfer, Clean/Punch steady state and relative Range bound");

    p={};p.thresholdDb=-18;p.ratio=4;p.kneeDb=0;
    std::vector<double> negative(constant.size(),-0.5);
    auto anti=render(constant,negative,p);
    near(anti.left.back(),-anti.right.back(),1e-12,"opposite polarities never cancel detector");
    near(20*std::log10(anti.left.back()/0.5),expectedGr,0.001,"RMS constant amplitude definition");
    std::vector<double> quiet(constant.size(),0.01);
    auto linked=render(constant,quiet,p);
    near(linked.left.back()/0.5,linked.right.back()/0.01,1e-10,"fully linked equal gains");
    p.stereoLink=0;auto unlinked=render(constant,quiet,p);
    near(unlinked.right.back(),0.01,1e-12,"unlinked quiet channel unaffected");
    group("RMS detector, antiphase stereo and link endpoints");

    p={};p.thresholdDb=-18;p.ratio=4;p.kneeDb=0;p.externalSidechain=true;
    const std::vector<double> silent(constant.size(),0);
    auto missing=render(constant,{},p);
    check(missing.left==constant && missing.meter.sidechainMissing && !missing.meter.sidechainSilent,"missing external SC never uses internal input");
    auto externalSilent=render(constant,{},p,128,48000,&silent,1);
    check(externalSilent.left==constant && !externalSilent.meter.sidechainMissing && externalSilent.meter.sidechainSilent,"connected silent SC distinguished");
    auto externalHot=render(quiet,{},p,128,48000,&constant);
    near(20*std::log10(externalHot.left.back()/0.01),expectedGr,0.001,"external mono SC drives compression");
    check(!externalHot.meter.sidechainSilent && !externalHot.meter.sidechainMissing,"connected active SC status");
    group("external SC missing/silent/active states and mono bus");

    {
        Core live;PrepareSpec mono;mono.inputChannels=mono.outputChannels=1;mono.sidechainChannels=1;
        check(live.prepare(mono,p),"SC transition fixture");
        std::array<double,4800> main{},sc{},out{};main.fill(0.5);sc.fill(0.5);
        AudioBlock<double> signal;signal.inputs[0]=main.data();signal.outputs[0]=out.data();
        signal.inputChannels=signal.outputChannels=1;signal.samples=main.size();
        signal.sidechainChannels=1;signal.sidechain[0]=sc.data();
        for(unsigned i=0;i<10;++i){live.process(signal);live.endBlock();MeterFrame m;while(live.readMeter(m)){};}
        near(20*std::log10(out.back()/0.5),expectedGr,0.001,"SC was compressing before disconnection");
        signal.sidechainChannels=0;signal.sidechain[0]=nullptr;
        MeterFrame disconnected;
        for(unsigned i=0;i<10;++i){live.process(signal);live.endBlock();while(live.readMeter(disconnected)){};}
        near(out.back(),0.5,0.0001,"SC disconnection releases toward unity");
        check(disconnected.sidechainMissing && !disconnected.sidechainSilent,"disconnection status retained");
        signal.sidechainChannels=1;signal.sidechain[0]=sc.data();signal.sidechainSilenceFlags=1;
        live.process(signal);live.endBlock();MeterFrame reconnected;check(live.readMeter(reconnected),"reconnection snapshot");
        check(!reconnected.sidechainMissing && reconnected.sidechainSilent,"connected silence after missing remains distinct");
        signal.sidechainSilenceFlags=0;
        for(unsigned i=0;i<10;++i){live.process(signal);live.endBlock();while(live.readMeter(reconnected)){};}
        near(20*std::log10(out.back()/0.5),expectedGr,0.001,"restored SC resumes compression");
    }
    group("working SC disconnect/reconnect and smooth release toward unity");

    const auto low=sine(48000,48000,50,0.8);
    p={};p.thresholdDb=-18;p.ratio=4;p.kneeDb=0;
    auto filterOff=render(low,{},p);
    p.highPass=true;p.highPassHz=1000;
    auto filterOn=render(low,{},p);
    check(magnitude(filterOn.left,24000)>magnitude(filterOff.left,24000)*1.5,"SC high pass reduces low-frequency triggering");
    p.ratio=1;p.lowPass=true;p.lowPassHz=1000;
    auto noToneChange=render(low,{},p);check(noToneChange.left==low,"SC EQ never filters main audio");
    group("sidechain filters affect detection only");

    p={};p.thresholdDb=-24;p.ratio=6;p.kneeDb=12;p.detector=Detector::blend;p.rmsBlend=0.4;
    p.style=Style::punch;p.stereoLink=0.3;
    const auto material=sine(12000,48000,71,0.85);
    auto a=render(material,material,p,1),b=render(material,material,p,17),c=render(material,material,p,2048);
    check(a.left==b.left && b.left==c.left && a.right==c.right,"identical output across variable block sizes");
    group("Blend/Punch deterministic output across 1/17/2048 samples");

    p={};p.thresholdDb=-60;p.ratio=20;p.inputDb=6;p.makeupDb=12;p.bypass=true;
    auto bypass=render(constant,{},p);check(bypass.left==constant,"bypass excludes input/makeup/mix");
    p.bypass=false;p.mix=0;auto dry=render(constant,{},p);
    near(dry.left.back(),0.5*std::pow(10,6.0/20),1e-12,"Dry includes Input Trim but excludes Makeup");
    p={};p.thresholdDb=-18;p.ratio=4;p.kneeDb=0;p.makeupDb=6;p.mix=0.5;
    auto blend=render(constant,{},p);
    near(blend.left.back(),0.5*(0.5+0.5*std::pow(10,(expectedGr+6)/20)),1e-6,"linear coherent dry/wet blend with manual makeup");
    group("bypass and Dry/Wet/Makeup signal definitions");

    p={};Core automated;PrepareSpec spec;check(automated.prepare(spec,p),"automation fixture");
    std::array<double,2048> input{},left{},right{};input.fill(0.5);
    AudioBlock<double> block;block.inputs={input.data(),input.data()};block.outputs={left.data(),right.data()};
    block.inputChannels=block.outputChannels=2;block.samples=input.size();
    double largestJump=0,last=0.5;
    for(unsigned pass=0;pass<24;++pass) {
        p.style=pass%2?Style::clean:Style::punch;p.detector=pass%3==0?Detector::peak:pass%3==1?Detector::rms:Detector::blend;
        p.thresholdDb=pass%2?-60:0;p.ratio=pass%2?20:1;p.inputDb=0;p.makeupDb=0;p.mix=1;
        p.rangeDb=pass%2?36:0;p.bypass=pass%4==0;p.externalSidechain=pass%5==0;
        inAudio=true;automated.setTargets(p);automated.process(block);automated.endBlock();inAudio=false;
        MeterFrame frame;while(automated.readMeter(frame)){}
        for(double sample:left){check(std::isfinite(sample),"finite simultaneous target changes");largestJump=std::max(largestJump,std::abs(sample-last));last=sample;}
    }
    check(largestJump<0.02,"smoothed simultaneous settings avoid a full-volume jump");
    check(allocations==0 && deallocations==0,"no C++ allocation/deallocation in physical target or audio processing");
    group("simultaneous settings/modes, smoothing and realtime C++ allocation guard");

    p={};p.ratio=1;
    std::vector<double> malformed={0.5,std::numeric_limits<double>::quiet_NaN(),0.25,
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-0.4,1e-300};
    auto clean=render(malformed,{},p,1);
    check(clean.left[1]==0 && clean.left[3]==0 && clean.left[4]==0,"NaN/Inf sanitized per sample");
    near(clean.left[2],0.25,0,"valid input recovers after contamination");
    check(std::all_of(clean.left.begin(),clean.left.end(),[](double x){return std::isfinite(x);}),"all malformed outputs finite");
    p.inputDb=24;p.makeupDb=24;
    std::vector<double> huge(256,std::numeric_limits<double>::max());
    auto protectedOutput=render(huge,{},p);
    check(std::isfinite(protectedOutput.left.back()) && protectedOutput.meter.numericSaturation,"overflow limited to representable audio range");
    group("nonfinite input, recovery, denormal detector state and numeric overflow");

    Core f32;spec.format=SampleFormat::float32;check(f32.prepare(spec),"float32 prepares");
    std::array<float,128> floatIn{},floatL{},floatR{};
    for(unsigned i=0;i<floatIn.size();++i)floatIn[i]=0.25f*std::sin(i*0.07f);
    AudioBlock<float> floats;floats.inputs={floatIn.data(),floatIn.data()};floats.outputs={floatL.data(),floatR.data()};
    floats.inputChannels=floats.outputChannels=2;floats.samples=128;
    Settings unity;unity.ratio=1;f32.setTargets(unity);
    // Set Ratio=1 at preparation to exclude the documented target smoothing.
    check(f32.prepare(spec,unity),"float unity Init");f32.process(floats);
    check(floatIn==floatL && floatIn==floatR,"float32 Ratio=1 exact null");
    floats.inputSilenceFlags=3;f32.process(floats);
    check(std::all_of(floatL.begin(),floatL.end(),[](float v){return v==0;}),"host silence flags honored");
    floats.samples=0;f32.process(floats);f32.endBlock();MeterFrame frame;
    check(f32.readMeter(frame),"zero block safe, meter POD available");
    for(unsigned i=0;i<100;++i){f32.process(floats);f32.endBlock();}
    unsigned count=0;while(f32.readMeter(frame))++count;
    check(count==7,"meter queue bounded under stalled consumer");
    group("32-bit audio, silence flags, zero blocks and stalled display queue");

    Settings bad;bad.thresholdDb=std::numeric_limits<double>::quiet_NaN();bad.ratio=-1;bad.attackMs=0;
    bad.releaseMs=std::numeric_limits<double>::infinity();bad.mix=10;
    auto safe=sanitized(bad);check(safe.thresholdDb==0 && safe.ratio==1 && safe.attackMs==0.05 && safe.releaseMs==100 && safe.mix==1,"invalid controls bounded before use");
    PrepareSpec invalid;invalid.sampleRate=0;Core rejected;check(!rejected.prepare(invalid),"invalid sample rate rejected");
    invalid={};invalid.outputChannels=1;check(!rejected.prepare(invalid),"undefined topology rejected");
    group("invalid physical controls and preparation rejection");
    std::cout<<"RESULT "<<groups<<" groups, "<<checks<<" checks, "<<allocations<<" audio new calls, "<<deallocations<<" audio delete calls\n";
}
