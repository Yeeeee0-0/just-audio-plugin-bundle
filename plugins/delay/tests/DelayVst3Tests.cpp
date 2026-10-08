#include "../Parameters.hpp"
#include "common/vst3/Processor.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <iostream>
#include <cstdlib>
using namespace Steinberg;using namespace Steinberg::Vst;
using namespace just::delay;
void check(bool x,const char* m){if(!x){std::cerr<<"FAIL "<<m<<"\n";std::exit(1);}}
int main(){
    auto processor=owned(new just::Processor);check(processor->initialize(nullptr)==kResultOk,"initialize Delay wrapper");
    SpeakerArrangement mono=SpeakerArr::kMono,stereo=SpeakerArr::kStereo,bad=SpeakerArr::k51;
    check(processor->setBusArrangements(&mono,1,&stereo,1)==kResultOk,"1 to 2 supported");
    check(processor->setBusArrangements(&bad,1,&stereo,1)==kResultFalse,"surround rejected");
    ProcessSetup setup{kRealtime,kSample64,2048,48000};
    check(processor->setupProcessing(setup)==kResultOk && processor->setActive(true)==kResultOk,"prepare activate");
    auto state=just::initialState(just::identity().processor,registry);
    set(state,route,2);set(state,timeL,1);set(state,timeR,1);set(state,mix,100);set(state,feedback,50);set(state,start,0);
    MemoryStream restore;check(just::writeSoundState(&restore,state,registry),"encode state");restore.seek(0,IBStream::kIBSeekSet,nullptr);
    check(processor->setState(&restore)==kResultOk,"queue state");
    ProcessData data{};data.symbolicSampleSize=kSample64;
    check(processor->process(data)==kResultOk,"zero-frame state flush");
    std::array<double,2048> input{},left{},right{};input[0]=1;
    double* ins[]={input.data()};double* outs[]={left.data(),right.data()};
    AudioBusBuffers in{},out{};in.numChannels=1;in.channelBuffers64=ins;out.numChannels=2;out.channelBuffers64=outs;
    data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.numSamples=2048;
    check(processor->process(data)==kResultOk && std::abs(left[63]-1)<1e-9 && right[63]==0,"actual wrapper mono Ping-Pong first echo");
    check(processor->getLatencySamples()==15 && processor->getTailSamples()>48000,"reported PDC and finite tail");
    double echoEnergy=0;for(int i=111;i<135;++i)echoEnergy+=right[i]*right[i];check(echoEnergy>0.01,"actual wrapper crossed echo");
    ParameterChanges changes;int32 queue=0,point=0;
    auto* q=changes.addParameterData(timeR,queue);q->addPoint(31,spec(timeR).toNormalized(375),point);
    auto* mode=changes.addParameterData(route,queue);mode->addPoint(31,0,point);
    data.inputParameterChanges=&changes;data.numSamples=64;
    check(processor->process(data)==kResultOk,"sample-offset automation accepted");
    MemoryStream saved;processor->getState(&saved);saved.seek(0,IBStream::kIBSeekSet,nullptr);
    just::SoundState after;check(just::readSoundState(&saved,just::identity().processor,registry,after),"read automated state");
    check(std::abs(value(after,timeL)-1)<1e-10 && std::abs(value(after,timeR)-375)<1e-9,"right automation never folds into left Time");
    auto* frozen=changes.addParameterData(freeze,queue);frozen->addPoint(0,1,point);
    data.numSamples=0;check(processor->process(data)==kResultOk && processor->getTailSamples()==kInfiniteTail,"zero-block Freeze reports infinite tail");
    auto snapshot=just::encodeState(after,registry);snapshot[20]=2;
    MemoryStream invalid;invalid.write(snapshot.data(),int32(snapshot.size()),nullptr);invalid.seek(0,IBStream::kIBSeekSet,nullptr);
    check(processor->setState(&invalid)==kResultFalse,"unsupported state schema rejected");
    data.numSamples=0;data.inputParameterChanges=nullptr;
    const char* presets[]={"01 Init Stereo Insert.vstpreset","02 Ping-Pong Quarter Sync Insert.vstpreset","03 Ping-Pong 250ms Send.vstpreset","04 Dual 250-375ms Insert.vstpreset"};
    const auto& words=just::identity().processor.words;FUID classID(words[0],words[1],words[2],words[3]);
    for(int presetIndex=0;presetIndex<4;++presetIndex){
        auto stream=owned(FileStream::open((std::string(JUST_DELAY_PRESET_DIR)+"/"+presets[presetIndex]).c_str(),"rb"));check(bool(stream),"open preset fixture");
        PresetFile preset(stream);check(preset.readChunkList() && preset.getClassID()==classID && !preset.contains(kControllerState),"SDK validates preset identity/chunks");
        check(preset.restoreComponentState(processor.get()),"SDK loads whole component state");check(processor->process(data)==kResultOk,"preset state flush");
        MemoryStream savedPreset;processor->getState(&savedPreset);savedPreset.seek(0,IBStream::kIBSeekSet,nullptr);just::SoundState loaded;
        check(just::readSoundState(&savedPreset,just::identity().processor,registry,loaded),"preset complete state accepted");
        check(value(loaded,route)==(presetIndex==0?0:presetIndex==3?1:2),"preset route preserved");
        check(value(loaded,freeze)==0 && value(loaded,tap4Enabled)==0,"hidden preset defaults fully restored");
    }
    processor->setActive(false);processor->terminate();
    std::cout<<"PASS actual Delay VST3 processor: mono-to-stereo Ping-Pong, zero-block state/Freeze, independent R automation, PDC/tail, four SDK-loaded complete-state presets\n";
}
