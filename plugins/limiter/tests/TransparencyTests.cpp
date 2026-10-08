#include "plugins/limiter/Dsp.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;
using namespace just::limiter;
static void check(bool value,const char* label){if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
static SoundState transparentState(){auto s=initialState({},registry);check(s.targets[mode]==0&&s.targets[ceiling]==1&&s.targets[input]==.4&&s.targets[output]==1,"approved transparent Init");return s;}
template<class Sample> static void nullAndPdc(double rate) {
    LimiterEngine engine;check(engine.prepare({rate,1,2,2}),"prepare");auto state=transparentState();engine.applyTargets(state,0);
    const auto delay=engine.latencySamples();check(delay==unsigned(std::ceil(rate*.005))+48,"unchanged PDC");
    std::vector<Sample> source(4096+delay);double nullPeak=0,maxGR=0;unsigned firstOutput=0;bool found=false;
    for(unsigned i=0;i<source.size();++i){
        // Safe under-sampled high frequency, dense alternating samples, sparse
        // transients and silence. No detector excursion above the actual limit.
        source[i]=i<4096?Sample(i==0?.8:i<1024?.9*std::sin(i*2.6):i<2048?((i&1)?.98:-.98):i<3072?(i%137==0?.99:0):0):Sample(0);
        Sample l=source[i],r=-l,outL=0,outR=0;AudioBlock<Sample> b{{&l,&r},{&outL,&outR},2,2,1};engine.process(b,{});
        if(!found&&outL!=0){firstOutput=i;found=true;}
        const double expected=i<delay?0:double(source[i-delay]);
        nullPeak=std::max(nullPeak,std::abs(double(outL)-expected));
        check(outR==-outL,"linked stereo polarity preserved");maxGR=std::max(maxGR,engine.appliedReductionDb());
    }
    check(firstOutput==delay,"measured impulse onset matches reported PDC");
    check(nullPeak<3e-15,"below-ceiling delayed null");check(maxGR==0,"below-ceiling gain reduction zero");
    std::cout<<"NULL rate="<<rate<<" bits="<<sizeof(Sample)*8<<" max_error="<<nullPeak<<" GR="<<maxGR<<" PDC="<<delay<<'\n';
}
// Independent 16x reconstruction with 65 base-rate taps. This is diagnostic,
// not a certification or the processing detector.
static double reconstructedPeak(const std::vector<double>& samples) {
    double peak=0;
    for(unsigned phase=0;phase<16;++phase){std::array<double,65> taps{};double sum=0;
        for(unsigned k=0;k<65;++k){double d=double(k)-32-double(phase)/16.;taps[k]=(std::abs(d)<1e-12?1:std::sin(3.141592653589793*d)/(3.141592653589793*d))*(std::abs(d)>=32?0:.42+.5*std::cos(3.141592653589793*d/32)+.08*std::cos(2*3.141592653589793*d/32));sum+=taps[k];}
        for(unsigned i=64;i+64<samples.size();++i){double x=0;for(unsigned k=0;k<65;++k)x+=samples[i+k-32]*taps[k]/sum;peak=std::max(peak,std::abs(x));}
    }return peak;
}
static void transientAndIntersample() {
    LimiterEngine engine;engine.prepare({48000,1,2,2});auto state=transparentState();engine.applyTargets(state,0);
    double peak=0,maxGR=0;for(unsigned i=0;i<4096;++i){double x=i==50?4.:i<1000?.02*std::sin(i*.17):0.,a=0,b=0;AudioBlock<double> block{{&x,&x},{&a,&b},2,2,1};engine.process(block,{});peak=std::max(peak,std::abs(a));maxGR=std::max(maxGR,engine.appliedReductionDb());}
    check(peak<=1+1e-12&&peak>.99,"4x transient held at sample ceiling");check(maxGR>12,"transient produces actual GR");
    engine.reset(ResetReason::seek);engine.applyTargets(state,0);std::vector<double> out(4096);
    for(unsigned i=0;i<out.size();++i){double x=i%4<2?.99:-.99,r=0;AudioBlock<double>b{{&x,&x},{&out[i],&r},2,2,1};engine.process(b,{});check(engine.appliedReductionDb()==0,"sample-safe intersample probe is not falsely treated as sample clipping");}
    const double intersample=reconstructedPeak(out);
    check(intersample>1.3,"document sample-peak mode is not a true-peak guarantee");
    std::cout<<"TRANSIENT sample_peak="<<peak<<" max_GR="<<maxGR<<" dB; INTERSAMPLE input_sample_peak=.99 reconstructed_output="<<intersample<<" ("<<20*std::log10(intersample)<<" dBFS)\n";
}
int main(){for(double rate:{44100.,48000.,96000.,192000.}){nullAndPdc<float>(rate);nullAndPdc<double>(rate);}transientAndIntersample();std::cout<<"PASS transparent sample-peak profile, null, transient, silence, polarity, PDC and explicit intersample boundary\n";}
