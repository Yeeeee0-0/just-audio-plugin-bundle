#include "plugins/limiter/TransparentDsp.hpp"
#include <iostream>
#include <iomanip>
#include <cstdlib>
using namespace just;using namespace just::limiter;
static unsigned checks=0;static void check(bool b,const char* s){++checks;if(!b){std::cerr<<"FAIL "<<s<<'\n';std::exit(1);}}
static SoundState state(bool tp=false){auto s=initialState({},registry);s.targets[mode]=tp?1:0;return s;}
static void param(SoundState& s,unsigned id,double v){s.targets[id]=parameters[id].toNormalized(v);}
struct Render {std::array<std::vector<double>,2> output;double gr=0,internalTP=0;unsigned pdc=0;};
static Render render(const std::array<std::vector<double>,2>& in,SoundState s,double fs=48000,int event=-1,ParamID id=0,double value=0){TransparentLimiterEngine engine;check(engine.prepare({fs,128,2,2}),"prepare");engine.applyTargets(s,0);Render r;r.pdc=engine.latencySamples();auto n=in[0].size()+r.pdc+80;for(auto&x:r.output)x.resize(n);for(std::size_t i=0;i<n;++i){if(int(i)==event){s.targets[id]=value;engine.applyTargets(s,int(i));}double l=i<in[0].size()?in[0][i]:0,rr=i<in[0].size()?in[1][i]:0;AudioBlock<double>b{{&l,&rr},{&r.output[0][i],&r.output[1][i]},2,2,1};engine.process(b,{});r.gr=std::max(r.gr,engine.appliedReductionDb());}r.internalTP=engine.measuredReconstructionPeak();return r;}
static double reconstruct(const std::vector<double>& x,unsigned phases=16,int radius=32){double peak=0;std::vector<double> h(2*radius+1);for(unsigned p=0;p<phases;++p){double sum=0;for(int k=0;k<=2*radius;++k){double d=k-radius-double(p)/phases;h[k]=std::abs(d)>=radius?0:(std::abs(d)<1e-12?1:std::sin(pi*d)/(pi*d))*(.42+.5*std::cos(pi*d/radius)+.08*std::cos(2*pi*d/radius));sum+=h[k];}for(int n=0;n<int(x.size());++n){double y=0;for(int k=0;k<=2*radius;++k){int at=n-radius+k;if(at>=0&&at<int(x.size()))y+=h[k]/sum*x[at];}peak=std::max(peak,std::abs(y));}}return peak;}
int main(){std::cout<<std::setprecision(12);
 for(bool tp:{false,true})for(double fs:{44100.,48000.,96000.}){
  std::array<std::vector<double>,2> in;for(auto&x:in)x.resize(4096);
  for(unsigned i=0;i<4096;++i){in[0][i]=.15*(std::sin(2*pi*20*i/fs)+std::sin(2*pi*22000*i/fs));in[1][i]=i==2048?-.4:.13*std::sin(i*.93);}
  auto r=render(in,state(tp),fs);double error=0;for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<4096;++i)error=std::max(error,std::abs(r.output[ch][i+r.pdc]-in[ch][i]));check(error<3e-15,"low-level neutral null");check(r.gr==0,"no phantom GR");std::cout<<"NULL TP="<<tp<<" fs="<<fs<<" peak="<<error<<" GR="<<r.gr<<" PDC="<<r.pdc<<'\n';
 }
 std::array<std::vector<double>,2> in;for(auto&x:in)x.resize(8192);
 for(unsigned i=0;i<8192;++i){in[0][i]=.5;in[1][i]=.2;}auto bypass=state();param(bypass,just::limiter::bypass,1);auto reentry=render(in,bypass,48000,4000,just::limiter::bypass,0);for(unsigned i=3500;i<5000;++i)check(std::abs(reentry.output[0][i]-.5)<3e-15,"below-ceiling bypass re-entry transparent");
 for(double ahead:{0.,1.,2.,5.}){auto s=state();param(s,lookahead,ahead);for(unsigned i=0;i<8192;++i){in[0][i]=i==4000?4:.1;in[1][i]=.05;}auto r=render(in,s);unsigned start=0;for(unsigned i=1000;i<4001;++i)if(r.output[1][i+r.pdc]<.05-1e-8){start=i;break;}std::cout<<"LOOKAHEAD "<<ahead<<" start="<<start<<" actual_ms="<<(4000-start)/48.<<" sample_peak="<<r.output[0][4000+r.pdc]<<'\n';check(start==4000-unsigned(std::ceil(ahead*48)),"full requested Live lookahead used");check(r.output[0][4000+r.pdc]<=1+1e-14,"impulse protected");}
 for(int probe=0;probe<6;++probe){for(unsigned i=0;i<8192;++i){double x=0;if(probe==0)x=i%4<2?.99:-.99;if(probe==1)x=i==4000?4:.01*std::sin(i*.7);if(probe==2)x=2*std::sin(i*.27)+.9*std::sin(i*2.9);if(probe==3)x=((i/71)%3==0?2:.2)*std::sin(i*1.9);if(probe==4)x=.9*std::sin(i*2.7);if(probe==5)x=.8*std::sin(i*123.457)+.79*std::sin(i*i*.733)+.21*std::cos(i*.731);in[0][i]=x;in[1][i]=-.7*x;}auto r=render(in,state(true));double independent=std::max(reconstruct(r.output[0]),reconstruct(r.output[1]));std::cout<<"TP_PROBE "<<probe<<" internal="<<r.internalTP<<" independent="<<independent<<" dB="<<20*std::log10(independent)<<" GR="<<r.gr;check(independent<=1+1e-9,"verified 16x65 output ceiling");double longer=reconstruct(r.output[0],32,48);std::cout<<" independent32x97="<<longer<<" dB="<<20*std::log10(longer)<<'\n';check(longer<=1+1e-9,"independent 32x97 output ceiling for reviewed probes");}
 std::cout<<"PASS "<<checks<<" transparency/lookahead/re-entry and two-reconstructor probe checks (not universal TP certification)\n";
}
