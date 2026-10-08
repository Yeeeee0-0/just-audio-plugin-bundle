#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "common/vst3/LayoutMessages.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include <iostream>
#include <cstdlib>
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool b,const char* s){++checks;if(!b){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
static std::vector<std::uint8_t> state(just::Processor& p){MemoryStream s;check(p.getState(&s)==kResultOk,"get state");auto* b=reinterpret_cast<std::uint8_t*>(s.getData());return {b,b+s.getSize()};}
static void customTest(){
    using namespace just;
    constexpr ParameterSpec specs[]={
        {"custom.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
        {"custom.model",1,"Model","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
        {"custom.lp_enabled",2,"LP Enabled","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
        {"custom.lp_hz",3,"LP Hz","Hz",20,20000,1000,Mapping::logarithmic,0,true,"all",Transition::continuous,0},
        {"custom.hold",4,"Hold","",0,1,0,Mapping::linear,0,true,"all",Transition::continuous,0},
        {"custom.hidden",5,"Hidden","",0,1,0,Mapping::linear,0,true,"all",Transition::continuous,0}
    };
    const SimpleControlBinding controls[]={{"Model",{1},1,true,""},{"Amount",{},0,true,""},{"LP / Hold",{2,3},2,true,""}};
    auto m=moduleDefinition();m.parameters={specs,6};m.simpleControls=controls;m.simpleControlCount=3;
    check(m.valid(),"conditional slot retains max two IDs per binding");
    auto s=initialState({},m.parameters);s.targets[1]=1;s.targets[4]=.4;
    check(moduleStatus(m,s).advancedCustom,"legacy fallback treats unlisted hold as hidden");
    m.simpleHasCustom=[](const SoundState& s){
        // Test policy: LP pair visible in Model0; Hold visible in Model1.
        return s.targets[5]!=0 || (s.targets[1]>=.5?(s.targets[2]!=0 || s.targets[3]!=std::log(1000./20)/std::log(20000./20)):s.targets[4]!=0);
    };
    const auto before=encodeState(s,m.parameters);
    check(!moduleStatus(m,s).advancedCustom,"conditional hold visibility avoids false Custom");
    s.targets[5]=.2;check(moduleStatus(m,s).advancedCustom,"genuinely hidden target still Custom");s.targets[5]=0;
    check(encodeState(s,m.parameters)==before,"custom/status query preserves complete sound state");
    check(std::strstr(moduleBypassTooltip(m),"Foundation")==nullptr,"neutral fallback no false pass-through claim");
    m.bypassTooltip="module-specific bypass";check(std::strcmp(moduleBypassTooltip(m),m.bypassTooltip)==0,"module tooltip accepted");
}
int main(){
    HostApplication host;auto p=owned(new just::Processor);auto c=owned(new just::Controller);
    check(p->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"initialize");just::BusLayoutSnapshot layout;
    check(!c->readBusLayout(layout),"unknown before connection");const auto initial=state(*p);
    // Controller-first is a legal connection order.
    check(c->connect(p)==kResultOk && !c->readBusLayout(layout),"request remembered before processor peer");
    check(p->connect(c)==kResultOk && c->readBusLayout(layout),"actual layout after both connected");
    check(layout.inputChannels==2 && layout.outputChannels==2 && layout.sidechainChannels==2 && !layout.sidechainActive && !(layout.validFields&just::layoutSampleRate),"initial bus known, Fs unknown, aux inactive");
    SpeakerArrangement ins[]={SpeakerArr::kMono,SpeakerArr::kMono},out=SpeakerArr::kStereo;
    check(p->setBusArrangements(ins,2,&out,1)==kResultOk && p->activateBus(kAudio,kInput,1,true)==kResultOk,"actual mono-to-stereo with SC");
    ProcessSetup setup{kOffline,kSample64,64,96000};check(p->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk,"prepare/activate");
    check(c->readBusLayout(layout) && layout.inputChannels==1 && layout.outputChannels==2 && layout.sidechainChannels==1 && layout.sidechainActive && layout.sampleRate==96000 && layout.offline && layout.active,"actual Fs/layout/active metadata");
    check(state(*p)==initial,"all metadata operations preserve sound state bytes");
    auto old=layout;auto oldMessage=owned(p->allocateMessage());check(just::writeLayoutMessage(oldMessage,old,just::identity().processor),"encode frame");
    check(c->notify(oldMessage)==kResultFalse,"duplicate sequence ignored");
    old.sequence+=1000;check(just::writeLayoutMessage(oldMessage,old,just::identity().processor),"encode delayed frame");
    check(p->disconnect(c)==kResultOk && !c->readBusLayout(layout),"processor close invalidates cache");
    check(c->disconnect(p)==kResultOk && !c->readBusLayout(layout),"controller disconnect unknown");
    check(c->notify(oldMessage)==kResultFalse,"disconnected queued frame rejected");
    check(p->connect(c)==kResultOk && c->connect(p)==kResultOk && c->readBusLayout(layout),"processor-first reconnect");
    check(layout.session!=old.session && c->notify(oldMessage)==kResultFalse,"old session rejected after reconnect");
    auto message=owned(p->allocateMessage());auto fresh=layout;fresh.sequence+=10;
    check(just::writeLayoutMessage(message,fresh,just::identity().processor),"encode current frame");
    message->getAttributes()->setInt("version",99);check(c->notify(message)==kInvalidArgument,"unknown version rejected");
    just::writeLayoutMessage(message,fresh,just::identity().processor);message->getAttributes()->setFloat("rate",std::numeric_limits<double>::quiet_NaN());check(c->notify(message)==kInvalidArgument,"nonfinite Fs rejected");
    just::writeLayoutMessage(message,fresh,just::identity().processor);message->getAttributes()->setInt("uid0",7);check(c->notify(message)==kInvalidArgument,"wrong UID rejected");
    auto incomplete=owned(p->allocateMessage());incomplete->setMessageID(just::layoutMessageID);check(c->notify(incomplete)==kInvalidArgument,"incomplete attributes rejected");
    check(c->readBusLayout(layout) && layout.sequence==fresh.sequence-10,"malformed frames never replace cache");
    check(p->setActive(false)==kResultOk && c->readBusLayout(layout) && !layout.active && layout.sampleRate==96000,"inactive retains valid negotiated metadata");
    ins[0]=SpeakerArr::kStereo;ins[1]=SpeakerArr::kEmpty;
    check(p->setBusArrangements(ins,2,&out,1)==kResultOk && c->readBusLayout(layout) && !(layout.validFields&just::layoutSampleRate),"renegotiation invalidates prepared Fs");
    check(state(*p)==initial,"invalid and lifecycle frames preserve complete state");
    p->disconnect(c);c->disconnect(p);c->terminate();p->terminate();
    customTest();std::cout<<"PASS "<<checks<<" layout/connection/schema/custom/tooltip checks\n";
}
