#include "plugins/gate/GateEngine.hpp"
#include "plugins/gate/EnvelopeModel.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
using namespace just;
namespace g=just::gate;
static unsigned checks=0;
static void check(bool value,const char* why){++checks;if(!value){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Result {double quietDb=0,loudDb=0,entryDb=0;};
struct Tap:AnalysisTap {EffectAnalysisSample last{};unsigned count=0;void pushSample(std::uint32_t,const EffectAnalysisSample& s) noexcept override{last=s;++count;}};
template<class S> Result fixture(unsigned mode,double range,double attack,double fs,std::ostream* timeline=nullptr){
    auto sound=initialState({},g::registry);
    auto set=[&](ParamID id,double v){sound.targets[g::registry.index(id)]=g::spec(id).toNormalized(v);};
    set(g::mode,mode);set(g::threshold,-36);set(g::range,range);set(g::attack,attack);set(g::release,20);set(g::hold,20);set(g::ratio,4);set(g::knee,0);
    g::GateEngine engine;check(engine.prepare({fs,64,1,1}),"prepare actual cross-threshold Gate engine");engine.applyTargets(sound,0);
    const auto total=unsigned(fs*3),first=unsigned(fs),second=unsigned(fs*2),entry=first+unsigned(fs*.020);
    double quietIn=0,quietOut=0,loudIn=0,loudOut=0;Tap tap;Result result;
    for(unsigned i=0;i<total;++i){
        const double amplitude=g::linear(i<first || i>=second?-66:-18);
        S input=S(amplitude*std::sin(2*3.14159265358979323846*997*i/fs)),output=0;
        AudioBlock<S> block;block.inputChannels=block.outputChannels=1;block.samples=1;block.inputs={&input,nullptr};block.outputs={&output,nullptr};ProcessContext context;context.analysis=&tap;engine.process(block,context);
        if(i>=unsigned(fs*.85) && i<first){quietIn+=double(input)*input;quietOut+=double(output)*output;}
        if(i>=unsigned(fs*1.85) && i<second){loudIn+=double(input)*input;loudOut+=double(output)*output;}
        if(i==entry)result.entryDb=engine.diagnostics().gainDb;
        check(std::isfinite(output),"finite actual crossed-threshold audio");
        if(timeline && i%unsigned(fs*.002)==0){auto d=engine.diagnostics();*timeline<<i/fs<<','<<double(input)<<','<<double(output)<<','<<d.detectorDb<<','<<d.gainDb<<','<<(mode==2?unsigned(d.duckPhase):unsigned(d.gatePhase))<<'\n';}
    }
    result.quietDb=10*std::log10(quietOut/quietIn);result.loudDb=10*std::log10(loudOut/loudIn);
    check(tap.count==total && (tap.last.validFields&(analysisGate|analysisReduction|analysisSidechain))==(analysisGate|analysisReduction|analysisSidechain),"real Gate producer reports every audio sample");
    check((tap.last.gateState>>8)==mode && std::abs(tap.last.reductionDb+engine.diagnostics().gainDb)<1e-12,"reported mode and reduction equal actual engine");
    return result;
}
static void historyCases(){
    g::EnvelopeHistory h;AnalysisWindow a;a.header={1,1,1,0,480,0,48000,1,1,0,analysisInputAligned,1};a.channels[0].peak=.1;a.channels[2].peak=.01;a.effectFields=analysisGate|analysisReduction;a.reductionDb=20;
    h.accept(a);check(h.count==1 && h.latest().channels[2].peak==.01 && h.latest().reductionDb==20,"history preserves actual IO and gain unchanged");
    h.accept(a);check(h.count==1,"repeated measurement never creates animation");
    auto b=a;b.header.sequence=2;b.header.startSample=480;b.header.endSample=960;h.accept(b);check(g::EnvelopeHistory::joins(h.at(0),h.at(1)),"same-time contiguous measurements join");
    auto gap=b;gap.header.sequence=4;gap.header.startSample=1440;gap.header.endSample=1920;gap.header.flags|=analysisGap;h.accept(gap);check(!g::EnvelopeHistory::joins(h.at(1),h.at(2)),"lost windows never interpolated");
    auto seek=a;seek.header.epoch=2;h.accept(seek);check(h.count==1 && h.latest().header.epoch==2,"seek discards prior epoch history");
    for(unsigned i=2;i<700;++i){seek.header.sequence=i;seek.header.startSample=(i-1)*480;seek.header.endSample=i*480;h.accept(seek);}check(h.count==512 && h.latest().header.sequence==699,"history bounded at 512 genuine windows");
    for(auto wh:{std::pair<int,int>{680,310},{720,296},{1080,460}}){auto l=g::PreviewLayout::fit(wh.first,wh.second);check(l.controlY+148<=wh.second && l.chartHeight>=96 && l.cell>=112,"four complete shared control cells and real chart fit supported body");}
    check(std::size(g::simpleIDs)==4 && g::simpleIDs[2]==121,"Simple Attack is existing stable ID121");
}
int main(int argc,char** argv){
    std::ofstream summary;if(argc>1){summary.open(argv[1]);summary<<"sample_rate,format,mode,range_db,attack_ms,quiet_gain_db,loud_gain_db,gain_20ms_after_rise_db\n";}
    for(double fs:{44100.,48000.,96000.,192000.})for(unsigned mode:{0u,1u,2u}){
        Result fast,slow;
        for(double depth:{0.,12.,48.})for(double attack:{1.,100.}){
            auto run=[&](auto sample){using S=decltype(sample);auto r=fixture<S>(mode,depth,attack,fs);const double quiet=mode==2?0:-depth,loud=mode==2?-depth:0;
                check(std::abs(r.quietDb-quiet)<.01 && std::abs(r.loudDb-loud)<.05,"Gate/Expand/Duck Range controls actual attenuation below/above threshold");
                if(depth==48 && attack==1)fast=r;if(depth==48 && attack==100)slow=r;
                if(summary)summary<<fs<<','<<(sizeof(S)==4?"float32":"float64")<<','<<g::modes[mode]<<','<<depth<<','<<attack<<','<<r.quietDb<<','<<r.loudDb<<','<<r.entryDb<<'\n';};run(float{});run(double{});
        }
        check(mode==2?slow.entryDb-fast.entryDb>20:fast.entryDb-slow.entryDb>20,"Attack audibly changes measured threshold-crossing gain after 20ms in all three modes");
    }
    if(argc>2){std::ofstream timeline(argv[2]);timeline<<"time_s,input,output,actual_detector_db,actual_gain_db,actual_phase\n";fixture<double>(0,48,100,48000,&timeline);}
    historyCases();std::cout<<"PASS crossed-threshold actual audio: "<<checks<<" checks; 144 fixtures; 997 Hz sine -66/-18 dBFS crossing -36 dBFS, Gate/Expand/Duck, Range 0/12/48, Attack 1/100 ms, four sample rates, float32/64.\n";
}
