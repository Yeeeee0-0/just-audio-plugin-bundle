#include "../FlangerEngine.hpp"
#include "common/vst3/Module.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "common/parameters/Automation.hpp"
#include <iostream>
#include <cstdlib>
#include <random>

static thread_local bool realtime=false;
static unsigned allocations=0,releases=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
using namespace just;using namespace just::flanger;
void check(bool good,const char* label){if(!good){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
void set(SoundState& s,ParamID id,double v){const auto i=registry.index(id);s.targets[i]=parameters[i].toNormalized(v);}
struct Fixture {
 FlangerEngine engine;SoundState state=initialState(pluginIdentities[7].processor,registry);
 std::vector<double> l,r,ol,orr;double rate;unsigned channels;
 Fixture(double fs=48000,unsigned ch=2):l(2048),r(2048),ol(2048),orr(2048),rate(fs),channels(ch){check(engine.prepare({fs,2048,ch,ch,0,SampleFormat::float64,false}),"prepare");}
 void process(unsigned n,ProcessContext c={}) {
  AudioBlock<double> b{{l.data(),r.data()},{ol.data(),orr.data()},channels,channels,n};
  realtime=true;engine.applyTargets(state,c.blockSampleOffset);engine.process(b,c);engine.endBlock();realtime=false;
  check(allocations==0 && releases==0,"audio new/delete guard");
 }
};
void parametersAndState() {
 check(moduleDefinition().valid() && registry.count==17,"module/registry");
 check(parameters[0].id==0 && std::string(parameters[0].stableKey)=="flan.bypass" && parameters[0].smoothingMs==0,"frozen bypass spec");
 for(const auto& p:parameters) {
  check(p.toNormalized(p.minimum)==0 && p.toNormalized(p.maximum)==1,"mapping endpoints");
  for(double n:{0.,.25,.5,.75,1.})check(std::abs(p.toNormalized(p.toPhysical(n))-(p.stepCount?std::round(n*p.stepCount)/p.stepCount:n))<1e-12,"mapping roundtrip");
 }
 check(std::abs(parameters[registry.index(rateHzID)].toNormalized(.25)-0.4234860060369887)<1e-12,"rate logarithmic golden Init");
 check(parameters[registry.index(divisionID)].toNormalized(18)==18./26,"division golden Init");
 for(unsigned i=0;i<27;++i) {
  unsigned base=i/3,variant=i%3;std::string label=divisionLabels[i];
  check((variant==0 && label.find("dotted")==std::string::npos && label.find("triplet")==std::string::npos)||(variant==1 && label.find("dotted")!=std::string::npos)||(variant==2 && label.find("triplet")!=std::string::npos),"division ordinal labels");
  double expected=(base<6?std::pow(2.,int(base)-4):3.*std::pow(2.,int(base)-6))*(variant==1?1.5:variant==2?2./3:1);
  check(std::abs(divisionBeats(i,3,4)-expected)<1e-12,"3/4 division length");
 }
 check(divisionBeats(18,4,4)==4 && divisionBeats(18,7,8)==3.5,"meter-dependent bars");
 SoundState s=initialState(pluginIdentities[7].processor,registry);set(s,baseMsID,7.25);set(s,modeID,1);set(s,phaseID,359);set(s,wetPolarityID,1);set(s,fbLowpassHzID,19999);set(s,divisionID,26);
 auto bytes=encodeState(s,registry);SoundState restored;
 check(decodeState(bytes.data(),bytes.size(),s.plugin,registry,restored)==StateResult::ok && s.targets==restored.targets,"complete hidden state restore");
 auto before=encodeState(restored,registry);EditorViewState view;for(unsigned i=0;i<100;++i)view.advanced=!view.advanced;
 check(before==encodeState(restored,registry),"view state separated from sound state");
 auto bad=bytes;bad.pop_back();auto original=restored.targets;
 check(decodeState(bad.data(),bad.size(),s.plugin,registry,restored)==StateResult::malformed && original==restored.targets,"bad restore transactional");
 CircularSmoother circular;circular.reset(359./360);circular.target(1./360,100);
 double moved=0,last=359./360;for(unsigned i=0;i<100;++i){double now=circular.tick();moved+=std::abs(std::remainder(now-last,1.));last=now;}
 check(std::abs(moved-2./360)<1e-12,"phase shortest circular arc");circular.reset(1);check(circular.tick()==0,"360 equals zero");
}
void combAndDry() {
 for(double fs:{44100.,48000.,96000.,192000.}) {
  Fixture f(fs);set(f.state,mixID,0);std::mt19937 rng(42);std::uniform_real_distribution<double> noise(-4,4);
  for(auto& v:f.l)v=noise(rng);f.r=f.l;f.process(2048);double err=0;for(unsigned i=0;i<2048;++i)err=std::max({err,std::abs(f.l[i]-f.ol[i]),std::abs(f.r[i]-f.orr[i])});if(err>0)std::cerr<<"dry fs="<<fs<<" max error="<<err<<" first="<<f.l[0]<<","<<f.ol[0]<<" mix="<<physical(f.state,mixID)<<"\n";check(err<1e-12,"dry precision, over-fullscale preserved");
  for(double polarity:{0.,1.}) {
   Fixture comb(fs,1);set(comb.state,modeID,1);set(comb.state,baseMsID,4);set(comb.state,feedbackID,0);set(comb.state,wetPolarityID,polarity);set(comb.state,mixID,50);
   double hz=polarity==0?125:250,energy=0;unsigned count=0;
   for(unsigned start=0;start<unsigned(fs);start+=2048) {
    unsigned n=std::min(2048u,unsigned(fs)-start);for(unsigned i=0;i<n;++i)comb.l[i]=std::sin(tau*hz*(start+i)/fs);
    comb.process(n);if(start>unsigned(.1*fs))for(unsigned i=0;i<n;++i){energy+=comb.ol[i]*comb.ol[i];++count;}
   }
   check(std::sqrt(energy/count)<0.0001,"analytic fixed-delay comb notch / polarity");
  }
 }
 Fixture impulse;set(impulse.state,modeID,1);set(impulse.state,baseMsID,2);set(impulse.state,mixID,100);set(impulse.state,feedbackID,0);impulse.l[0]=1;impulse.process(512);
 check(std::abs(impulse.ol[96]-1)<1e-12 && impulse.ol[95]==0 && impulse.ol[97]==0,"wet effect delay is not PDC");
 check(impulse.engine.latencySamples()==0 && impulse.engine.tailSamples().samples==240000,"fixed PDC/tail");
}
void rangesTimingStereo() {
 for(double fs:{44100.,48000.,96000.,192000.})for(double base:{.5,2.,10.})for(double depth:{0.,50.,100.}) {
  Fixture f(fs);set(f.state,baseMsID,base);set(f.state,depthID,depth);set(f.state,rateHzID,20);set(f.state,stereoPhaseID,180);
  for(int k=0;k<12;++k){f.process(2048);auto d=f.engine.diagnosticsForTest();check(d.delayLeftMs>=.05-1e-12 && d.delayRightMs>=.05-1e-12 && d.delayLeftMs<=12 && d.delayRightMs<=12,"delay boundaries");}
 }
 Fixture same;for(unsigned i=0;i<2048;++i)same.r[i]=same.l[i]=std::sin(i*.31);same.process(2048);check(same.ol==same.orr,"zero stereo phase dual mono");
 Fixture f;set(f.state,syncID,1);ProcessContext c;c.tempoValid=c.timeSignatureValid=true;c.bpm=120;c.numerator=c.denominator=4;
 f.process(2048,c);check(f.engine.diagnosticsForTest().effectiveRate==.5,"120 BPM bar = 2 seconds");
 c.numerator=7;c.denominator=8;f.process(2048,c);check(std::abs(f.engine.diagnosticsForTest().effectiveRate-2./3.5)<1e-12,"7/8 host meter");
 set(f.state,divisionID,2);c.bpm=960;f.process(16,c);check(f.engine.diagnosticsForTest().effectiveRate>20,"sync rate not free rate clamped");
 c.tempoValid=false;f.process(0,c);f.process(16,c);check(f.engine.diagnosticsForTest().lastBpm==960 && f.engine.diagnosticsForTest().syncUnavailable,"retain last BPM / unavailable flag");
 set(f.state,timeModeID,1);c.tempoValid=true;c.bpm=120;c.ppqValid=true;c.ppq=-.25;c.blockSampleOffset=17;f.process(16,c);
 check(f.engine.diagnosticsForTest().phase>=0 && f.engine.diagnosticsForTest().phase<1,"negative PPQ transport offset");
 double expectedPhase=cycle((-.25+32*120./(60*48000))/divisionBeats(2,7,8));
 check(std::abs(f.engine.diagnosticsForTest().phase-expectedPhase)<1e-12,"negative PPQ includes block sample offset exactly once");
 c.seek=true;c.ppq=19.75;f.process(16,c);for(unsigned i=0;i<16;++i)check(std::isfinite(f.ol[i]),"seek crossfade finite");

 c.ppq=std::numeric_limits<double>::max();c.bpm=std::numeric_limits<double>::max();f.process(16,c);
 check(std::isfinite(f.engine.diagnosticsForTest().phase),"extreme finite tempo and PPQ remain bounded");
 for(unsigned i=0;i<16;++i)check(std::isfinite(f.ol[i]),"extreme host timing keeps audio finite");
 Fixture play;set(play.state,timeModeID,2);ProcessContext on;on.playing=true;play.process(1,on);check(play.engine.diagnosticsForTest().phase==0,"On Play starts phase zero");
 play.process(1024,on);on.playing=false;play.process(32,on);on.playing=true;play.process(1,on);check(play.engine.diagnosticsForTest().phase==0,"On Play restarts on rising edge");
 Fixture fallback;set(fallback.state,timeModeID,1);fallback.process(1024);double previous=fallback.engine.diagnosticsForTest().phase;fallback.process(16);check(fallback.engine.diagnosticsForTest().phase>previous && physical(fallback.state,timeModeID)==1,"Transport Sync Off retains selection and runs free");
}
void tailsAndSafety(bool longRun) {
 for(double fs:{44100.,48000.,96000.,192000.})for(double fb:{-95.,95.})for(double cutoff:{500.,20000.}) {
  Fixture f(fs);set(f.state,baseMsID,10);set(f.state,depthID,100);set(f.state,feedbackID,fb);set(f.state,fbLowpassHzID,cutoff);set(f.state,mixID,100);
  std::fill(f.l.begin(),f.l.end(),1);f.r=f.l;for(unsigned start=0;start<unsigned(fs*.5);start+=2048)f.process(2048);
  check(f.engine.diagnosticsForTest().ringPeak<=20.00000001,"feedback conservative DC bound");
  std::fill(f.l.begin(),f.l.end(),0);f.r=f.l;double endPeak=0;
  for(unsigned start=0;start<unsigned(fs*5);start+=2048){f.process(2048);if(start>unsigned(fs*4.9))for(auto v:f.ol)endPeak=std::max(endPeak,std::abs(v));}
  check(endPeak<1e-5,"tail below -100 dBFS within 5 seconds");
 }
 Fixture bad;bad.l[2]=std::numeric_limits<double>::quiet_NaN();bad.l[3]=std::numeric_limits<double>::infinity();bad.r[0]=1;bad.process(2048);
 for(auto v:bad.ol)check(std::isfinite(v),"nonfinite channel isolation");check(*std::max_element(bad.orr.begin()+64,bad.orr.begin()+256)>0,"bad channel not clearing other");
 Fixture f;std::mt19937 random(20261002);std::uniform_real_distribution<double> noise(-1,1);
 std::array<double,12> pinkBands{};
 auto pink=[&](unsigned sample){double sum=noise(random);for(unsigned band=0;band<pinkBands.size();++band){if((sample & ((1u<<band)-1u))==0)pinkBands[band]=noise(random);sum+=pinkBands[band];}return sum/13.;};
 unsigned total=longRun?48000u*600u:48000u*2u;double peak=0;
 for(unsigned start=0;start<total;start+=2048) {
  unsigned n=std::min(2048u,total-start),section=start/48000;
  set(f.state,feedbackID,section%2?95:-95);set(f.state,fbLowpassHzID,section%3?500:20000);set(f.state,baseMsID,section%2?.5:10);set(f.state,depthID,100);set(f.state,rateHzID,section%2?.01:20);
  for(unsigned i=0;i<n;++i){double v=section%4==0?1:section%4==1?std::sin(tau*997*(start+i)/48000.):section%4==2?pink(start+i):(i==0?1:0);f.r[i]=f.l[i]=v;}
  f.process(n);for(unsigned i=0;i<n;++i){check(std::isfinite(f.ol[i]) && std::isfinite(f.orr[i]),"stress finite");peak=std::max(peak,std::abs(f.ol[i]));}
 }
 check(f.engine.diagnosticsForTest().ringPeak<=20.000001,"stress ring bound");
 std::cout<<"stress audio seconds="<<total/48000.<<" outputPeak="<<peak<<" ringPeak="<<f.engine.diagnosticsForTest().ringPeak<<'\n';
}
void partitionAutomationBypass() {
 Fixture a,b;for(unsigned i=0;i<2048;++i)a.l[i]=b.l[i]=std::sin(i*.02);a.r=a.l;b.r=b.l;a.process(2048);b.process(2048);
 set(a.state,baseMsID,10);set(b.state,baseMsID,10);set(a.state,phaseID,359);set(b.state,phaseID,359);a.process(2048);
 std::vector<double> left(2048),right(2048);
 for(unsigned offset=0;offset<2048;) {
  unsigned n=std::min(37u,2048-offset);AudioBlock<double> block{{b.l.data()+offset,b.r.data()+offset},{left.data()+offset,right.data()+offset},2,2,n};ProcessContext c;c.blockSampleOffset=int(offset);
  realtime=true;b.engine.applyTargets(b.state,int(offset));b.engine.process(block,c);realtime=false;offset+=n;
 }
 check(a.ol==left && a.orr==right,"irregular host block partition invariance");
 set(a.state,bypassID,1);a.process(2048);a.process(2048);check(a.ol==a.l && a.orr==a.r,"soft bypass raw input");
 auto saved=a.state.targets;set(a.state,modeID,1);a.process(0);set(a.state,modeID,0);
 check(a.state.targets[registry.index(depthID)]==saved[registry.index(depthID)] && a.state.targets[registry.index(rateHzID)]==saved[registry.index(rateHzID)],"Manual retains Rate/Depth");
 check(allocations==0 && releases==0,"runtime allocation guards");
}
int main(int argc,char** argv) {
 parametersAndState();combAndDry();rangesTimingStereo();tailsAndSafety(argc>1 && std::string(argv[1])=="--long");partitionAutomationBypass();
 std::cout<<"PASS flanger mapping/state, dry/comb/polarity, delay bounds, stereo/timing, tails, nonfinite, automation, bypass, allocation guards\n";
}
