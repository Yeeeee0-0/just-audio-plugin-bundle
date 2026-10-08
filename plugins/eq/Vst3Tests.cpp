#include "Engine.hpp"
#include "common/vst3/Processor.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool b,const char* s){if(!b){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
int main(){
    auto* p=new just::Processor;check(p->initialize(nullptr)==kResultOk,"initialize");
    SpeakerArrangement ins[]={SpeakerArr::kStereo,SpeakerArr::kEmpty},out=SpeakerArr::kStereo;check(p->setBusArrangements(ins,2,&out,1)==kResultOk,"stereo optional disconnected SC");
    ProcessSetup setup{kRealtime,kSample64,256,48000};check(p->setupProcessing(setup)==kResultOk,"setup");p->setActive(true);
    ParameterChanges changes;int32 qindex=0,point=0;auto add=[&](just::eq::Field f,double v,int offset){auto i=just::eq::index(0,f);auto* q=changes.addParameterData(just::eq::parameters[i].id,qindex);q->addPoint(offset,just::eq::parameters[i].toNormalized(v),point);};
    add(just::eq::enabled,1,23);add(just::eq::target,1,23);add(just::eq::gain,12,23);
    std::array<double,256> l,r,ol{},orr{};for(int i=0;i<256;++i){l[i]=std::sin(2*3.141592653589793*1000*i/48000);r[i]=-l[i];}
    double* ip[]={l.data(),r.data()},*op[]={ol.data(),orr.data()};AudioBusBuffers input{},output{};input.numChannels=output.numChannels=2;input.channelBuffers64=ip;output.channelBuffers64=op;
    ProcessData data{};data.symbolicSampleSize=kSample64;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.inputParameterChanges=&changes;
    check(p->process(data)==kResultOk,"sample-offset automation");check(l==ol && r==orr,"Mid only leaves pure Side exact through VST3 wrapper");
    MemoryStream saved;check(p->getState(&saved)==kResultOk,"save");saved.seek(0,IBStream::kIBSeekSet,nullptr);just::SoundState state;check(just::readSoundState(&saved,just::identity().processor,just::eq::registry,state),"decode full state");
    check(just::eq::physical(state,just::eq::index(0,just::eq::target))==1,"field serialized");
    data.numSamples=0;data.numInputs=data.numOutputs=0;data.inputs=data.outputs=nullptr;ParameterChanges flush;auto* q=flush.addParameterData(just::eq::id(0,just::eq::target),qindex);q->addPoint(0,1,point);data.inputParameterChanges=&flush;
    check(p->process(data)==kResultOk,"zero block field flush");MemoryStream after;p->getState(&after);after.seek(0,IBStream::kIBSeekSet,nullptr);check(just::readSoundState(&after,just::identity().processor,just::eq::registry,state) && just::eq::physical(state,just::eq::index(0,just::eq::target))==2,"zero block field state retained");
    check(p->getLatencySamples()==0 && p->getTailSamples()>0,"zero latency and finite ringout");p->setActive(false);p->terminate();p->release();std::cout<<"PASS EQ VST3 buses, automation offsets, zero-block field, full state, PDC/tail\n";
    // Audible sample-offset case: real Mid tone, plus a per-sample reference that
    // independently constructs the VST3 ramp and the discrete gate at sample 23.
    p=new just::Processor;check(p->initialize(nullptr)==kResultOk,"offset initialize");check(p->setBusArrangements(ins,2,&out,1)==kResultOk,"offset stereo setup");setup.maxSamplesPerBlock=512;check(p->setupProcessing(setup)==kResultOk,"offset processing setup");p->setActive(true);
    std::array<double,512> mid,actualL{},actualR{},expected{},early{},late{};
    for(int i=0;i<512;++i)mid[i]=0.2*std::sin(2*just::eq::pi*1000*i/48000);
    auto reference=[&](int gateOffset,std::array<double,512>& result){
        just::eq::EqEngine e;check(e.prepare({48000,512,2,2}),"reference prepare");auto s=just::initialState(just::identity().processor,just::eq::registry);
        auto gi=just::eq::index(0,just::eq::gain),ei=just::eq::index(0,just::eq::enabled),ti=just::eq::index(0,just::eq::target);
        double first=s.targets[gi],last=just::eq::parameters[gi].toNormalized(12);
        for(int n=0;n<512;++n){s.targets[gi]=n<23?first+(last-first)*double(n+1)/24:last;s.targets[ei]=n>=gateOffset?1:0;s.targets[ti]=n>=gateOffset?0.5:0;double other=0;e.applyTargets(s,n);e.process(just::AudioBlock<double>{{mid.data()+n,mid.data()+n},{result.data()+n,&other},2,2,1,0},{});}
    };
    reference(23,expected);reference(22,early);reference(24,late);
    ParameterChanges audible;auto addAudible=[&](just::eq::Field f,double value){auto i=just::eq::index(0,f);auto* q=audible.addParameterData(just::eq::parameters[i].id,qindex);q->addPoint(23,just::eq::parameters[i].toNormalized(value),point);};
    addAudible(just::eq::enabled,1);addAudible(just::eq::target,1);addAudible(just::eq::gain,12);
    double* midIn[]={mid.data(),mid.data()},*actualOut[]={actualL.data(),actualR.data()};input.channelBuffers64=midIn;output.channelBuffers64=actualOut;
    data.numSamples=512;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.inputParameterChanges=&audible;check(p->process(data)==kResultOk,"audible automation process");
    double error=0,change=0,earlyError=0,lateError=0;
    for(int n=0;n<512;++n){error=std::max(error,std::abs(actualL[n]-expected[n]));change=std::max(change,std::abs(actualL[n]-mid[n]));earlyError=std::max(earlyError,std::abs(actualL[n]-early[n]));lateError=std::max(lateError,std::abs(actualL[n]-late[n]));if(n<23)check(actualL[n]==mid[n],"before offset stays exact dry");}
    check(error<2e-14 && actualL==actualR,"actual host output matches smoothed/crossfaded sample reference");
    check(change>0.01 && earlyError>1e-7 && lateError>1e-7,"offset test detects audible processing and one-sample early/late events");
    std::cout<<"OFFSET23 max_reference_error="<<error<<" max_audio_change="<<change<<" wrong_offset22_error="<<earlyError<<" wrong_offset24_error="<<lateError<<"\n";
    // Legacy schema1 contains only ID0=On. All new parameters must fill Init,
    // while the original bypass target survives transactional restore.
    just::ParameterRegistry oldRegistry{just::eq::parameters,1};auto oldState=just::initialState(just::identity().processor,oldRegistry);oldState.targets[0]=1;
    MemoryStream old;check(just::writeSoundState(&old,oldState,oldRegistry),"encode legacy ID0 On");old.seek(0,IBStream::kIBSeekSet,nullptr);check(p->setState(&old)==kResultOk,"restore legacy ID0 On");
    MemoryStream restored;p->getState(&restored);restored.seek(0,IBStream::kIBSeekSet,nullptr);check(just::readSoundState(&restored,just::identity().processor,just::eq::registry,state),"read expanded legacy state");
    auto init=just::initialState(just::identity().processor,just::eq::registry);init.targets[0]=1;check(state.targets==init.targets,"legacy On survives and all 182 new targets fill declared Init");
    data.inputParameterChanges=nullptr;check(p->process(data)==kResultOk,"render restored bypass fade");for(int n=240;n<512;++n)check(actualL[n]==mid[n] && actualR[n]==mid[n],"legacy On is exact dry after declared 5ms bypass ramp");
    p->setActive(false);p->terminate();p->release();std::cout<<"PASS audible offset23 against per-sample reference and legacy ID0=On defaults/audio\n";
}
