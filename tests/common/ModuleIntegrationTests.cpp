#include "TestModule.hpp"
#include "common/vst3/Processor.hpp"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <iostream>
#include <cstdlib>
using namespace Steinberg;using namespace Steinberg::Vst;
static thread_local bool inProcess=false;
static unsigned allocationCalls=0,releaseCalls=0;
void* operator new(std::size_t n){if(inProcess)++allocationCalls;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{if(inProcess && p)++releaseCalls;std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void check(bool result,const char* label){if(!result){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
int main() {
    auto processor=owned(new just::Processor);check(processor->initialize(nullptr)==kResultOk,"fixture injection initialized");
    SpeakerArrangement ins[]={SpeakerArr::kMono,SpeakerArr::kStereo},outArrangement=SpeakerArr::kStereo;
    check(processor->setBusArrangements(ins,2,&outArrangement,1)==kResultOk,"module enables 1-to-2 and SC");
    processor->activateBus(kAudio,kInput,1,true);
    ProcessSetup setup{kRealtime,kSample64,16,48000};check(processor->setupProcessing(setup)==kResultOk,"fixture prepared");
    processor->setActive(true);auto resets=just::test::trace.resetCalls,prepares=just::test::trace.prepareCalls;
    ParameterChanges changes;int32 index=0,point=0;
    auto* a=changes.addParameterData(1,index);a->addPoint(0,0,point);a->addPoint(3,0.75,point);a->addPoint(7,1,point);
    auto* b=changes.addParameterData(2,index);b->addPoint(3,0.5,point);b->addPoint(7,0.25,point);
    auto* mode=changes.addParameterData(3,index);mode->addPoint(3,1,point);
    std::array<double,8> audio{},left{},right{},sc{};double* input[]={audio.data()};double* output[]={left.data(),right.data()};double* scChannels[]={sc.data(),sc.data()};
    AudioBusBuffers inputs[2]{};inputs[0].numChannels=1;inputs[0].channelBuffers64=input;inputs[1].numChannels=2;inputs[1].channelBuffers64=scChannels;
    AudioBusBuffers out{};out.numChannels=2;out.channelBuffers64=output;
    ProcessContext context{};context.state=ProcessContext::kTempoValid|ProcessContext::kPlaying;context.tempo=std::numeric_limits<double>::quiet_NaN();context.projectTimeSamples=123;
    ProcessData data{};data.numSamples=8;data.symbolicSampleSize=kSample64;data.numInputs=2;data.numOutputs=1;data.inputs=inputs;data.outputs=&out;data.inputParameterChanges=&changes;data.processContext=&context;
    inProcess=true;auto result=processor->process(data);inProcess=false;
    check(result==kResultOk && allocationCalls==0 && releaseCalls==0,"wrapper process no C++ new/delete");
    check(just::test::trace.targets[1]==1 && just::test::trace.targets[2]==0.25 && just::test::trace.targets[3]==1,"all queues reach full targets");
    check(just::test::trace.applyCalls==8 && just::test::trace.offset==7,"all sample positions delivered");
    check(just::test::trace.context.blockSampleOffset==7 && just::test::trace.context.projectSamples==123,"segment retains original block origin");
    check(!just::test::trace.context.tempoValid && just::test::trace.sidechainChannels==2,"invalid BPM marked unavailable and SC pointers forwarded");
    check(just::test::trace.resetCalls==resets && just::test::trace.prepareCalls==prepares,"automation never reparses or reprepares engine");
    processor->setActive(false);processor->terminate();
    std::cout<<"PASS module injection, multi-queue sample curves, 1-to-2/SC, context offsets and process C++ allocation guard\n";
}
