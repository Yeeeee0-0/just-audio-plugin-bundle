// Serial QA fixture: real loaded VST3, live test audio and OS input only.
// It emits no NSEvent/CGEvent and never calls a control's mouse/key/delegate methods.
#import <Cocoa/Cocoa.h>
#include "../Parameters.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
 unsigned begins=0,writes=0,ends=0,depth=0,maxDepth=0;std::vector<std::pair<Steinberg::Vst::ParamID,double>> pending;std::ofstream events;
 void log(const char* action,Steinberg::Vst::ParamID id,double value=0){events<<std::setprecision(17)<<"{\"event\":\""<<action<<"\",\"id\":"<<id<<",\"value\":"<<value<<",\"depth\":"<<depth<<"}\n";events.flush();}
 tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id)override{++begins;++depth;maxDepth=std::max(maxDepth,depth);log("begin",id);return kResultOk;}
 tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,ParamValue value)override{++writes;pending.emplace_back(id,value);log("write",id,value);return kResultOk;}
 tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id)override{++ends;if(depth)--depth;log("end",id);return kResultOk;}
 tresult PLUGIN_API restartComponent(int32)override{return kResultOk;}
 OBJ_METHODS(Handler,FObject) DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
@interface FlangerQADelegate : NSObject <NSApplicationDelegate,NSWindowDelegate> {
@public
 std::string bundlePath,evidencePath;VST3::Hosting::Module::Ptr module;HostApplication host;
 IPtr<IComponent> component;IPtr<IEditController> controller;IPtr<IAudioProcessor> processor;IPtr<IPlugView> view;IPtr<Handler> handler;
 IPtr<IConnectionPoint> processorConnection,controllerConnection;
 NSWindow* window;NSView* editorParent;NSTextField* status;NSTimer* timer;NSButton* timingButton;
 std::array<double,256> left,right,outL,outR;std::uint64_t position,ticks;bool hostTiming;
}
@end
@implementation FlangerQADelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notice {
 hostTiming=true;position=ticks=0;std::string error;module=VST3::Hosting::Module::create(bundlePath,error);check(bool(module),error.c_str());auto classes=module->getFactory().classInfos();
 component=module->getFactory().createInstance<IComponent>(classes[0].ID());controller=module->getFactory().createInstance<IEditController>(classes[1].ID());check(component && controller,"real factory");
 check(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");handler=owned(new Handler);handler->events.open(evidencePath+"/native-input-events.jsonl");controller->setComponentHandler(handler);
 processor=FUnknownPtr<IAudioProcessor>(component);processorConnection=FUnknownPtr<IConnectionPoint>(component);controllerConnection=FUnknownPtr<IConnectionPoint>(controller);
 check(processorConnection && controllerConnection && processorConnection->connect(controllerConnection)==kResultOk && controllerConnection->connect(processorConnection)==kResultOk,"real runtime bridge");
 ProcessSetup setup{kRealtime,kSample64,256,48000};check(processor->setupProcessing(setup)==kResultOk && component->setActive(true)==kResultOk && processor->setProcessing(true)==kResultOk,"prepare actual audio");
 MemoryStream saved;check(component->getState(&saved)==kResultOk,"complete initial state");saved.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&saved)==kResultOk,"load initial controller state");
 view=owned(controller->createView(ViewType::kEditor));check(bool(view),"real editor");ViewRect size;view->getSize(&size);const int w=std::max(880,size.getWidth()),h=std::max(584,size.getHeight());ViewRect desired(0,0,w,h);view->checkSizeConstraint(&desired);
 window=[[NSWindow alloc]initWithContentRect:NSMakeRect(160,100,desired.getWidth(),desired.getHeight()+80) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];window.title=@"JUST Flanger v0.3 · isolated native input QA";window.releasedWhenClosed=NO;window.delegate=self;
 editorParent=[[NSView alloc]initWithFrame:NSMakeRect(0,80,desired.getWidth(),desired.getHeight())];[window.contentView addSubview:editorParent];check(view->attached((__bridge void*)editorParent,kPlatformTypeNSView)==kResultOk && view->onSize(&desired)==kResultOk,"native attach/size");
 status=[NSTextField wrappingLabelWithString:@""];status.frame=NSMakeRect(18,8,desired.getWidth()-225,58);status.font=[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];[window.contentView addSubview:status];
 timingButton=[NSButton buttonWithTitle:@"Test host timing: ON" target:self action:@selector(toggleTiming:)];timingButton.frame=NSMakeRect(desired.getWidth()-205,28,185,28);[window.contentView addSubview:timingButton];
 timer=[NSTimer scheduledTimerWithTimeInterval:1./30 target:self selector:@selector(tick:) userInfo:nil repeats:YES];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
- (void)toggleTiming:(id)sender {hostTiming=!hostTiming;timingButton.title=hostTiming?@"Test host timing: ON":@"Test host timing: OFF";}
- (void)tick:(id)sender {
 ParameterChanges events;for(const auto& point:handler->pending){int32 index=0,at=0;events.addParameterData(point.first,index)->addPoint(0,point.second,at);}handler->pending.clear();
 double* input[]={left.data(),right.data()};double* output[]={outL.data(),outR.data()};AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers64=input;out.channelBuffers64=output;
 Steinberg::Vst::ProcessContext context{};context.sampleRate=48000;context.tempo=137;context.timeSigNumerator=4;context.timeSigDenominator=4;context.state=Steinberg::Vst::ProcessContext::kPlaying;
 if(hostTiming)context.state|=Steinberg::Vst::ProcessContext::kTempoValid|Steinberg::Vst::ProcessContext::kTimeSigValid|Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
 ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
 for(unsigned block=0;block<6;++block){for(unsigned i=0;i<256;++i){left[i]=.2*std::sin((position+i)*.1305066);right[i]=.16*std::sin((position+i)*.071);}
  context.projectTimeSamples=position;context.projectTimeMusic=position*137./(60*48000);data.inputParameterChanges=block==0?&events:nullptr;check(processor->process(data)==kResultOk,"real test audio");position+=256;}
 ++ticks;status.stringValue=[NSString stringWithFormat:@"OS input only · real VST3/test tone 48k stereo, test-host tempo 137 BPM\nbegin=%u write=%u end=%u depth=%u maxDepth=%u · frames=%llu",handler->begins,handler->writes,handler->ends,handler->depth,handler->maxDepth,ticks];
 std::ofstream snapshot(evidencePath+"/native-input-state.json");snapshot<<std::setprecision(17)<<"{\"begins\":"<<handler->begins<<",\"writes\":"<<handler->writes<<",\"ends\":"<<handler->ends<<",\"depth\":"<<handler->depth<<",\"maxDepth\":"<<handler->maxDepth<<",\"ticks\":"<<ticks<<",\"targets\":{";
 for(unsigned i=0;i<just::flanger::registry.count;++i){const auto& p=just::flanger::parameters[i];if(i)snapshot<<',';snapshot<<'"'<<p.id<<"\":"<<controller->getParamNormalized(p.id);}snapshot<<"}}\n";
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app{return YES;}
- (void)applicationWillTerminate:(NSNotification*)notice {
 [timer invalidate];if(view){view->removed();view.reset();}if(processor){processor->setProcessing(false);component->setActive(false);}if(processorConnection && controllerConnection){processorConnection->disconnect(controllerConnection);controllerConnection->disconnect(processorConnection);}if(controller){controller->setComponentHandler(nullptr);controller->terminate();}if(component)component->terminate();
}
@end
int main(int argc,char** argv){@autoreleasepool{check(argc==3,"usage <real-vst3> <existing-evidence-dir>; serial QA only");[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];FlangerQADelegate* delegate=[FlangerQADelegate new];delegate->bundlePath=argv[1];delegate->evidencePath=argv[2];NSApp.delegate=delegate;[NSApp run];}}
