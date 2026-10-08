#include "SimpleMacros.hpp"
#include "Presets.hpp"
#include "ReverbEngine.hpp"
#include "common/ui/DisplayFormat.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <filesystem>
#include <fstream>
using namespace just;using namespace just::reverb;
static void check(bool value,const char* label){if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
static std::vector<double> render(const SoundState& state,bool transients=false,double seconds=2) {
    ReverbEngine engine;check(engine.prepare({48000,256,2,2,0,SampleFormat::float64,true}),"macro audio prepare");
    engine.applyTargets(state,0);
    const auto frames=std::size_t(48000*seconds);
    std::vector<double> result(frames*2),inL(256),inR(256),outL(256),outR(256);
    std::uint32_t random=43;constexpr double pi=3.14159265358979323846;
    for(std::size_t at=0;at<frames;at+=256) {
        for(unsigned n=0;n<256;++n){
            if(!transients){inL[n]=at==0 && n==0?.5:0;inR[n]=at==0 && n==0?.27:0;continue;}
            const double t=(at+n)/48000.;double x=0;
            for(double onset:{.15,.6,1.1})if(t>=onset && t<onset+.25){
                const double dt=t-onset;random=1664525*random+1013904223;
                const double noise=double(random)/4294967295.*2-1;
                x+=.16*noise*std::exp(-dt*65)+.18*std::sin(2*pi*(170*dt-40*dt*dt))*std::exp(-dt*18)
                    +.065*std::sin(2*pi*3200*dt)*std::exp(-dt*24);
            }
            inL[n]=x;inR[n]=x*.73;
        }
        engine.process(AudioBlock<double>{{inL.data(),inR.data()},{outL.data(),outR.data()},2,2,256,!transients && at?3ull:0ull},{});engine.endBlock();
        for(std::size_t n=0;n<std::min<std::size_t>(256,frames-at);++n){
            check(std::isfinite(outL[n]) && std::isfinite(outR[n]),"macro render finite");result[(at+n)*2]=outL[n];result[(at+n)*2+1]=outR[n];
        }
    }
    return result;
}
struct AudioMeasures {
    double energy=0,centroid=0,sideFraction=0,hfDifferenceRatio=0,earlyFraction=0;
};
static AudioMeasures measure(const std::vector<double>& audio) {
    AudioMeasures s;double side=0,hf=0,early=0;
    for(std::size_t n=0;n<audio.size()/2;++n){
        const double l=audio[n*2],r=audio[n*2+1],e=l*l+r*r;
        s.energy+=e;s.centroid+=e*n/48000.;side+=(l-r)*(l-r)*.5;
        if(n){const double dl=l-audio[(n-1)*2],dr=r-audio[(n-1)*2+1];hf+=dl*dl+dr*dr;}
        if(n<4800)early+=e;
    }
    check(s.energy>0,"nonzero measured audio");s.centroid/=s.energy;s.sideFraction=side/s.energy;
    s.hfDifferenceRatio=hf/s.energy;s.earlyFraction=early/s.energy;return s;
}
static void writeComparison(const std::filesystem::path& path,const std::vector<double>& a,const std::vector<double>& b) {
    std::ofstream file(path,std::ios::binary);check(bool(file),"comparison opens");
    const std::uint32_t samples=std::uint32_t(a.size()+b.size()+48000*2*.4),bytes=samples*2;
    auto integer=[&](std::uint32_t v,int n){for(int i=0;i<n;++i)file.put(char(v>>(i*8)));};
    file.write("RIFF",4);integer(36+bytes,4);file.write("WAVEfmt ",8);integer(16,4);integer(1,2);integer(2,2);
    integer(48000,4);integer(48000*4,4);integer(4,2);integer(16,2);file.write("data",4);integer(bytes,4);
    auto wave=[&](const auto& data){for(double v:data){check(std::isfinite(v) && std::abs(v)<1,"listen clip finite and unclipped at fixed gain");integer(std::uint16_t(std::int16_t(std::lrint(v*32767))),2);}};
    wave(a);for(unsigned n=0;n<48000*2*.4;++n)integer(0,2);wave(b);check(bool(file),"comparison written");
}
static void audioEvidence(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);auto anchor=presetState(0);set(anchor,Mix,35);
    const std::array<std::array<double,2>,7> pair={{{.1,.9},{.1,.9},{.15,.85},
        {simpleMacros[3].display.toNormalized(.65),simpleMacros[3].display.toNormalized(3.2)},
        {simpleMacros[4].display.toNormalized(65),simpleMacros[4].display.toNormalized(150)},
        {simpleMacros[5].display.toNormalized(25),1},{0,.5}}};
    const char* names[]={"01-brightness","02-character","03-distance","04-space","05-decay-rate","06-stereo-width","07-mix"};
    std::ofstream csv(directory/"measures.csv");csv<<"macro,low_display,high_display,low_energy,high_energy,low_centroid_seconds,high_centroid_seconds,low_side_fraction,high_side_fraction,low_hf_difference_ratio,high_hf_difference_ratio,low_first_100ms_fraction,high_first_100ms_fraction\n";
    for(unsigned m=0;m<7;++m){
        const auto macro=SimpleMacro(m);auto low=macroTarget(macro,pair[m][0],anchor),high=macroTarget(macro,pair[m][1],anchor);
        writeComparison(directory/(std::string(names[m])+"-low-then-high.wav"),render(low,true,6),render(high,true,6));
        // Isolate true wet output for directional measures; do not label the
        // finite-window energy centroid as an RT60 estimate or a spectrum.
        if(macro!=SimpleMacro::mix){set(low,Mix,100);set(high,Mix,100);}
        const auto l=measure(render(low,false,4)),h=measure(render(high,false,4));
        if(macro==SimpleMacro::brightness)check(h.hfDifferenceRatio>l.hfDifferenceRatio,"Brightness raises measured wet HF difference proxy");
        if(macro==SimpleMacro::distance)check(h.earlyFraction<l.earlyFraction,"Distance reduces first-100ms wet energy fraction");
        if(macro==SimpleMacro::space || macro==SimpleMacro::decayRate)check(h.centroid>l.centroid,"Space/Rate increase measured wet tail energy centroid");
        if(macro==SimpleMacro::width)check(h.sideFraction>l.sideFraction,"Width increases measured side energy fraction");
        csv<<simpleMacros[m].display.title<<','<<simpleMacros[m].display.toPhysical(macroNormalized(macro,low))<<','<<simpleMacros[m].display.toPhysical(macroNormalized(macro,high))<<','<<l.energy<<','<<h.energy<<','<<l.centroid<<','<<h.centroid<<','<<l.sideFraction<<','<<h.sideFraction<<','<<l.hfDifferenceRatio<<','<<h.hfDifferenceRatio<<','<<l.earlyFraction<<','<<h.earlyFraction<<'\n';
    }
    check(bool(csv),"audio measure CSV written");
    std::cout<<"PASS seven fixed-gain 48kHz stereo A/B clips and real wet directional measures; subjective listening pending\n";
}
struct Host {
    SoundState state=presetState(0);std::vector<ParamID> begin,end,write;int reject=-1;
    EditorServices services() {
        EditorServices s;s.owner=this;
        s.readTarget=[](void* p,ParamID id){return static_cast<Host*>(p)->state.targets[registry.index(id)];};
        s.beginEdit=[](void* p,ParamID id){auto& h=*static_cast<Host*>(p);if(h.reject==int(id))return false;h.begin.push_back(id);return true;};
        s.performEdit=[](void* p,ParamID id,double v){auto& h=*static_cast<Host*>(p);h.write.push_back(id);h.state.targets[registry.index(id)]=v;return true;};
        s.endEdit=[](void* p,ParamID id){static_cast<Host*>(p)->end.push_back(id);};return s;
    }
};
int main(int argc,char** argv) {
    check(registry.count==36 && std::size(presets)==14,"legacy 36 parameters and 14 presets");
    for(unsigned p=0;p<14;++p) {
        auto state=presetState(p);auto bytes=encodeState(state,registry);
        for(unsigned m=0;m<simpleMacroCount;++m)check(std::isfinite(macroNormalized(SimpleMacro(m),state)),"all preset macros derived");
        check(encodeState(state,registry)==bytes && matchingPreset(state)==int(p),"display does not mutate complete preset");
    }
    auto anchor=presetState(0);set(anchor,LowDecay,.8);set(anchor,HighDecay,.4);
    const auto target=macroTarget(SimpleMacro::decayRate,simpleMacros[4].display.toNormalized(150),anchor);
    check(std::abs(physical(target,LowDecay)-1.2)<1e-12 && std::abs(physical(target,MidDecay)-1.5)<1e-12 && std::abs(physical(target,HighDecay)-.6)<1e-12,"rate scales three original ratios");
    const auto reachable=decayRateBounds(anchor);check(reachable[0]>=25 && reachable[1]<=200,"rate reachable interval stays legal");
    const auto clipped=macroTarget(SimpleMacro::decayRate,1,anchor);
    check(physical(clipped,LowDecay)<=2 && physical(clipped,HighDecay)>=.25,"existing per-band limits respected");
    auto constrained=anchor;set(constrained,LowDecay,2);set(constrained,HighDecay,.5);
    const auto boundary=decayRateBounds(constrained);check(std::abs(boundary[0]-50)<1e-12 && std::abs(boundary[1]-100)<1e-12,"joint bounds show actual 50 to 100 percent");
    auto atLimit=macroTarget(SimpleMacro::decayRate,1,constrained);
    for(auto id:{LowDecay,MidDecay,HighDecay})check(std::abs(physical(atLimit,id)/physical(constrained,id)-1)<1e-12,"shared upper bound preserves all three ratios");
    check(std::abs(physical(atLimit,MidDecay)-1)<1e-12 && atLimit.targets[registry.index(Decay)]==constrained.targets[registry.index(Decay)],"unreachable 200 percent resolves to actual 100 without double multiply");
    for(unsigned m=0;m<simpleMacroCount;++m)check(macroTarget(SimpleMacro(m),macroNormalized(SimpleMacro(m),anchor),anchor).targets==anchor.targets,"zero delta is bit-exact across all secondary targets");
    for(unsigned m=0;m<simpleMacroCount;++m) {
        auto macro=SimpleMacro(m);auto changed=macroTarget(macro,.2,anchor);const auto& binding=macroBinding(macro);
        for(unsigned i=0;i<registry.count;++i) {
            bool bound=false;for(unsigned n=0;n<binding.count;++n)bound|=parameters[i].id==binding.ids[n];
            if(!bound)check(changed.targets[i]==anchor.targets[i],"macro preserves unrelated legacy targets");
        }
        auto reset=macroResetTarget(macro,anchor);
        check(std::abs(macroNormalized(macro,reset)-macroDefault(macro))<1e-14,"reset uses original macro Init");
        auto display=macroDisplaySpec(macro);check(std::abs(display.toNormalized(display.initial)-macroDefault(macro))<1e-14,"common double click reads original Init projection");
    }
    check(macroTarget(SimpleMacro::space,std::numeric_limits<double>::quiet_NaN(),anchor).targets==anchor.targets,"non-finite request no-op");
    Host host;MacroGesture gesture;
    check(gesture.begin(host.services(),SimpleMacro::decayRate,host.state),"three-ID gesture begins");
    check(gesture.update(.8),"three-ID gesture edits");gesture.end();
    check(host.begin.size()==3 && host.write.size()==3 && host.end.size()==3,"all existing IDs balanced");
    host=Host{};host.reject=MidDecay;
    check(!gesture.begin(host.services(),SimpleMacro::decayRate,host.state),"partial begin rejection returned");
    check(host.begin.size()==1 && host.end.size()==1 && host.write.empty(),"partial begin unwinds without writes");
    host=Host{};
    check(gesture.begin(host.services(),SimpleMacro::character,host.state),"Character begins at current state");gesture.update(.5);gesture.end();
    const auto afterCharacter=host.state;
    check(gesture.begin(host.services(),SimpleMacro::distance,host.state),"Distance starts after Character");gesture.update(.75);gesture.end();
    check(std::abs(physical(host.state,Diffusion)-physical(afterCharacter,Diffusion)-2)<1e-12,"Distance retains Character's current diffusion anchor");
    const auto afterDistance=host.state;
    check(gesture.begin(host.services(),SimpleMacro::character,host.state),"Character restarts after Distance");gesture.update(.6);gesture.end();
    check(std::abs(physical(host.state,Diffusion)-physical(afterDistance,Diffusion)-4)<1e-12,"Character does not pull back a stale shared Diffusion target");
    host=Host{};check(gesture.begin(host.services(),SimpleMacro::brightness,host.state),"unchanged brightness gesture begins");gesture.update(macroNormalized(SimpleMacro::brightness,host.state));gesture.end();check(host.write.empty(),"zero delta sends no DSP writes");
    TextEditSession edit;double normalized=0;
    anchor.targets[registry.index(HighCut)]=.123456789012345;
    auto exact=edit.begin(simpleMacros[0].display,{},macroNormalized(SimpleMacro::brightness,anchor));
    check(!edit.changed(simpleMacros[0].display,{},exact.c_str(),normalized),"unchanged macro text cannot quantize host target");
    check(edit.changed(simpleMacros[0].display,{},"75 %",normalized) && normalized==.75,"macro percent parses to underlying log cutoff target");
    anchor=presetState(0);const auto original=render(anchor);
    for(auto macro:{SimpleMacro::brightness,SimpleMacro::character,SimpleMacro::distance,SimpleMacro::space}) {
        auto low=macroTarget(macro,.05,anchor),high=macroTarget(macro,.95,anchor);
        if(macro==SimpleMacro::brightness)check(physical(low,HighCut)<physical(high,HighCut) && physical(low,HighDecay)<physical(high,HighDecay),"brighter raises filter and HF decay");
        if(macro==SimpleMacro::character)check(physical(low,ModDepth)<physical(high,ModDepth) && physical(low,ModRate)<physical(high,ModRate) && physical(low,Diffusion)<physical(high,Diffusion),"Character increases modulation and density");
        if(macro==SimpleMacro::distance)check(physical(low,EarlyLate)<physical(high,EarlyLate) && physical(low,Diffusion)<physical(high,Diffusion),"farther has more late balance and diffusion");
        if(macro==SimpleMacro::space)check(physical(low,Decay)<physical(high,Decay) && physical(low,Size)<physical(high,Size) && physical(low,Style)==physical(high,Style),"larger space increases room size and base decay without changing legacy style");
    }
    for(unsigned m=0;m<simpleMacroCount;++m) {
        auto changed=render(macroTarget(SimpleMacro(m),m==3?.75:.9,anchor));double energy=0;
        for(std::size_t n=0;n<original.size();++n){auto difference=changed[n]-original[n];energy+=difference*difference;}
        check(energy>1e-7,"each macro changes actual stereo DSP output");
        std::cout<<simpleMacros[m].display.title<<" audio difference energy "<<energy<<'\n';
    }
    if(argc>1)audioEvidence(argv[1]);
    std::cout<<"PASS seven UI projections, original preset/state preservation, ratio scaling, gesture rejection, precise text and real DSP output\n";
}
