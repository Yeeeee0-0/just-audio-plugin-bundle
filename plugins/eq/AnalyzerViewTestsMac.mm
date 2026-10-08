// Actual loaded EQ bundle, real processor PCM, standalone NSView only.
// Menu actions here are fixture setup, never native-input acceptance.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static id forbidWindow(id cls,SEL,...){std::cerr<<"FAIL NSWindow allocation "<<class_getName(cls)<<'\n';std::exit(2);}
static void noWindow(){check(!NSApp.isActive && NSApp.windows.count==0,"no window or activation");}
static NSView* findView(NSView* parent,NSString* identifier){if([parent.identifier isEqualToString:identifier])return parent;for(NSView* child in parent.subviews)if(auto* found=findView(child,identifier))return found;return nil;}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned edits=0;
    tresult PLUGIN_API beginEdit(ParamID) override{++edits;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue) override{++edits;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{++edits;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static std::vector<char> sound(IComponent* component){MemoryStream stream;check(component->getState(&stream)==kResultOk,"save sound state");return {stream.getData(),stream.getData()+stream.getSize()};}
static NSMenuItem* option(NSPopUpButton* button,int tag){return [[[button menu] itemWithTag:tag/100*100].submenu itemWithTag:tag];}
int main(int argc,char** argv){@autoreleasepool {
    check(argc==3,"usage exact-bundle existing-capture-directory");
    for(SEL selector:{@selector(alloc),@selector(allocWithZone:)}){auto method=class_getClassMethod(NSWindow.class,selector);class_replaceMethod(object_getClass(NSWindow.class),selector,(IMP)forbidWindow,method_getTypeEncoding(method));}
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];noWindow();
    std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());HostApplication host;
    auto classes=module->getFactory().classInfos();check(classes.size()==2,"EQ processor/controller class pair");
    auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
    check(component && controller && component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize actual EQ");
    FUnknownPtr<IConnectionPoint> pc(component),cc(controller);check(pc->connect(cc)==kResultOk && cc->connect(pc)==kResultOk,"connect real processor/controller");
    auto handler=owned(new Handler);controller->setComponentHandler(handler);
    auto state=just::initialState(just::pluginIdentities[0].processor,just::eq::registry);
    auto set=[&](just::eq::Field field,double value){auto i=just::eq::index(0,field);state.targets[i]=just::eq::parameters[i].toNormalized(value);};
    set(just::eq::enabled,1);set(just::eq::frequency,1000);set(just::eq::gain,-6);set(just::eq::q,1);
    auto bytes=just::encodeState(state,just::eq::registry);MemoryStream ps(bytes.data(),bytes.size()),cs(bytes.data(),bytes.size());
    check(component->setState(&ps)==kResultOk && controller->setComponentState(&cs)==kResultOk,"seed Bell -6dB fixture");
    const auto original=sound(component);
    FUnknownPtr<IAudioProcessor> processor(component);ProcessSetup setup{kRealtime,kSample64,512,48000};
    check(processor->setupProcessing(setup)==kResultOk && component->setActive(true)==kResultOk,"prepare PCM processing");
    // The inherited SDK AudioEffect hook returns kNotImplemented; real process
    // callbacks below are still required to succeed, as in the existing host.
    const auto processing=processor->setProcessing(true);check(processing==kResultOk || processing==kNotImplemented,"optional processing hook");
    NSView* root=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1000,720)];auto view=owned(controller->createView(ViewType::kEditor));
    check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"attach standalone NSView");ViewRect size(0,0,1000,720);view->onSize(&size);
    auto* analyzer=(NSPopUpButton*)findView(root,@"JustEQ.Analyzer");check(analyzer && !analyzer.hidden && analyzer.menu.numberOfItems==6,"one compact Analyzer entry and five submenus");
    auto* canvas=findView(root,@"JustEQ.Canvas");check(canvas && NSMaxY(analyzer.frame)<=20,"entry fits existing top margin");
    auto choose=[&](int tag){auto* item=option(analyzer,tag);check(item && item.target,"real analyzer menu item");check([NSApp sendAction:item.action to:item.target from:item],"fixture chooses actual menu action");noWindow();};
    auto selected=[&](int tag){check(option(analyzer,tag).state==NSControlStateValueOn,"expected analyzer menu checkmark");};
    std::array<double,512> left{},right{},outLeft{},outRight{};double* inputs[]={left.data(),right.data()},*outputs[]={outLeft.data(),outRight.data()};
    AudioBusBuffers input{},output{};input.numChannels=output.numChannels=2;input.channelBuffers64=inputs;output.channelBuffers64=outputs;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample64;data.numSamples=512;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;
    std::uint64_t position=0;std::uint32_t random=0x41515;bool silence=false;
    auto render=[&](unsigned blocks){for(unsigned block=0;block<blocks;++block){for(unsigned i=0;i<512;++i){random^=random<<13;random^=random>>17;random^=random<<5;double noise=(double(random)/4294967295.-.5)*.001;const double t=double(position+i)/48000.;const double x=silence?0:.02*std::sin(2*M_PI*100*t)+.055*std::sin(2*M_PI*1000*t)+.015*std::sin(2*M_PI*10000*t)+noise;left[i]=x;right[i]=-x;}
        check(processor->process(data)==kResultOk,"real anti-phase stereo PCM processed");position+=512;
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:512./48000]];noWindow();}}
    ;
    auto capture=[&](const char* name){[(NSObject*)canvas performSelector:@selector(showSelection)];[(NSObject*)canvas performSelector:@selector(sync)];[root layoutSubtreeIfNeeded];
        auto* image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];[root cacheDisplayInRect:root.bounds toBitmapImageRep:image];
        check(canvas.toolTip.length==0,"main graph never registers the removed explanatory tooltip");
        check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithFormat:@"%s/%s",argv[2],name] atomically:YES],"capture actual loaded view pixels");noWindow();};
    render(100);selected(102);selected(202);selected(301);selected(400);selected(501);capture("01-default-120-tilt45-4096.png");
    choose(202);choose(300);render(60);selected(202);selected(300);capture("02-measurement-120-tilt0-4096.png");
    choose(401);render(90);selected(401);capture("03-fine-120-tilt0-8192.png");
    choose(200);choose(301);choose(500);render(30);selected(200);selected(500);capture("04-range60-fast.png");
    silence=true;render(230);capture("05-silence.png");
    MemoryStream savedUi;check(controller->getState(&savedUi)==kResultOk,"save analyzer UI preferences");
    choose(201);choose(300);choose(400);choose(502);choose(100);
    check(view->removed()==kResultOk,"close editor before preference restore");view=nullptr;
    savedUi.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setState(&savedUi)==kResultOk,"restore saved analyzer UI state");
    view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"reopen instance after UI restore");view->onSize(&size);
    analyzer=(NSPopUpButton*)findView(root,@"JustEQ.Analyzer");canvas=findView(root,@"JustEQ.Canvas");
    render(20);selected(102);selected(200);selected(301);selected(401);selected(500);capture("06-ui-preferences-restored.png");
    check(view->removed()==kResultOk,"detach for legacy UI load");view=nullptr;
    MemoryStream legacy;IBStreamer old(&legacy,kLittleEndian);
    check(old.writeInt32(2) && old.writeInt32(1000) && old.writeInt32(720) && old.writeDouble(1) && old.writeBool(false) && old.writeInt32(1) && old.writeBool(true) && old.writeBool(false) && old.writeBool(false) && old.writeInt32(30) && old.writeDouble(1),"make legacy UIv2 fixture");
    legacy.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setState(&legacy)==kResultOk,"load old UIv2 without analyzer preferences");
    view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"reopen old UI state");view->onSize(&size);
    analyzer=(NSPopUpButton*)findView(root,@"JustEQ.Analyzer");canvas=findView(root,@"JustEQ.Canvas");
    render(20);selected(102);selected(202);selected(301);selected(400);selected(501);capture("07-legacy-ui-defaults.png");
    check(controller->getParameterCount()==195 && handler->edits==0 && sound(component)==original,"all display settings leave195 parameters and sound bytes unchanged");
    view->removed();view=nullptr;processor->setProcessing(false);component->setActive(false);pc->disconnect(cc);cc->disconnect(pc);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();noWindow();
    std::cout<<"PASS "<<checks<<" loaded analyzer PCM/menu/UI-state fixture checks; seven pixel captures; no windows/native input acceptance\n";
}}
