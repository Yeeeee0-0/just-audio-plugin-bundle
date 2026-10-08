#include "common/vst3/Module.hpp"
#include "common/runtime/AnalysisCollector.hpp"
#include <cstdlib>
#include <new>
#include <iostream>
using namespace just;
static thread_local bool realtime=false;
static unsigned allocations=0,releases=0,checks=0;
void* operator new(std::size_t n){if(realtime)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(realtime && p)++releases;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
struct Capture {
    unsigned windows=0,raw=0;bool valid=true,aligned=true,paired=true,tap=true;
    static bool send(void* p,const AnalysisMessage& m){auto& c=*static_cast<Capture*>(p);c.valid&=m.valid();
        if(m.kind==AnalysisKind::envelope){++c.windows;const auto& w=m.window;c.aligned&=(w.header.flags&analysisInputAligned)!=0;c.paired&=w.channels[0].peak==w.channels[2].peak && w.channels[1].peak==w.channels[3].peak && w.channels[0].rms==w.channels[2].rms && w.channels[1].rms==w.channels[3].rms;
            unsigned expected=JUST_FEEDBACK_INDEX==4?analysisReduction:JUST_FEEDBACK_INDEX==6?analysisGate|analysisReduction:JUST_FEEDBACK_INDEX==3?analysisModulation:JUST_FEEDBACK_INDEX==7?analysisDelay:0;c.tap&=(w.effectFields&expected)==expected;
        }else{++c.raw;const auto& s=m.sampleFrame;for(unsigned i=0;i<s.count;++i)c.paired&=s.samples[0][i]==s.samples[2][i] && s.samples[1][i]==s.samples[3][i];}return true;
    }
};
template<class S> static void exercise(double fs){
    const auto& definition=moduleDefinition();std::unique_ptr<Engine> engine(definition.createEngine());PrepareSpec spec{fs,256,2,2};check(engine->prepare(spec),"prepare actual module");
    auto sound=initialState({},definition.parameters);sound.targets[definition.parameters.index(0)]=1;engine->applyTargets(sound,0);
    AnalysisCollector collector;Capture capture;check(collector.prepare(fs,256,engine->latencySamples()),"prepare actual module PDC collector");collector.setSender({},&capture,Capture::send);collector.setEnabled(true);
    std::array<S,256> l{},r{};const S* in[]={l.data(),r.data()};S* out[]={l.data(),r.data()};
    for(unsigned pass=0;pass<32;++pass){for(unsigned i=0;i<256;++i){l[i]=S(.4*std::sin((pass*256+i)*.13));r[i]=-l[i];}
        AudioBlock<S> first;first.inputChannels=first.outputChannels=2;first.samples=17;first.inputs={l.data(),r.data()};first.outputs={l.data(),r.data()};auto second=first;second.samples=239;second.inputs={l.data()+17,r.data()+17};second.outputs={l.data()+17,r.data()+17};
        ProcessContext context;context.analysis=&collector;context.playing=true;
        realtime=true;collector.capture(in,2,0,256);collector.captureBypass(0,256,true);engine->process(first,context);context.blockSampleOffset=17;engine->process(second,context);collector.finish(out,2,2,256,1,analysisPlaying|analysisTransportKnown);engine->endBlock();realtime=false;
    }
    check(capture.windows>=4 && capture.raw>=7 && capture.valid && capture.aligned,"real module produces finite timed paired data at actual Fs");check(capture.paired,"actual bypass samples/envelopes align at module PDC before in-place overwrite");check(capture.tap,"effect producer fills every window across automation subranges");check(!allocations && !releases,"analysis-enabled actual module performs no C++ allocation/release");
}
int main(){for(double fs:{44100.,48000.,96000.,192000.}){exercise<float>(fs);exercise<double>(fs);}std::cout<<"PASS analysis-enabled module "<<JUST_FEEDBACK_INDEX<<": "<<checks<<" checks; four Fs, float32/64, in-place, split offsets, actual fixed PDC, producer completeness; C++ alloc/release="<<allocations<<"/"<<releases<<"\n";}
