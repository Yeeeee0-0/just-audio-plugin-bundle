// Isolated real-bundle host. --capture never orders/activates a window.
// --interactive is reserved for dot's serial QA; no synthesized input is sent.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>
#include <set>
#include <fstream>
#include <iomanip>
using namespace Steinberg;using namespace Steinberg::Vst;
static void require(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
class QAHandler final:public FObject,public IComponentHandler {
public:
 ParameterChanges changes;std::set<ParamID> active;unsigned writes=0;
 tresult PLUGIN_API beginEdit(ParamID p) override {active.insert(p);std::cout<<"BEGIN "<<p<<std::endl;return kResultOk;}
 tresult PLUGIN_API performEdit(ParamID p,ParamValue v) override {int32 index=0;auto* q=changes.addParameterData(p,index);q->addPoint(0,v,index);++writes;std::cout<<"EDIT "<<p<<" "<<std::setprecision(17)<<v<<std::endl;return kResultOk;}
 tresult PLUGIN_API endEdit(ParamID p) override {active.erase(p);std::cout<<"END "<<p<<std::endl;return kResultOk;}
 tresult PLUGIN_API restartComponent(int32) override {return kResultOk;}
 OBJ_METHODS(QAHandler,FObject)
 DEFINE_INTERFACES
 DEF_INTERFACE(IComponentHandler)
 END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};
static NSView* findView(NSView* parent,NSString* identifier){if([parent.identifier isEqualToString:identifier])return parent;for(NSView* v in parent.subviews)if(auto* match=findView(v,identifier))return match;return nil;}
static bool hasLegacyBandBar(NSView* parent){for(NSView* v in parent.subviews){if([v isKindOfClass:NSButton.class])for(int b=1;b<=12;++b)if([[(NSButton*)v title] isEqualToString:[NSString stringWithFormat:@"%02d",b]] || [[(NSButton*)v title] isEqualToString:[NSString stringWithFormat:@"%02d•",b]])return true;if(hasLegacyBandBar(v))return true;}return false;}
static void capture(NSView* parent,const std::string& path){[parent layoutSubtreeIfNeeded];auto* bitmap=[parent bitmapImageRepForCachingDisplayInRect:parent.bounds];[parent cacheDisplayInRect:parent.bounds toBitmapImageRep:bitmap];require([[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"capture pixels");}
int main(int argc,char** argv){@autoreleasepool {
 require(argc==3 || argc==4,"usage: host bundle output-directory [--capture|--interactive]");bool interactive=argc==4 && std::string(argv[3])=="--interactive";require(argc==3 || interactive || std::string(argv[3])=="--capture","explicit mode");
 [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
 std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);require(bool(module),error.c_str());HostApplication host;
 auto infos=module->getFactory().classInfos();auto component=module->getFactory().createInstance<IComponent>(infos[0].ID());auto controller=module->getFactory().createInstance<IEditController>(infos[1].ID());require(component && controller,"factory");
 require(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");FUnknownPtr<IConnectionPoint> pc(component),cc(controller);pc->connect(cc);cc->connect(pc);auto handler=owned(new QAHandler);controller->setComponentHandler(handler.get());
 auto state=just::initialState(just::pluginIdentities[0].processor,just::eq::registry);auto set=[&](int b,just::eq::Field f,double v){auto i=just::eq::index(b,f);state.targets[i]=just::eq::parameters[i].toNormalized(v);};
 const double hz[]={40,160,720,2200,6400,18000},db[]={0,-2,3.4,-2.7,2.2,0};
 for(int b=0;b<6;++b){set(b,just::eq::enabled,1);set(b,just::eq::frequency,hz[b]);set(b,just::eq::gain,db[b]);set(b,just::eq::q,1.2);}set(0,just::eq::type,3);set(5,just::eq::type,4);
 auto bytes=just::encodeState(state,just::eq::registry);MemoryStream ps(bytes.data(),bytes.size()),cs(bytes.data(),bytes.size());require(component->setState(&ps)==kResultOk && controller->setComponentState(&cs)==kResultOk,"seed real state (QA fixture only)");
 FUnknownPtr<IAudioProcessor> processor(component);ProcessSetup setup{kRealtime,kSample64,512,48000};require(processor->setupProcessing(setup)==kResultOk,"setup");component->setActive(true);processor->setProcessing(true);
 auto view=owned(controller->createView(ViewType::kEditor));NSRect bounds=NSMakeRect(0,0,1120,590);auto* parent=[[NSView alloc] initWithFrame:bounds];auto* window=[[NSWindow alloc] initWithContentRect:bounds styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.title=@"JUST EQ · isolated v0.3 native QA";window.contentView=parent;
 require(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"attach");ViewRect size(0,0,1120,590);view->onSize(&size);
 std::array<double,512> l{},r{},ol{},orr{};double* in[]={l.data(),r.data()},*out[]={ol.data(),orr.data()};AudioBusBuffers input{},output{};input.numChannels=output.numChannels=2;input.channelBuffers64=in;output.channelBuffers64=out;ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample64;data.numSamples=512;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.inputParameterChanges=&handler->changes;std::uint64_t position=0;
 auto render=[&](){for(int i=0;i<512;++i){double t=double(position+i)/48000.;l[i]=.09*std::sin(2*M_PI*720*t)+.08*std::sin(2*M_PI*180*t)+.04*std::sin(2*M_PI*6500*t);r[i]=.07*std::sin(2*M_PI*720*t)-.04*std::sin(2*M_PI*180*t)+.06*std::sin(2*M_PI*6500*t);}processor->process(data);handler->changes.clearQueue();position+=512;};
 if(interactive){[NSApp finishLaunching];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];std::cout<<"READY real native input only; deterministic real processor audio; no audible playback. Close window to exit."<<std::endl;}
 // Observe only this isolated host's native events; never generate any.
 NSObject* monitor=interactive?[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown|NSEventMaskLeftMouseUp|NSEventMaskLeftMouseDragged|NSEventMaskKeyDown|NSEventMaskScrollWheel handler:^NSEvent*(NSEvent* event){if(event.window==window)std::cout<<"NATIVE_EVENT "<<int(event.type)<<" key="<<(event.type==NSEventTypeKeyDown?event.keyCode:0)<<std::endl;return event;}]:nil;
 auto tick=[NSTimer timerWithTimeInterval:512./48000 repeats:YES block:^(NSTimer*){render();}];[[NSRunLoop currentRunLoop] addTimer:tick forMode:NSRunLoopCommonModes];
 NSDate* end=[NSDate dateWithTimeIntervalSinceNow:interactive?1800:1];
 while([end timeIntervalSinceNow]>0 && (!interactive || window.visible)){auto* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:.01] inMode:NSDefaultRunLoopMode dequeue:YES];if(event)[NSApp sendEvent:event];}
 if(!interactive){auto* panel=findView(parent,@"JustEQ.SelectedPanel");auto* canvas=findView(parent,@"JustEQ.Canvas");require(panel && canvas && findView(parent,@"JustEQ.Solo") && findView(parent,@"JustEQ.SelectedBandField"),"real bundle exposes selected panel / Solo / per-band field");require(!hasLegacyBandBar(parent),"no legacy Band 1-12 bottom bar");require(panel.frame.size.width<=388 && panel.frame.size.height<=170,"compact approved panel bounds");require(NSContainsRect(canvas.bounds,panel.frame),"panel remains within main canvas");require(!handler->writes && handler->active.empty(),"refresh/layout/capture does not edit sound");require(!window.isVisible,"offscreen test never presents a window");}
 [tick invalidate];if(monitor)[NSEvent removeMonitor:monitor];capture(parent,std::string(argv[2])+"/eq-native.png");
 std::cout<<"DONE mode="<<(interactive?"interactive":"offscreen-capture")<<" writes="<<handler->writes<<" activeGestures="<<handler->active.size()<<" nativeInteractionVerified=false (manual evidence required)"<<std::endl;
 view->removed();view=nullptr;processor->setProcessing(false);component->setActive(false);pc->disconnect(cc);cc->disconnect(pc);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();[window close];
}}
