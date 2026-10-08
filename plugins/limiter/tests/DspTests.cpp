#include "plugins/limiter/Dsp.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <cstdlib>
#include <fstream>
using namespace just;
using namespace just::limiter;
static thread_local bool realtime=false;
static std::uint64_t allocationCalls=0,releaseCalls=0;
void* operator new(std::size_t n){if(realtime)++allocationCalls;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(realtime && p)++releaseCalls;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
static void check(bool condition,const char* label){if(!condition){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
// Existing protection matrix retains its original legacy profile. New default
// behavior and missing-ID migration are covered by dedicated v0.3 tests.
static SoundState state(){auto s=initialState({},registry);s.targets[ceiling]=23./24;s.targets[mode]=1;s.targets[algorithmVersion]=0;return s;}
static void target(SoundState& s,ParamID id,double physicalValue){s.targets[id]=parameters[id].toNormalized(physicalValue);}
template<class S> using Audio=std::array<std::vector<S>,2>;
template<class S> static Audio<S> run(const Audio<S>& in,double rate,SoundState s,unsigned channels,unsigned blockSize,bool varied=false) {
 LimiterEngine engine;check(engine.prepare({rate,2048,channels,channels,0,std::is_same<S,float>::value?SampleFormat::float32:SampleFormat::float64,false}),"prepare");
 Audio<S> out;for(auto& v:out)v.resize(in[0].size());
 std::uint32_t rng=9;unsigned at=0;
 while(at<in[0].size()) {
  rng=rng*1664525+1013904223;unsigned count=std::min<unsigned>(varied?1+rng%blockSize:blockSize,in[0].size()-at);
  AudioBlock<S> b;b.samples=count;b.inputChannels=b.outputChannels=channels;
  for(unsigned ch=0;ch<channels;++ch){b.inputs[ch]=in[ch].data()+at;b.outputs[ch]=out[ch].data()+at;}
  realtime=true;engine.applyTargets(s,at);engine.process(b,{});engine.endBlock();realtime=false;at+=count;
 }
 check(allocationCalls==0 && releaseCalls==0,"no realtime new/delete");return out;
}
template<class S> static double peak(const Audio<S>& x,unsigned channels=2) {
 double result=0;for(unsigned ch=0;ch<channels;++ch)for(S v:x[ch]){check(std::isfinite(v),"finite output");result=std::max(result,std::abs(double(v)));}return result;
}
// Independent offline reference: 16x, 65 base taps, larger support than the
// engine's 8x/33-tap bound. No engine coefficients or guard implementation used.
template<class S> static double referencePeak(const Audio<S>& x,unsigned channels=2) {
 double result=peak(x,channels);
 for(unsigned p=1;p<16;++p) {
  std::array<double,65> kernel{};double sum=0;
  for(unsigned k=0;k<65;++k){double d=double(k)-32-double(p)/16;const double cardinal=std::abs(d)<1e-12?1:std::sin(3.14159265358979323846*d)/(3.14159265358979323846*d);const double window=std::abs(d)>=32?0:0.42+0.5*std::cos(3.14159265358979323846*d/32)+0.08*std::cos(2*3.14159265358979323846*d/32);kernel[k]=cardinal*window;sum+=kernel[k];}
  for(auto& h:kernel)h/=sum;
  for(unsigned ch=0;ch<channels;++ch)for(std::size_t i=0;i<x[ch].size();++i) {
   double value=0;
   for(unsigned k=0;k<65;++k){auto at=std::int64_t(i)+k-32;if(at>=0 && at<std::int64_t(x[ch].size()))value+=kernel[k]*double(x[ch][at]);}
   result=std::max(result,std::abs(value));
  }
 }
 return result;
}
template<class S> static void matrix() {
 for(double rate:{44100.,48000.,96000.,192000.})for(unsigned channels:{1u,2u})for(double m:{0.,1.}) {
  Audio<S> in;for(auto& v:in)v.resize(6144);std::uint32_t rng=7;
  for(unsigned i=0;i<4096;++i) {
   rng=rng*1664525+1013904223;double noise=(double(rng)/4294967296.-0.5)*2;
   double value=i<1024?std::sin(2*pi*0.247*i+0.73)*63:
                i<2048?std::sin(2*pi*0.49*i+0.3)*3:
                i<3072?noise*10:(i%47==0?63:0);
   in[0][i]=static_cast<S>(value);in[1][i]=static_cast<S>(-0.375*value);
  }
  auto s=state();target(s,mode,m);target(s,input,36);target(s,lookahead,m==0?0:0.1);target(s,release,10);
  auto fixed=run(in,rate,s,channels,128),varied=run(in,rate,s,channels,513,true);
  check(fixed==varied,"block partition exact invariance");
  const double cap=std::pow(10.,-1./20);check(peak(fixed,channels)<=cap*std::pow(10.,0.01/20),"sample peak ceiling");
  if(channels==2)for(std::size_t i=0;i<fixed[0].size();++i)check(std::abs(double(fixed[1][i])+0.375*double(fixed[0][i]))<2e-7,"stereo image shared gain");
  if(m==1){double tp=referencePeak(fixed,channels);check(tp<=cap*std::pow(10.,0.1/20),"independent 16x reference TP ceiling");}
 }
}
static void pdcAndBypass() {
 for(double rate:{44100.,48000.,96000.,192000.})for(double m:{0.,1.}) {
  LimiterEngine e;check(e.prepare({rate,2048,2,2}),"PDC prepare");unsigned latency=e.latencySamples();
  check(latency==std::ceil(rate*0.005)+48,"reported fixed PDC formula");
  Audio<double> in;for(auto& v:in)v.resize(latency+128);in[0][0]=0.01;in[1][0]=-0.01;
  auto s=state();target(s,mode,m);auto out=run(in,rate,s,2,127,true);
  auto at=std::max_element(out[0].begin(),out[0].end())-out[0].begin();check(at==latency,"impulse peak matches PDC exactly");
  target(s,bypass,1);target(s,input,36);target(s,trim,-24);target(s,ceiling,-24);
  for(unsigned i=0;i<128;++i){in[0][i]=double(i)/17-2;in[1][i]=-in[0][i];}
  in[0][11]=std::numeric_limits<double>::max();in[1][11]=-in[0][11];
  out=run(in,rate,s,2,65,true);
  for(unsigned i=0;i<out[0].size();++i)for(unsigned ch=0;ch<2;++ch)check(out[ch][i]==(i<latency?0:in[ch][i-latency]),"bypass raw input exact and PDC aligned");
 }
}
static void tighteningAndInvalid() {
 LimiterEngine e;check(e.prepare({48000,2048,2,2}),"automation prepare");auto s=state();target(s,input,36);
 std::array<double,1200> in{},left{},right{};in.fill(4);
 AudioBlock<double> b{{in.data(),in.data()},{left.data(),right.data()},2,2,1200};e.applyTargets(s,0);e.process(b,{});
 target(s,ceiling,-24);e.applyTargets(s,37);e.process(b,{});
 for(double x:left)check(std::abs(x)<=std::pow(10.,-24./20)*(1+1e-12),"tightening enforces new sample ceiling immediately");
 in.fill(0);in[0]=std::numeric_limits<double>::quiet_NaN();in[1]=std::numeric_limits<double>::infinity();in[2]=std::numeric_limits<double>::max();e.process(b,{});e.endBlock();
 Telemetry t;check(e.readTelemetry(t) && t.invalidInput,"invalid telemetry");for(double x:left)check(std::isfinite(x),"invalid input containment");
 auto delay=e.latencySamples();target(s,mode,0);target(s,lookahead,0);e.applyTargets(s,0);check(e.latencySamples()==delay,"mode/Lookahead do not change PDC");
 just::ProcessContext seek;seek.seek=true;in.fill(0);e.process(b,seek);for(double x:left)check(x==0,"seek clears stale delay audio");
}
static void stateAndMappings() {
 check(registry.valid(),"valid parameter registry");auto s=state();
 auto init=initialState({},registry);
 check(init.targets[input]==0.4 && init.targets[ceiling]==1 && init.targets[mode]==0 && init.targets[output]==1 && init.targets[algorithmVersion]==1,"approved transparent Init and legacy numeric mappings");
 target(s,mode,0);target(s,trim,-7.3);target(s,transient,88);target(s,lookahead,4.3);target(s,guide,-16);
 auto bytes=encodeState(s,registry);SoundState restored;
 check(decodeState(bytes.data(),bytes.size(),{},registry,restored)==StateResult::ok && s.targets==restored.targets,"all hidden state roundtrip");
 for(const auto& p:parameters){char text[64];double n;p.format(p.toNormalized(p.initial),text,64);check(p.parse(text,n),"parameter format parse Init");}
}
static void releaseGuideAndTail() {
 Audio<double> in;for(auto& v:in)v.resize(4096);for(unsigned i=0;i<3000;++i){in[0][i]=i<20?10:0.1;in[1][i]=in[0][i];}
 auto fast=state();target(fast,mode,0);target(fast,transient,0);target(fast,lookahead,0);target(fast,release,10);auto slow=fast;target(slow,release,200);
 auto a=run(in,48000,fast,2,128),b=run(in,48000,slow,2,128);check(a[0][1500]>b[0][1500]*2,"Release in ms changes audible recovery");
 auto reference=fast;target(reference,guide,-5);check(run(in,48000,reference,2,128)==a,"loudness guide never drives audio gain");
 LimiterEngine e;check(e.prepare({48000,128,2,2}),"tail prepare");auto s=state();e.applyTargets(s,0);std::array<double,128> input{},left{},right{};input[0]=0.01;AudioBlock<double> block{{input.data(),input.data()},{left.data(),right.data()},2,2,128};e.process(block,{});
 input.fill(0);block.inputSilenceFlags=3;bool heardTail=false;
 for(unsigned n=0;n<8;++n){e.process(block,{});for(double x:left)heardTail|=x!=0;}
 check(heardTail,"input silence flags preserve delayed wet output");for(double x:left)check(x==0,"finite tail ends in exact silence");
}
static void extremeAutomation() {
 LimiterEngine e;check(e.prepare({48000,128,2,2}),"automation stress prepare");auto s=state();Audio<double> out;for(auto& channel:out)channel.resize(4096);std::uint32_t rng=77;
 for(unsigned i=0;i<4096;++i) {
  if(i%17==0){target(s,input,i%2?-24:36);target(s,ceiling,i%3?-24:0);target(s,release,i%2?10:2000);target(s,transient,i%2?0:100);target(s,lookahead,i%2?0:5);}
  rng=rng*1664525+1013904223;double in=(double(rng)/4294967296.-0.5)*128;AudioBlock<double> block{{&in,&in},{out[0].data()+i,out[1].data()+i},2,2,1};
  realtime=true;e.applyTargets(s,i);e.process(block,{});e.endBlock();realtime=false;
  check(std::abs(out[0][i])<=std::pow(10.,physical(s,ceiling)/20)*(1+1e-12),"per-sample extreme automation sample ceiling");
 }
 check(referencePeak(out)<=std::pow(10.,0.1/20),"automated stream independent reference below maximum requested ceiling");check(allocationCalls==0 && releaseCalls==0,"automation callbacks allocate/free nothing");
}
static void benchmark() {
 LimiterEngine e;check(e.prepare({48000,128,2,2}),"CPU prepare");auto s=state();e.applyTargets(s,0);
 std::array<double,128> in{},left{},right{};for(unsigned i=0;i<128;++i)in[i]=2*std::sin(i*0.13);
 AudioBlock<double> b{{in.data(),in.data()},{left.data(),right.data()},2,2,128};std::vector<double> timings;timings.reserve(1024);
 for(unsigned i=0;i<1100;++i){auto start=std::chrono::steady_clock::now();e.process(b,{});e.endBlock();auto end=std::chrono::steady_clock::now();if(i>=76)timings.push_back(std::chrono::duration<double,std::micro>(end-start).count());}
 std::sort(timings.begin(),timings.end());std::cout<<"CPU 48k/128/stereo/64-bit: P99 "<<timings[1013]<<" us ("<<timings[1013]/(128./48000*1e6)*100<<"% of block)\n";
}
int main() {
 stateAndMappings();pdcAndBypass();tighteningAndInvalid();releaseGuideAndTail();extremeAutomation();matrix<float>();matrix<double>();benchmark();
 std::cout<<"PASS Limiter: 32/64-bit mono/stereo 44.1/48/96/192k, Live/Mix, extreme peaks, independent 16x reference, exact PDC/bypass, block partition, state, invalids, no callback new/delete\n";
}
