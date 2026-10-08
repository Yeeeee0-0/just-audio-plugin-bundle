#import <Cocoa/Cocoa.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "plugins/limiter/Parameters.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool b,const char* why){if(!b){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
 std::ofstream log;ParameterChanges queue{16};unsigned starts=0,writes=0,ends=0;
 explicit Handler(const std::string& directory):log(directory+"/native-input-events.jsonl"){}
 tresult PLUGIN_API beginEdit(ParamID id)override{++starts;log<<"{\"event\":\"begin\",\"id\":"<<id<<"}\n";log.flush();return kResultOk;}
 tresult PLUGIN_API performEdit(ParamID id,ParamValue v)override{++writes;int32 qi=0,pi=0;queue.addParameterData(id,qi)->addPoint(0,v,pi);log.precision(17);log<<"{\"event\":\"write\",\"id\":"<<id<<",\"normalized\":"<<v<<"}\n";log.flush();return kResultOk;}
 tresult PLUGIN_API endEdit(ParamID id)override{++ends;log<<"{\"event\":\"end\",\"id\":"<<id<<"}\n";log.flush();return kResultOk;}
 tresult PLUGIN_API restartComponent(int32)override{return kResultOk;}
 OBJ_METHODS(Handler,FObject)
 DEFINE_INTERFACES
  DEF_INTERFACE(IComponentHandler)
 END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};
class Frame final:public FObject,public IPlugFrame {
public:
 NSWindow* window=nil;NSView* parent=nil;
 tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* r)override{
  if(!r||!window)return kResultFalse;
  const NSSize oldSize=window.contentView.bounds.size,oldMinimum=window.contentMinSize;
  ViewRect minimum{0,0,0,0};if(view->checkSizeConstraint(&minimum)!=kResultTrue)return kResultFalse;
  // The final shell's constraints follow renderScale. The QA host must not
  // impose an unscaled 880x600 floor on an otherwise valid 75% request.
  window.contentMinSize=NSMakeSize(minimum.getWidth(),minimum.getHeight()+44);
  [window setContentSize:NSMakeSize(r->getWidth(),r->getHeight()+44)];
  const auto actual=window.contentView.bounds.size;
  if(std::abs(actual.width-r->getWidth())>.5||std::abs(actual.height-r->getHeight()-44)>.5){window.contentMinSize=oldMinimum;[window setContentSize:oldSize];return kResultFalse;}
  parent.frame=NSMakeRect(0,0,r->getWidth(),r->getHeight());return view->onSize(r);
 }
 OBJ_METHODS(Frame,FObject)
 DEFINE_INTERFACES
  DEF_INTERFACE(IPlugFrame)
 END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};
struct Session {
 IAudioProcessor* processor=nullptr;IEditController* controller=nullptr;IPlugView* view=nullptr;Handler* handler=nullptr;
 std::uint64_t position=0;unsigned probe=0;std::string directory;NSView* parent=nil;
 double peak=0;unsigned captures=0;
 void render(){
  std::array<double,960> l{},r{},a{},b{};
  for(unsigned i=0;i<960;++i){double t=double(position+i);l[i]=probe==3?0:probe==2?((position+i)%24000==0?4:0):(probe==1?4:.25)*std::sin(t*2*3.14159265358979323846*1000/48000);r[i]=-.6*l[i];}
  double* ins[]={l.data(),r.data()},*outs[]={a.data(),b.data()};AudioBusBuffers input{},output{};input.numChannels=output.numChannels=2;input.channelBuffers64=ins;output.channelBuffers64=outs;
  ProcessContext context{};context.state=ProcessContext::kPlaying;context.projectTimeSamples=position;context.sampleRate=48000;
  ProcessData data{};data.numSamples=960;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.inputParameterChanges=&handler->queue;data.processContext=&context;
  check(processor->process(data)==kResultOk,"actual VST3 audio callback");handler->queue.clearQueue();position+=960;peak=0;for(auto x:a)peak=std::max(peak,std::abs(x));
 }
 void capture(){
  [parent layoutSubtreeIfNeeded];auto* rep=[parent bitmapImageRepForCachingDisplayInRect:parent.bounds];[parent cacheDisplayInRect:parent.bounds toBitmapImageRep:rep];auto data=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  auto name=directory+"/capture-"+std::to_string(++captures)+".png";check([data writeToFile:[NSString stringWithUTF8String:name.c_str()] atomically:YES],"native pixels saved");
  std::ofstream state(directory+"/native-input-state.json");state.precision(17);state<<"{\"starts\":"<<handler->starts<<",\"writes\":"<<handler->writes<<",\"ends\":"<<handler->ends<<",\"samplePeak\":"<<peak<<",\"pdcSamples\":"<<processor->getLatencySamples()<<",\"framesProcessed\":"<<position<<",\"targets\":[";
  for(unsigned id=0;id<just::limiter::registry.count;++id){if(id)state<<',';state<<controller->getParamNormalized(id);}state<<"]}\n";
  std::cout<<"CAPTURE "<<name<<" gestures "<<handler->starts<<'/'<<handler->writes<<'/'<<handler->ends<<'\n';
 }
};
@interface LimiterQaDelegate:NSObject<NSWindowDelegate> { @public Session* session; }
- (void)tick:(id)sender;
- (void)probe:(NSPopUpButton*)sender;
- (void)capture:(id)sender;
@end
@implementation LimiterQaDelegate
- (void)tick:(id)sender{session->render();}
- (void)probe:(NSPopUpButton*)sender{session->probe=unsigned(sender.indexOfSelectedItem);}
- (void)capture:(id)sender{session->capture();}
- (void)windowDidResize:(NSNotification*)note{auto* window=(NSWindow*)note.object;auto size=window.contentView.bounds.size;session->parent.frame=NSMakeRect(0,0,size.width,size.height-44);ViewRect r{0,0,int32(size.width),int32(size.height-44)};session->view->onSize(&r);}
- (void)windowWillClose:(NSNotification*)note{[NSApp stop:nil];}
@end
int main(int argc,char** argv){@autoreleasepool{
 check(argc==4,"usage: just_limiter_host ABS_PLUGIN ABS_OUTPUT --serial-native-qa|--offscreen");const bool native=std::string(argv[3])=="--serial-native-qa";check(native||std::string(argv[3])=="--offscreen","explicit QA mode required");
 std::filesystem::create_directories(argv[2]);[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];[NSApp finishLaunching];
 std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());auto infos=module->getFactory().classInfos();check(infos.size()==2,"processor/controller classes");
 HostApplication host;auto component=module->getFactory().createInstance<IComponent>(infos[0].ID());auto controller=module->getFactory().createInstance<IEditController>(infos[1].ID());check(component&&controller,"factory");
 check(component->initialize(&host)==kResultOk&&controller->initialize(&host)==kResultOk,"initialize");FUnknownPtr<IConnectionPoint> cp(component),cc(controller);check(cp&&cc&&cp->connect(cc)==kResultOk&&cc->connect(cp)==kResultOk,"actual analysis connection");
 auto handler=owned(new Handler(argv[2]));controller->setComponentHandler(handler);FUnknownPtr<IAudioProcessor> processor(component);ProcessSetup setup{kRealtime,kSample64,960,48000};check(processor&&processor->setupProcessing(setup)==kResultOk,"prepare");check(processor->getLatencySamples()==288,"host PDC 288");component->setActive(true);processor->setProcessing(true);
 auto view=owned(controller->createView(ViewType::kEditor));check(bool(view),"editor");ViewRect size{0,0,1120,620};view->checkSizeConstraint(&size);
 auto* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(80,80,size.getWidth(),size.getHeight()+44) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.contentMinSize=NSMakeSize(880,644);window.title=@"JUST Limiter · isolated native QA (no audio device)";
 auto* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,size.getWidth(),size.getHeight())];[window.contentView addSubview:parent];auto frame=owned(new Frame);frame->window=window;frame->parent=parent;view->setFrame(frame);
 check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk&&view->onSize(&size)==kResultOk,"attach real plugin");
 Session session;session.processor=processor;session.controller=controller;session.view=view;session.handler=handler;session.directory=argv[2];session.parent=parent;
 auto* delegate=[LimiterQaDelegate new];delegate->session=&session;window.delegate=delegate;
 auto* picker=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(12,size.getHeight()+8,240,28) pullsDown:NO];[picker addItemsWithTitles:@[@"Low level −12 dB",@"Overload +12 dB",@"4x impulse",@"Silence"]];picker.autoresizingMask=NSViewMinYMargin;picker.target=delegate;picker.action=@selector(probe:);[window.contentView addSubview:picker];
 auto* capture=[NSButton buttonWithTitle:@"Save pixels + state" target:delegate action:@selector(capture:)];capture.autoresizingMask=NSViewMinYMargin;capture.frame=NSMakeRect(270,size.getHeight()+8,180,28);[window.contentView addSubview:capture];
 if(native){[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];auto timer=[NSTimer scheduledTimerWithTimeInterval:.02 target:delegate selector:@selector(tick:) userInfo:nil repeats:YES];std::cout<<"Native QA ready: OS mouse/keyboard only; no synthetic events.\n";[NSApp run];[timer invalidate];session.capture();}
 else {for(unsigned n=0;n<100;++n){session.render();if(n%4==3)[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.005]];}[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];session.capture();std::cout<<"OFFSCREEN ONLY - no native interaction acceptance claimed\n";}
 view->removed();view->setFrame(nullptr);controller->setComponentHandler(nullptr);processor->setProcessing(false);component->setActive(false);cp->disconnect(cc);cc->disconnect(cp);controller->terminate();component->terminate();window.delegate=nil;[window orderOut:nil];
}}
