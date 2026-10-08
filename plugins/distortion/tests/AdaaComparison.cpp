// Experimental comparison only. Not registered in Module.cpp or a shipping path.
// ADAA method: Bilbao et al., IEEE SPL 2017, doi:10.1109/LSP.2017.2675541.
#include "plugins/distortion/Dsp.hpp"
#include <fstream>
#include <iostream>
using namespace just::distortion;
static double primitive(double x,double shape){
    const double a=std::abs(x),w=.5*shape,l=1-w,h=1+2*w;
    if(!w)return a<=1?.5*a*a:a-.5;
    if(a<=l)return .5*a*a;
    double t=std::min(1.0,(a-l)/(3*w));
    double area=.5*l*l+3*w*(l*t+w*(1.5*t*t-t*t*t+.25*t*t*t*t));
    return a>h?area+a-h:area;
}
struct Experimental : Oversampler {
    double previous=0,previousAdaa=0;
    std::array<double,5> equalizer{};unsigned equalizerAt=0;
    void prepare(unsigned f){Oversampler::prepare(f);previous=previousAdaa=0;equalizer.fill(0);equalizerAt=0;}
    double tick(double x,double shape,unsigned mode){
        inputAt=(inputAt+1)%taps;input[inputAt]=x;double out=0;
        for(unsigned phase=0;phase<factor;++phase){
            double up=0;for(unsigned n=phase;n<taps;n+=factor)up+=coefficients[n]*input[(inputAt+taps-n/factor)%taps];up*=factor;
            double difference=up-previous;
            double y=std::abs(difference)<1e-6*std::max({1.,std::abs(up),std::abs(previous)})?
                transfer(Model::Hard,.5*(up+previous),0,shape):(primitive(up,shape)-primitive(previous,shape))/difference;
            previous=up;
            // The 0.5-high-sample ADAA lag plus 0.5-high-sample averaging is
            // compensated by decimating phase1: net PDC remains32 at fixed4x.
            double filtered=mode?.5*(y+previousAdaa):y;previousAdaa=y;
            if(mode==2){
                // Original 5-tap correction for the TWO half-sample averages.
                // H=1+t+t^2, t=sin^2(w/2); cos^2(w/2)*H=1-t^3.
                // Adds two high samples; decimation phase3 cancels total3.
                static constexpr double tapsEQ[]={.0625,-.5,1.875,-.5,.0625};
                equalizerAt=(equalizerAt+1)%5;equalizer[equalizerAt]=filtered;filtered=0;
                for(unsigned j=0;j<5;++j)filtered+=tapsEQ[j]*equalizer[(equalizerAt+5-j)%5];
            }
            highAt=(highAt+1)%taps;high[highAt]=filtered;
            if(phase==(mode==2?3u:mode==1?1u:0u))for(unsigned n=0;n<taps;++n)out+=coefficients[n]*high[(highAt+taps-n)%taps];
        }
        return cleanTiny(out);
    }
};
int main(int argc,char** argv){
    if(argc!=2)return 2;std::ofstream file(argv[1]);file.precision(17);
    file<<"sampleRate,softness,drive,frequency,input,oneX,fourX,adaa,compensated,equalized,eightX\n";
    constexpr unsigned count=4096;
    for(double fs:{44100.,48000.,96000.,192000.})for(double shape:{0.,.5,1.})for(double drive:{12.,24.,36.})for(double hz:{1000.,10000.,17000.}){
        // Match the original48k bins85/853/1450 exactly; truncate at otherFs too.
        unsigned bin=static_cast<unsigned>(hz*count/fs);
        Oversampler base,highQuality;Experimental adaa,comp,equalized;
        base.prepare(4);highQuality.prepare(8);adaa.prepare(4);comp.prepare(4);equalized.prepare(4);
        double delay[32]={};unsigned at=0;
        for(unsigned i=0;i<count*7+32;++i){
            double x=.2*std::sin(2*pi*bin*i/count),u=x*dbGain(drive);
            double direct=transfer(Model::Hard,u,0,shape),dry=delay[at];delay[at]=direct;at=(at+1)%32;
            double b=base.tick(u,[&](double v){return transfer(Model::Hard,v,0,shape);});
            double a=adaa.tick(u,shape,0),c=comp.tick(u,shape,1),q=equalized.tick(u,shape,2);
            double h=highQuality.tick(u,[&](double v){return transfer(Model::Hard,v,0,shape);});
            if(i>=count*6 && i<count*7)file<<fs<<','<<shape*100<<','<<drive<<','<<fs*bin/count<<','<<x<<','<<dry<<','<<b<<','<<a<<','<<c<<','<<q<<','<<h<<'\n';
        }
    }
    // Verify the antiderivative against the actual static curve, including knees.
    for(double shape:{0.,.5,1.})for(double x=-3;x<=3;x+=.001){double derivative=(primitive(x+1e-6,shape)-primitive(x-1e-6,shape))/2e-6;
        if(std::abs(derivative-transfer(Model::Hard,x,0,shape))>3e-7){std::cerr<<"FAIL primitive derivative at "<<x<<" shape "<<shape<<"\n";return 1;}}
    for(unsigned mode:{1u,2u})for(unsigned bin:{85u,853u,1450u}){
        Oversampler baseline;Experimental candidate;baseline.prepare(4);candidate.prepare(4);
        double rb=0,rc=0,ib=0,ic=0;
        for(unsigned i=0;i<count*7;++i){double phase=2*pi*bin*i/count,x=.05*std::sin(phase);
            double b=baseline.tick(x,[](double v){return transfer(Model::Hard,v,0,.5);}),c=candidate.tick(x,.5,mode);
            if(i>=count*6){rb+=b*std::cos(phase);ib+=b*std::sin(phase);rc+=c*std::cos(phase);ic+=c*std::sin(phase);}}
        std::cout<<"smallSignal mode="<<mode<<" Hz="<<48000.*bin/count<<" delta_dB="<<20*std::log10(std::hypot(rc,ic)/std::hypot(rb,ib))<<" phase_delta="<<std::atan2(ic,rc)-std::atan2(ib,rb)<<'\n';
    }
    for(unsigned mode:{1u,2u}){Experimental e;e.prepare(4);double peak=0;unsigned where=0;for(unsigned i=0;i<128;++i){double y=e.tick(i==0?.05:0,.5,mode);if(std::abs(y)>peak){peak=std::abs(y);where=i;}}if(where!=32)return 1;}
    std::cout<<"PASS experimental primitive derivative/PDC32 peak; wrote108 coherent comparisons (not effect integration)\n";
}
