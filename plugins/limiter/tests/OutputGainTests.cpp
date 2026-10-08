#include "plugins/limiter/Dsp.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;
using namespace just::limiter;
static void check(bool value,const char* label){if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
static void set(SoundState& s,ParamID id,double v){s.targets[id]=parameters[id].toNormalized(v);}
static double tick(LimiterEngine& e,double x){double a=0,b=0;AudioBlock<double> block{{&x,&x},{&a,&b},2,2,1};e.process(block,{});check(a==b&&std::isfinite(a),"finite linked stereo");return a;}
int main(){
 for(double processingMode:{0.,1.}){
  LimiterEngine normal,trimmed;check(normal.prepare({48000,1,2,2})&&trimmed.prepare({48000,1,2,2}),"prepare");
  auto a=initialState({},registry);set(a,mode,processingMode);auto b=a;set(b,output,-12);
  normal.applyTargets(a,0);trimmed.applyTargets(b,0);
  const double gain=std::pow(10.,-12./20);double error=0;
  for(unsigned i=0;i<2000;++i){double x=i<1000?3*std::sin(i*.19):.1,aa=tick(normal,x),bb=tick(trimmed,x);error=std::max(error,std::abs(bb-aa*gain));check(normal.appliedReductionDb()==trimmed.appliedReductionDb(),"Output does not change detector, release or GR");}
  check(error<1e-14,"independent gain follows limiter");
  // Dense output edits and bypass transitions must not modify the detector or
  // bypass's raw-input definition. Host parameter segmenting is tested separately.
  for(unsigned i=0;i<3000;++i){if(i%13==0){set(b,output,i%2?-36:0);trimmed.applyTargets(b,i);}if(i==200||i==1000||i==1800){set(a,bypass,i==1000?0:1);set(b,bypass,i==1000?0:1);normal.applyTargets(a,i);trimmed.applyTargets(b,i);}double x=2.3*std::sin(i*.21),aa=tick(normal,x),bb=tick(trimmed,x);check(normal.appliedReductionDb()==trimmed.appliedReductionDb(),"dense Output/bypass automation leaves GR invariant");if(i>2300)check(aa==bb,"settled bypass ignores Output gain");}
  std::cout<<"PASS output mode="<<processingMode<<" scalar_error="<<error<<" dense edits and bypass GR identical\n";
 }
 // Exact 5ms dB ramp after the delayed signal has settled.
 LimiterEngine e;e.prepare({48000,1,2,2});auto s=initialState({},registry);e.applyTargets(s,0);for(unsigned i=0;i<1000;++i)tick(e,.25);
 set(s,output,-24);e.applyTargets(s,0);double previous=1;
 for(unsigned i=1;i<=240;++i){const double out=tick(e,.25),expected=.25*std::pow(10.,(-24.*i/240)/20);check(std::abs(out-expected)<1e-14,"5ms output dB smoothing");check(out<=previous,"smooth attenuation monotonic");previous=out;check(e.appliedReductionDb()==0,"output-only attenuation is not limiting GR");}
 check(std::abs(tick(e,.25)-.25*std::pow(10.,-24./20))<1e-14,"output ramp settles");
 std::cout<<"PASS independent post gain, 5ms smooth automation, <=0dB range and raw bypass\n";
}
