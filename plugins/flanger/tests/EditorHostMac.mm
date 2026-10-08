#import <Cocoa/Cocoa.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "../Parameters.hpp"
#include "common/ui/ObjCNames.hpp"
#include <iostream>
#include <chrono>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
void check(bool v,const char* label){if(!v){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
 unsigned starts=0,writes=0,ends=0;Steinberg::Vst::ParamID last=0;
 tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id)override{++starts;last=id;return kResultOk;}
 tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,ParamValue v)override{check(id==last && std::isfinite(v),"gesture target");++writes;return kResultOk;}
 tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id)override{check(id==last,"gesture end");++ends;return kResultOk;}
 tresult PLUGIN_API restartComponent(int32)override{return kResultOk;}
 OBJ_METHODS(Handler,FObject)
 DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};
NSView* find(NSView* parent,Class kind,NSInteger tag) {for(NSView* v in parent.subviews){if([v isKindOfClass:kind] && (tag<0 || v.tag==tag))return v;if(NSView* found=find(v,kind,tag))return found;}return nil;}
NSView* rotary(NSView* parent,just::ParamID id) {for(NSView* v in parent.subviews){if([v.accessibilityIdentifier isEqualToString:[NSString stringWithUTF8String:just::flanger::parameters[just::flanger::registry.index(id)].stableKey]] && [v.identifier isEqualToString:[NSString stringWithUTF8String:just::rotaryViewIdentifier]])return v;if(auto* r=rotary(v,id))return r;}return nil;}
NSView* identified(NSView* parent,NSString* id){for(NSView* v in parent.subviews){if([v.identifier isEqualToString:id])return v;if(auto* r=identified(v,id))return r;}return nil;}
unsigned rotaryCount(NSView* parent){unsigned n=0;for(NSView* v in parent.subviews){if([v.identifier isEqualToString:[NSString stringWithUTF8String:just::rotaryViewIdentifier]])++n;n+=rotaryCount(v);}return n;}
NSButton* button(NSView* parent,NSString* label){for(NSView* v in parent.subviews){if([v isKindOfClass:NSButton.class] && [[(NSButton*)v title]isEqualToString:label])return (NSButton*)v;if(auto b=button(v,label))return b;}return nil;}
void screenshot(NSView* view,const std::string& path){[view layoutSubtreeIfNeeded];auto image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];auto bytes=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([bytes writeToFile:[NSString stringWithUTF8String:path.c_str()]atomically:YES],"UI screenshot");}
int main(int argc,char** argv) {
 @autoreleasepool {
  check(argc>=2,"bundle argument");[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
  std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());auto classes=module->getFactory().classInfos();HostApplication host;
  auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());auto reference=module->getFactory().createInstance<IComponent>(classes[0].ID());auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
  check(component && reference && controller,"factory");check(component->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");
  auto handler=owned(new Handler);controller->setComponentHandler(handler.get());FUnknownPtr<IAudioProcessor> processor(component),referenceProcessor(reference);
  ProcessSetup setup{kRealtime,kSample64,256,48000};check(processor->setupProcessing(setup)==kResultOk && referenceProcessor->setupProcessing(setup)==kResultOk,"prepare");component->setActive(true);reference->setActive(true);processor->setProcessing(true);referenceProcessor->setProcessing(true);
  MemoryStream saved;check(component->getState(&saved)==kResultOk,"save");saved.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&saved)==kResultOk,"controller load");
  std::array<double,256> left{},right{},ol{},orr{},refL{},refR{};double* input[]={left.data(),right.data()};double* output[]={ol.data(),orr.data()};double* refOutput[]={refL.data(),refR.data()};AudioBusBuffers in{},out{},refOut{};in.numChannels=out.numChannels=refOut.numChannels=2;in.channelBuffers64=input;out.channelBuffers64=output;refOut.channelBuffers64=refOutput;
  ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;auto referenceData=data;referenceData.outputs=&refOut;
  NSView* parent=[[NSView alloc]initWithFrame:NSMakeRect(0,0,760,460)];auto view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native attach");ViewRect size;view->getSize(&size);parent.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());
  auto advanced=button(parent,@"Advanced");check(advanced && button(parent,@"Bypass") && button(parent,@"Preset"),"shared shell");
  check(rotary(parent,just::flanger::rateHzID) && rotary(parent,just::flanger::mixID),"four shared continuous controls attached");
  if(argc>2)screenshot(parent,std::string(argv[2])+"/simple.png");
  for(int block=0;block<200;++block) {
   for(unsigned i=0;i<256;++i){left[i]=std::sin((block*256+i)*.027);right[i]=std::sin((block*256+i)*.061);}
   if(block%4==0)[advanced performClick:nil];
   if(block%25==0){view->removed();view.reset();view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"reopen");advanced=button(parent,@"Advanced");}
   check(processor->process(data)==kResultOk && referenceProcessor->process(referenceData)==kResultOk,"effect process");check(ol==refL && orr==refR,"view changes/reopen preserve continuous effect samples");check(handler->writes==0 && handler->starts==0 && handler->ends==0,"no view sound gestures");
  }
  if(advanced.state!=NSControlStateValueOn)[advanced performClick:nil];
  check(rotaryCount(parent)==10,"all ten continuous effect controls use the common rotary; enums remain menus");
  auto sc=(NSScrollView*)find(parent,NSScrollView.class,-1);check(sc && sc.documentView,"Advanced scroll container");
  if(argc>2){screenshot(parent,std::string(argv[2])+"/advanced.png");[sc.documentView scrollPoint:NSMakePoint(0,sc.documentView.bounds.size.height-sc.contentView.bounds.size.height)];screenshot(parent,std::string(argv[2])+"/advanced-more.png");[sc.documentView scrollPoint:NSZeroPoint];}
  auto rate=rotary(parent,just::flanger::rateHzID);check([(NSNumber*)[rate valueForKey:@"enabled"] boolValue],"free rate editable");
  auto field=(NSTextField*)[rate valueForKey:@"value"];check(field && field.delegate,"shared Rate numeric field");
  const auto exactRate=just::flanger::parameters[just::flanger::registry.index(just::flanger::rateHzID)].toNormalized(.243789123);
  controller->setParamNormalized(just::flanger::rateHzID,exactRate);
  [rate performSelector:@selector(refreshValue)];
  [field.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:field]];
  check(std::abs(field.doubleValue-.243789123)<1e-14,"programmatic field session retains full physical precision");
  [field.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:field]];
  check(handler->writes==0 && controller->getParamNormalized(just::flanger::rateHzID)==exactRate,"unchanged numeric session performs no sound write");
  [field.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:field]];
  field.stringValue=@"0.5 Hz";
  [field.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:field]];
  check(handler->starts==1 && handler->writes==1 && handler->ends==1 && handler->last==just::flanger::rateHzID,"programmatic text session uses one complete gesture and fixed ID");
  check(controller->getParamNormalized(just::flanger::rateHzID)==just::flanger::parameters[just::flanger::registry.index(just::flanger::rateHzID)].toNormalized(.5),"shared numeric physical mapping");
  controller->setParamNormalized(just::flanger::modeID,1);[advanced performClick:nil];
  check(![(NSNumber*)[rate valueForKey:@"enabled"] boolValue] && ![(NSNumber*)[rotary(parent,just::flanger::depthID) valueForKey:@"enabled"] boolValue],"Manual disables Rate/Depth");
  controller->setParamNormalized(just::flanger::modeID,0);controller->setParamNormalized(just::flanger::syncID,1);[advanced performClick:nil];
  check(![(NSNumber*)[rate valueForKey:@"enabled"] boolValue] && [(NSNumber*)[rotary(parent,just::flanger::depthID) valueForKey:@"enabled"] boolValue],"Sync disables free Rate only");
  [advanced performClick:nil];
  check(rate.hidden && !((NSPopUpButton*)find(parent,NSPopUpButton.class,just::flanger::divisionID)).isHiddenOrHasHiddenAncestor,"Simple Sync replaces free Rate with the original Division menu");
  auto clock=(NSSegmentedControl*)identified(parent,@"flanger.clock-mode");check(clock && clock.selectedSegment==1,"Free/Sync reflects the original sound parameter");
  clock.selectedSegment=0;[clock sendAction:clock.action to:clock.target];
  check(handler->starts==2 && handler->writes==2 && handler->ends==2 && handler->last==just::flanger::syncID && !rate.hidden,"Free/Sync uses one balanced gesture and retains free Rate target");
  check(controller->getParamNormalized(just::flanger::rateHzID)==just::flanger::parameters[just::flanger::registry.index(just::flanger::rateHzID)].toNormalized(.5),"Sync toggles preserve the saved free Rate");
  data.numSamples=64;std::vector<double> times;times.reserve(4000);
  for(int block=0;block<4400;++block){
   if(block%256==0)[[NSRunLoop currentRunLoop]runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.001]];
   auto begin=std::chrono::steady_clock::now();check(processor->process(data)==kResultOk,"benchmark process");auto end=std::chrono::steady_clock::now();
   if(block>=400)times.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
  }
  std::sort(times.begin(),times.end());double p99=times[3960];
  std::cout<<"synthetic host default settings / NSView attached / 48k stereo float64 / 64 samples: median_us="<<times[2000]<<" p99_us="<<p99<<" block_percent="<<p99/(64./48000.*1e6)*100<<'\n';
  view->removed();view.reset();processor->setProcessing(false);referenceProcessor->setProcessing(false);component->setActive(false);reference->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();reference->terminate();
  std::cout<<"PASS loaded VST3 native editor: 50 toggles / 8 reopens, exact continuous effect audio, no implicit sound writes, shared rotary composition, programmatic numeric sessions and Free/Sync; OS pointer/keyboard input requires separate serial QA\n";
 }
}
