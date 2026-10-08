// Compile this same replay against exact 0be3 and the current checkout, then
// byte-compare outputs. No new API is used so this is an independent old-engine oracle.
#include "plugins/eq/Engine.hpp"
#include <cstdio>
using namespace just;using namespace just::eq;
int main(int argc,char** argv){if(argc!=2)return 1;auto* out=std::fopen(argv[1],"wb");if(!out)return 2;
 for(double fs:{44100.,48000.,96000.,192000.}){
  EqEngine e;e.prepare({fs,64,2,2,0});auto s=initialState({},registry);
  std::array<double,64> l{},r{},ol{},orr{};
  auto set=[&](int b,Field f,double v){auto i=index(b,f);s.targets[i]=parameters[i].toNormalized(v);};
  for(int block=0;block<1200;++block){
   for(int b=0;b<12;++b){set(b,enabled,1);set(b,type,(block/31+b)%6);set(b,target,b%3);set(b,frequency,80*std::pow(1.48,b));set(b,gain,((block/17+b)%7)-3);set(b,q,.6+.1*(b%4));set(b,slope,(block/11+b)%5);set(b,dynamicEnabled,(block/67+b)%2);set(b,threshold,-20-b);}
   for(int i=0;i<64;++i){double t=(block*64+i)/fs;l[i]=.13*std::sin(2*pi*173*t)+.04*std::cos(2*pi*1803*t);r[i]=.07*std::sin(2*pi*517*t)-.09*std::cos(2*pi*67*t);}
   e.applyTargets(s,0);AudioBlock<double> a{{l.data(),r.data()},{ol.data(),orr.data()},2,2,64,0};e.process(a,{});e.endBlock();std::fwrite(ol.data(),sizeof(double),64,out);std::fwrite(orr.data(),sizeof(double),64,out);
  }
 }
 std::fclose(out);return 0;
}
