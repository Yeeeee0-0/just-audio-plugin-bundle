#include "plugins/limiter/AppliedAttenuation.hpp"
#include "plugins/limiter/MeterHold.hpp"
#include "plugins/limiter/Dsp.hpp"
#include <iostream>
#include <cstdlib>
using namespace just::limiter;
static void check(bool value,const char* text){if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
int main(){
 std::array<double,65> kernel{};kernel[0]=kernel[64]=0.5;
 AppliedAttenuation live,mix;
 for(int n=0;n<90;++n){double lg=n==0?0.5:1,mg=n==0?0.25:1;
  check(live.advance(lg,mg,0,kernel)==(n==48?0.5:1),"Live envelope follows 16+32 sample output delay");
  check(mix.advance(lg,mg,1,kernel)==(n==32||n==48?0.25:1),"Mix bound follows only contributing FIR taps then 32 sample guard delay");
 }
 check(AppliedAttenuation::reductionDb(0.1,0.2,1,1)==0,"settled bypass is no reduction");
 check(std::abs(AppliedAttenuation::reductionDb(0.5,0.25,0,1)-18.06179973983887)<1e-12,"same-output control and safety gains compose once");
 // Noncoincident detector and guard peaks must never be combined across time.
 AppliedAttenuation isolated;
 for(int n=0;n<90;++n){double g=isolated.advance(n==0?0.5:1,1,0,kernel);double db=AppliedAttenuation::reductionDb(g,n==0?0.25:1,0,1);check(db<12.0413,"separate peaks do not add into spurious 18 dB");}
 MeterHold hold;hold.observe(10,0.8,6);hold.resetOutput();hold.resetReduction();hold.observe(10,0.8,6);check(!hold.outputHoldValid&&!hold.reductionHoldValid,"reset ignores old snapshot");hold.observe(11,0.2,2);check(hold.maximumOutput==0.2&&hold.maximumReduction==2,"next newer sample resumes hold");
 for(double rate:{44100.,48000.,96000.,192000.}){
  LimiterEngine e;just::PrepareSpec spec{rate,1,2,2,0};check(e.prepare(spec),"prepare");auto state=just::initialState({},registry);state.targets[mode]=0;state.targets[lookahead]=0;e.applyTargets(state,0);
  // Live settled constant input: independent amplitude ratio is valid only in
  // this special flat steady signal and verifies the measured control gain.
  double inputL=4,inputR=-4,outL=0,outR=0;just::AudioBlock<double> b{};b.inputs={&inputL,&inputR};b.outputs={&outL,&outR};b.samples=1;b.inputChannels=b.outputChannels=2;
  for(int i=0;i<int(rate/4);++i)e.process(b,{});
  check(std::abs(e.appliedReductionDb()+20*std::log10(std::abs(outL)/4))<1e-10,"steady Live measured applied reduction matches independently known gain");
  state.targets[bypass]=1;e.applyTargets(state,0);for(int i=0;i<int(rate/100);++i)e.process(b,{});check(e.appliedReductionDb()==0,"real bypass measurement clears without DSP reset");
 }
 std::cout<<"PASS applied GR: exact contribution timing, no cross-time stage peak summing, bypass, 4-rate steady reference; display-only sequence reset\n";
}
