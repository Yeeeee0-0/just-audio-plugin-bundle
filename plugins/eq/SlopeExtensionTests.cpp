#include "Filter.hpp"
#include <iostream>
#include <cstdlib>
using namespace just::eq;
static void check(bool ok,const char* message){if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
int main(){
 const int orders[]={1,2,4,6,8,3,12,16};
 for(double fs:{44100.,48000.,96000.,192000.})for(int mode=0;mode<8;++mode)for(Shape shape:{Shape::highPass,Shape::lowPass}){
  FilterBank f;f.update(shape,1000,0,.7071067811865476,mode,fs);
  check(f.count==(orders[mode]+1)/2,"expected real sections");
  // Bilinear transform prewarping predicts the actual Butterworth response,
  // so there is no assumption that a digital octave is an analog octave.
  for(double hz:{200.,500.,1000.,2000.,6000.}){
   double ratio=std::tan(pi*hz/fs)/std::tan(pi*1000/fs);if(shape==Shape::highPass)ratio=1/ratio;
   double expected=-10*std::log10(1+std::pow(ratio,orders[mode]*2));
   expected=std::max(-240.,expected);
   check(std::abs(f.db(hz,fs)-expected)<1e-6,"actual coefficients match independent Butterworth response");
  }
  double hz=shape==Shape::highPass?750:1500,energy=0;int count=int(fs*.8);
  f.reset();for(int i=0;i<count;++i){double y=f.tick(std::sin(2*pi*hz*i/fs));if(i>count/2)energy+=y*y;}
  double measured=20*std::log10(std::sqrt(energy/(count/2)*2));
  check(std::abs(measured-f.db(hz,fs))<.04,"processed audio agrees with drawn curve");
  for(double q:{.1,.7071067811865476,20.})for(double cutoff:{20.,1000.,20000.}){f.reset();f.update(shape,cutoff,0,q,mode,fs);for(int k=0;k<f.count;++k){auto c=f.sections[k].c;check(std::abs(c.a2)<1 && 1+c.a1+c.a2>0 && 1-c.a1+c.a2>0,"endpoint poles remain stable");}for(int i=0;i<8192;++i)check(std::isfinite(f.tick(i==0?1:0)),"finite impulse at endpoint Q/frequency");}
 }
 std::cout<<"PASS real 6/12/18/24/36/48/72/96 slopes; four rates; independent bilinear Butterworth oracle; measured audio/curve agreement; stable poles and finite endpoint impulse\n";
}
