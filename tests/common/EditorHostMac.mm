#import <Cocoa/Cocoa.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>
#include <array>
#include <cstring>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool condition,const char* label){if(!condition){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;
    tresult PLUGIN_API beginEdit(ParamID id) override{check(id==0,"bypass gesture ID");++starts;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override{check(id==0 && std::isfinite(value),"bypass gesture value");++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{check(id==0,"bypass end ID");++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static NSButton* button(NSView* parent,NSString* name) {
    for(NSView* view in parent.subviews) {
        if([view isKindOfClass:NSButton.class] && [[(NSButton*)view title] isEqualToString:name])return (NSButton*)view;
        if(auto* nested=button(view,name))return nested;
    }
    return nil;
}
static void saveImage(NSView* view,const char* path) {
    [view layoutSubtreeIfNeeded];NSBitmapImageRep* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    NSData* png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    check([png writeToFile:[NSString stringWithUTF8String:path] atomically:YES],"UI screenshot written");
}
static std::vector<std::uint8_t> state(IComponent* c) {
    MemoryStream s;check(c->getState(&s)==kResultOk,"complete component state");
    auto* b=reinterpret_cast<const std::uint8_t*>(s.getData());return {b,b+s.getSize()};
}
template<class Sample> static void run(const VST3::Hosting::Module::Ptr& module,HostApplication& host,int format,int argc,char** argv) {
    auto classes=module->getFactory().classInfos();check(classes.size()==2,"processor/controller factory pair");
    auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());
    auto reference=module->getFactory().createInstance<IComponent>(classes[0].ID());
    auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
    check(component && reference && controller,"same factory twin instances");
    check(component->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize twins/controller");
    auto handler=owned(new Handler);controller->setComponentHandler(handler.get());
    FUnknownPtr<IAudioProcessor> processor(component),twin(reference);check(processor && twin,"audio interfaces");
    FUnknownPtr<IConnectionPoint> pConnection(component),cConnection(controller);
    check(pConnection && cConnection && pConnection->connect(cConnection)==kResultOk && cConnection->connect(pConnection)==kResultOk,"runtime connection");
    // Both processors receive exactly the same complete state, including hidden targets,
    // configurations and seed. Editor preferences remain a separate controller chunk.
    MemoryStream sound;check(component->getState(&sound)==kResultOk,"initial complete sound state");
    sound.seek(0,IBStream::kIBSeekSet,nullptr);check(reference->setState(&sound)==kResultOk,"reference restores complete state");
    sound.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&sound)==kResultOk,"visible twin restores same complete state");
    sound.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&sound)==kResultOk,"controller restores all targets");
    ProcessSetup setup{kRealtime,format,64,48000};
    check(processor->setupProcessing(setup)==kResultOk && twin->setupProcessing(setup)==kResultOk,"matching sample format/setup");
    check(component->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"activate twins");
    processor->setProcessing(true);twin->setProcessing(true);
    std::array<Sample,64> left{},right{},outLeft{},outRight{},refLeft{},refRight{};
    Sample* inputs[]={left.data(),right.data()};Sample* outputs[]={outLeft.data(),outRight.data()};Sample* refOutputs[]={refLeft.data(),refRight.data()};
    AudioBusBuffers in{},out{},refOut{};in.numChannels=out.numChannels=refOut.numChannels=2;
    if constexpr(std::is_same_v<Sample,double>){in.channelBuffers64=inputs;out.channelBuffers64=outputs;refOut.channelBuffers64=refOutputs;}
    else {in.channelBuffers32=inputs;out.channelBuffers32=outputs;refOut.channelBuffers32=refOutputs;}
    ProcessContext context{};context.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;context.tempo=137;context.sampleRate=48000;
    ProcessData data{};data.numSamples=64;data.symbolicSampleSize=format;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
    auto referenceData=data;referenceData.outputs=&refOut;
    unsigned blocks=0;bool changed=false,tail=false;
    auto render=[&](bool silent=false,IParameterChanges* events=nullptr){
        for(unsigned i=0;i<64;++i){left[i]=silent?0:Sample(.7*std::sin((blocks*64+i)*.071));right[i]=silent?0:Sample(-.4*std::cos((blocks*64+i)*.113));}
        in.silenceFlags=silent?3:0;data.inputParameterChanges=referenceData.inputParameterChanges=events;
        check(processor->process(data)==kResultOk && twin->process(referenceData)==kResultOk,"twins process");
        check(std::memcmp(outLeft.data(),refLeft.data(),sizeof(outLeft))==0 && std::memcmp(outRight.data(),refRight.data(),sizeof(outRight))==0,"view changes preserve bit-exact effect audio");
        check(out.silenceFlags==refOut.silenceFlags && state(component)==state(reference),"complete state and silence flags match reference");
        for(unsigned i=0;i<64;++i){check(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]),"finite rendered samples");changed|=outLeft[i]!=left[i] || outRight[i]!=right[i];tail|=silent && (outLeft[i]!=0 || outRight[i]!=0);}
        ++blocks;context.projectTimeSamples+=64;context.projectTimeMusic+=64*137./(60*48000);
    };
    render();render();
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,760,460)];
    auto view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native editor attach");
    ViewRect size;view->getSize(&size);parent.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());
    auto* advanced=button(parent,@"Advanced");auto* bypass=button(parent,@"Bypass");
    check(advanced && bypass && button(parent,@"Preset"),"JUST title bar");check(bypass.toolTip.length>0,"bypass has tooltip");
    auto before=state(component);
    if(argc>=3 && format==kSample64){std::string file=std::string(argv[2])+"/simple.png";saveImage(parent,file.c_str());}
    for(int i=0;i<13;++i){[advanced performClick:nil];check(controller->getParamNormalized(0)==0 && handler->writes==0 && state(component)==before,"view toggle emits no sound edits");render(i>3);before=state(component);}
    if(argc>=3 && format==kSample64){std::string file=std::string(argv[2])+"/advanced.png";saveImage(parent,file.c_str());}
    MemoryStream ui;check(controller->getState(&ui)==kResultOk,"save separate editor chunk");
    view->removed();view.reset();render(true); // reference and effect continue their history/tails while UI is closed
    view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"editor reopen");
    check(button(parent,@"Advanced").state==NSControlStateValueOn && handler->writes==0,"reopen preserves view only");render(true);
    [button(parent,@"Bypass") performClick:nil];check(controller->getParamNormalized(0)==1 && handler->starts==1 && handler->writes==1 && handler->ends==1,"one complete host bypass gesture");
    ParameterChanges changes;int32 index=0,point=0;changes.addParameterData(0,index)->addPoint(0,1,point);render(false,&changes);render(true);
    view->removed();view.reset();ui.seek(0,IBStream::kIBSeekSet,nullptr);
    check(controller->setState(&ui)==kResultOk && controller->getParamNormalized(0)==1,"editor chunk cannot overwrite sound");render(true);
    if(argc>=4 && std::strcmp(argv[3],"--require-effect")==0)check(changed && tail,"test fixture proves non-pass-through and preserved tail");
    processor->setProcessing(false);twin->setProcessing(false);component->setActive(false);reference->setActive(false);
    pConnection->disconnect(cConnection);cConnection->disconnect(pConnection);
    controller->setComponentHandler(nullptr);controller->terminate();component->terminate();reference->terminate();
}
int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc>=2,"usage: just_editor_host <vst3> [screenshot-dir] [--require-effect]");
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());HostApplication host;
        run<float>(module,host,kSample32,argc,argv);run<double>(module,host,kSample64,argc,argv);
        std::cout<<"PASS float32/64 same-factory twins, complete state/seed, 13 view toggles, tails, close/reopen, isolated editor chunk, one bypass gesture\n";
    }
}
