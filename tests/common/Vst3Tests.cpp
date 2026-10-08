#include "common/vst3/Processor.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <iostream>
#include <cstdlib>
using namespace Steinberg;
using namespace Steinberg::Vst;
void check(bool value,const char* label){if(!value){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
int main() {
    auto* processor=new just::Processor;
    check(processor->initialize(nullptr)==kResultOk,"initialize");
    SpeakerArrangement mono=SpeakerArr::kMono,stereo=SpeakerArr::kStereo,surround=SpeakerArr::k51;
    check(processor->setBusArrangements(&surround,1,&surround,1)==kResultFalse,"unsupported bus");
    check(processor->setBusArrangements(&mono,1,&stereo,1)==kResultFalse,"1-to-2 pending module implementation");
    check(processor->setBusArrangements(&mono,1,&mono,1)==kResultOk,"mono bus");
    ProcessSetup setup{};setup.sampleRate=48000;setup.maxSamplesPerBlock=2048;setup.symbolicSampleSize=kSample64;
    check(processor->setupProcessing(setup)==kResultOk,"setup");
    check(processor->setActive(true)==kResultOk,"active");
    ParameterChanges changes;int32 queueIndex=0,pointIndex=0;
    auto* q=changes.addParameterData(just::bypassParamID,queueIndex);
    q->addPoint(0,1,pointIndex);
    ProcessData data{};data.symbolicSampleSize=kSample64;data.inputParameterChanges=&changes;
    check(processor->process(data)==kResultOk,"zero-sample parameter flush");
    MemoryStream stream;check(processor->getState(&stream)==kResultOk,"get state");
    stream.seek(0,IBStream::kIBSeekSet,nullptr);
    auto registry=just::moduleDefinition().parameters;just::SoundState state;
    check(just::readSoundState(&stream,just::identity().processor,registry,state) && state.targets[0]==1,"flush target retained");
    state.targets[0]=0;MemoryStream restore;check(just::writeSoundState(&restore,state,registry),"encode restored state");
    restore.seek(0,IBStream::kIBSeekSet,nullptr);check(processor->setState(&restore)==kResultOk,"restore queued");
    MemoryStream beforeProcess;check(processor->getState(&beforeProcess)==kResultOk,"immediate get restored state");
    beforeProcess.seek(0,IBStream::kIBSeekSet,nullptr);check(just::readSoundState(&beforeProcess,just::identity().processor,registry,state) && state.targets[0]==0,"pending state visible before audio");
    std::array<double,32> audio{},output{};for(unsigned i=0;i<audio.size();++i)audio[i]=double(i)-10;
    double* inputChannel=audio.data();double* outputChannel=output.data();
    AudioBusBuffers input{};input.numChannels=1;input.channelBuffers64=&inputChannel;
    AudioBusBuffers out{};out.numChannels=1;out.channelBuffers64=&outputChannel;
    data.numSamples=32;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&out;data.inputParameterChanges=nullptr;
    check(processor->process(data)==kResultOk && audio==output,"audio host pass through");
    check(processor->getLatencySamples()==0 && processor->getTailSamples()==kNoTail,"reported placeholder PDC/tail");
    processor->setActive(false);processor->terminate();processor->release();
    std::cout<<"PASS direct VST3 processor bus/state/zero-block/audio contract\n";
}
