#include "AnalyzerModel.hpp"
#include "common/runtime/ExtendedSpectrumTransform.hpp"
#include "SpectrumDisplay.hpp"
#include "Filter.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
using namespace just;
using namespace just::eq;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static bool near(double a,double b,double epsilon=1e-5){return std::abs(a-b)<epsilon;}
static double db(double a){return 20*std::log10(std::max(1e-30,a));}
static SampleFrame frame(std::uint64_t start,double rate,double hz,double amplitude=.5,bool anti=false,FilterBank* filter=nullptr){
    SampleFrame f;f.count=1024;f.header={1,1,start/1024+1,start,start+1024,0,rate,2,2,0,analysisInputAligned,analysisNow()};
    for(unsigned i=0;i<f.count;++i){double value=amplitude*std::sin(2*pi*hz*(start+i)/rate);f.samples[0][i]=float(value);f.samples[1][i]=float(anti?-value:value);f.samples[2][i]=float(filter?filter->tick(value):value);f.samples[3][i]=anti?-f.samples[2][i]:f.samples[2][i];}return f;
}
static ExtendedSpectrumSnapshot tone(unsigned size,double rate,double hz,double amplitude=.5,bool anti=false,bool bell=false){
    auto analyzer=std::make_unique<ExtendedSpectrumTransform>();ExtendedSpectrumSnapshot result;FilterBank filter;filter.update(Shape::bell,hz,-6,1,0,rate);
    for(unsigned i=0;i<size*4;i+=1024)analyzer->accept(frame(i,rate,hz,amplitude,anti,bell?&filter:nullptr),1,1,size,36,[&](auto& s){result=s;});
    return result;
}
static double at(const SpectrumDisplay& display,const AnalyzerSettings& settings,double hz,std::size_t columns=10000,unsigned tap=0){
    return display.point(tap,std::log(hz/20)/std::log(1000.)*(columns-1),columns,settings).db;
}
int main(){
    AnalyzerSettings settings;check(settings.valid() && settings.rangeDb==120 && settings.fftSize==4096 && settings.tiltDbPerOctave==4.5,"approved analyzer defaults");
    for(unsigned n:{4096u,8192u})for(double rate:{44100.,48000.,96000.,192000.})for(double wanted:{100.,1000.,10000.}){
        const auto peak=unsigned(std::round(wanted*n/rate));const double hz=peak*rate/n;
        const auto a=tone(n,rate,hz),quiet=tone(n,rate,hz,.5*std::pow(10.,-6./20)),anti=tone(n,rate,hz,.5,true),bell=tone(n,rate,hz,.5,false,true);
        check(a.fftSize==n && a.binCount==n/2+1,"reported resolution agrees with FFT bins");
        auto maximum=std::max_element(a.amplitude[0].begin()+1,a.amplitude[0].begin()+a.binCount);
        check(unsigned(maximum-a.amplitude[0].begin())==peak,"100/1k/10k peaks have correct frequency bins");
        check(near(db(*maximum),db(.5),.002),"coherent tone single-sided amplitude is calibrated");
        check(near(db(quiet.amplitude[0][peak])-db(*maximum),-6,.0001),"input -6dB produces measured -6dB, no per-frame normalization");
        check(a.amplitude[0]==a.amplitude[1],"flat Pre/Post streams match");
        check(a.amplitude==anti.amplitude,"opposite-polarity stereo remains visible");
        check(near(db(bell.amplitude[1][peak])-db(bell.amplitude[0][peak]),-6,.025),"real Bell filter output is -6dB at center");
        const auto offGrid=tone(n,rate,wanted);
        const auto maximum2=std::max_element(offGrid.amplitude[0].begin()+1,offGrid.amplitude[0].begin()+offGrid.binCount);
        check(std::abs(double(maximum2-offGrid.amplitude[0].begin())*rate/n-wanted)<=rate/n*.51,"literal100/1k/10k tones peak within half a bin");
        check(db(*maximum2)>=db(.5)-1.43 && db(*maximum2)<=db(.5)+.01,"off-bin Hann scalloping is bounded and not normalized away");
    }
    {
        settings.tiltDbPerOctave=0;
        std::array<float,4097> quiet{};quiet[1707]=1e-6f;
        SpectrumDisplay display;check(display.accept(quiet.data(),quiet.data(),quiet.size(),8192,48000,1,8192,0,settings),"-120dB amplitude fixture accepted without invented noise floor");
        double peak=-180;for(unsigned x=0;x<1000;++x)peak=std::max(peak,display.point(0,x,1000,settings).db);
        check(peak>-122 && peak<-119.9,"high-frequency low-level peak survives screen reduction");
        check(SpectrumDisplay::verticalPosition(-100,90)==1 && SpectrumDisplay::verticalPosition(-100,120)<1,"range selection reveals only actual low-level data");
        check(near(SpectrumDisplay::tilt(1000,4.5),0) && near(SpectrumDisplay::tilt(2000,4.5),4.5) && near(SpectrumDisplay::tilt(500,4.5),-4.5),"4.5dB/oct tilt pivots at1kHz");
    }
    {
        std::array<float,4097> values;values.fill(.01f);SpectrumDisplay display;
        check(!display.accept(values.data(),values.data(),2049,4095,48000,1,4096,0,settings),"invalid FFT dimensions rejected");
        display.accept(values.data(),values.data(),4097,8192,48000,1,8192,0,settings);
        const double flat=at(display,settings,1000);settings.tiltDbPerOctave=4.5;
        check(near(at(display,settings,1000),flat,.01),"1k anchor is unchanged by display tilt");
        settings.tiltDbPerOctave=0;
        values.fill(1e-9f);values[1701]=.5f;
        display.accept(values.data(),values.data(),4097,8192,48000,2,16384,0,settings);
        for(unsigned columns:{280u,588u,1176u,2400u}){
            double peak=-180;for(unsigned x=0;x<columns;++x){const auto p=display.point(0,x,columns,settings);if(p.valid){check(p.db<=db(.5)+1e-5 && p.db>=-180,"screen interpolation cannot overshoot measured extrema");peak=std::max(peak,p.db);}}
            check(near(peak,db(.5),.0001),"narrow peak retained at minimum/large width and1x/2x DPI");
        }
        values.fill(0);display.accept(values.data(),values.data(),4097,8192,48000,3,24576,1,settings);
        for(unsigned x=0;x<588;++x){const auto p=display.point(0,x,588,settings);check(!p.valid || p.db==-180,"silent input never fabricates a noise trace");}
    }
    {
        std::array<float,2049> loud,quiet;loud.fill(.1f);quiet.fill(.001f);
        SpectrumDisplay slowUI,fastUI;
        for(auto* d:{&slowUI,&fastUI}){d->accept(loud.data(),loud.data(),2049,4096,48000,1,4096,10,settings);d->accept(quiet.data(),quiet.data(),2049,4096,48000,1,5120,10,settings);}
        for(unsigned i=1;i<=15;++i)slowUI.advance(10+double(i)/15,settings);
        for(unsigned i=1;i<=60;++i)fastUI.advance(10+double(i)/60,settings);
        check(near(at(slowUI,settings,1000),at(fastUI,settings,1000),1e-8) && near(at(slowUI,settings,1000),-56,.0001),"same wall-clock release at15/60Hz UI refresh");
        slowUI.accept(loud.data(),loud.data(),2049,4096,48000,1,6144,11,settings);
        check(near(at(slowUI,settings,1000),-20,.0001),"new measured attack rises immediately");
        slowUI.clear();check(!slowUI.hasData(),"resume can clear old display before new complete window");
    }
    {
        AnalyzerModel model;model.settings.tiltDbPerOctave=0;
        ExtendedSpectrumSnapshot measured;measured.fftSize=4096;measured.binCount=2049;measured.hopSize=1024;measured.hasEnvelope=true;
        measured.configurationGeneration=1;measured.presentationGeneration=2;
        measured.header={1,1,1,0,4096,0,48000,2,2,0,analysisInputAligned,10250000000ull};
        for(unsigned tap=0;tap<2;++tap){measured.amplitude[tap].fill(.001f);measured.envelopeDb[tap].fill(-29);}
        model.update(measured,AnalysisAvailability::fresh,10.25);
        check(near(at(model.display,model.settings,1000),-29,.0001),"common every-hop envelope used without recomputing peaks");
        model.update(measured,AnalysisAvailability::fresh,10.375);
        check(near(at(model.display,model.settings,1000),-33.5,.0001),"between-read decay uses elapsed wall time");
        measured.header.sequence=2;measured.header.endSample=5120;measured.header.startSample=1024;measured.header.sourceNanoseconds=10350000000ull;
        for(auto& channel:measured.envelopeDb)channel.fill(-32.6f);
        model.update(measured,AnalysisAvailability::fresh,10.4);
        check(near(at(model.display,model.settings,1000),-34.4,.0001),"delayed common snapshot aligns to source time without double decay");
        measured.configurationGeneration=2;measured.presentationGeneration=1;
        for(auto& channel:measured.envelopeDb)channel.fill(-60);
        model.update(measured,AnalysisAvailability::fresh,10.4);
        check(near(at(model.display,model.settings,1000),-60,.0001),"both generation identities compared independently, no XOR collision");
        model.update({},AnalysisAvailability::unavailable,11);check(!model.display.hasData(),"unavailable new generation never retains stale peak");
    }
    check(registry.count==195,"analyzer does not add sound parameters");
    std::cout<<"PASS "<<checks<<" common FFT / EQ display-envelope checks; no native input acceptance\n";
}
