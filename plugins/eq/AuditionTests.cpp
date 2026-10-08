#include "Engine.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;using namespace just::eq;
static void check(bool b,const char* text){if(!b){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
static double amplitude(Shape shape,Target target,double hz,double q,double probe,double rate,bool mono=false) {
 Audition a;a.prepare(rate);a.begin(shape,target,hz,q);double energy=0;int count=int(rate/4);
 for(int i=0;i<count;++i){double x=std::sin(2*pi*probe*i/rate);auto y=a.tick(x,target==Target::side?-x:x,0,0,mono);if(i>count/2)energy+=y[0]*y[0];}
 return std::sqrt(energy/(count/2)*2);
}
int main(){
 for(double rate:{44100.,48000.,96000.,192000.}){
  check(amplitude(Shape::bell,Target::stereo,1000,2,1000,rate)>0.99,"bell audition passes centre at unity");
  check(amplitude(Shape::bell,Target::stereo,1000,5,200,rate)<0.06,"bell rejects distant input");
  check(amplitude(Shape::bell,Target::stereo,1000,5,600,rate)<amplitude(Shape::bell,Target::stereo,1000,0.7,600,rate),"Q narrows audition");
  check(amplitude(Shape::highPass,Target::stereo,1000,0.7,100,rate)>0.99 && amplitude(Shape::highPass,Target::stereo,1000,0.7,10000,rate)<0.011,"low cut auditions removed low side");
  check(amplitude(Shape::lowPass,Target::stereo,1000,0.7,100,rate)<0.011 && amplitude(Shape::lowPass,Target::stereo,1000,0.7,10000,rate)>0.99,"high cut auditions removed high side");
  check(amplitude(Shape::bell,Target::side,1000,1,1000,rate,true)==0,"Side solo on mono is silent");
  {EqEngine e;check(e.prepare({rate,256,2,2,0}),"live audition prepare");auto state=initialState({},registry);
   auto set=[&](Field f,double v){auto n=index(0,f);state.targets[n]=parameters[n].toNormalized(v);e.applyTargets(state,0);};
   set(enabled,1);set(frequency,1000);set(q,5);set(gain,18);check(e.beginAudition(0),"live audition begin");int position=0;
   auto measure=[&](double probe){double energy=0;int count=int(rate*.35);for(int i=0;i<count;++i,++position){double in=.1*std::sin(2*pi*probe*position/rate),out=0;AudioBlock<double> block{};block.inputs={&in,&in};block.outputs={&out,nullptr};block.samples=1;block.inputChannels=block.outputChannels=2;e.process(block,{});check(std::isfinite(out)&&std::abs(out)<.8,"live controls stay bounded during smoothing");if(i>count/2)energy+=out*out;}return std::sqrt(energy/(count/2)*2)/.1;};
   check(measure(1000)>.99,"held audition ignores 18dB EQ gain");set(frequency,6000);check(measure(1000)<.05,"held audition follows smoothed frequency automation");check(measure(6000)>.99,"held audition passes new centre without begin/reset");set(q,.2);double broad=measure(3000);set(q,10);check(measure(3000)<broad*.15,"held audition follows Q automation");set(type,3);measure(3000);check(!e.auditionActive(),"topology automation safely ends audition");
  }
  for(auto target:{Target::stereo,Target::mid,Target::side}){
   EqEngine actual,ref;PrepareSpec spec{rate,256,2,2,0};check(actual.prepare(spec)&&ref.prepare(spec),"prepare");auto state=initialState({},registry);
   auto set=[&](Field f,double v){auto n=index(0,f);state.targets[n]=parameters[n].toNormalized(v);};
   set(enabled,1);set(gain,9);set(frequency,1000);set(dynamicEnabled,1);set(just::eq::target,int(target));
   const auto saved=encodeState(state,registry);actual.applyTargets(state,0);ref.applyTargets(state,0);
   check(actual.setAudition(id(0,frequency),true),"begin enabled band by stable frequency ID");check(!actual.setAudition(id(0,gain),true),"reject non-frequency anchor");
   for(int i=0;i<int(rate*0.4);++i){if(i==int(rate*0.3))check(actual.setAudition(id(0,frequency),false),"explicit common end accepted");if(i==int(rate*0.28))check(actual.auditionActive(),"module never competes with common 250ms lease");double l=0.3*std::sin(i*0.04),r=0.1*std::sin(i*0.19),aL=0,aR=0,bL=0,bR=0;AudioBlock<double> a{};a.inputs={&l,&r};a.outputs={&aL,&aR};a.samples=1;a.inputChannels=a.outputChannels=2;auto b=a;b.outputs={&bL,&bR};actual.process(a,{});ref.process(b,{});if(i>int(rate*0.31))check(aL==bL && aR==bR,"explicit release restores exact continuously running EQ");}
   check(!actual.auditionActive(),"explicit end completes release ramp");check(encodeState(state,registry)==saved,"audition not serialized");
   actual.beginAudition(0);actual.endAudition();check(!actual.auditionActive(),"mouse-up before processing cancels");
   actual.beginAudition(0);actual.reset(ResetReason::seek);check(!actual.auditionActive(),"seek resets transient audition");
   actual.beginAudition(0);state.targets[0]=1;actual.applyTargets(state,0);check(!actual.auditionActive()&&!actual.beginAudition(0),"bypass releases and rejects audition");
   check(!actual.beginAudition(bandCount),"invalid band rejected");
  }
 }
 std::cout<<"PASS audition: four rates, live smoothed Freq/Q automation, topology safe release, reversed cut sides, mono Side, M/S, stable frequency anchor, single common lease authority, smooth explicit release, exact resumed EQ, state/bypass/seek\n";
}
