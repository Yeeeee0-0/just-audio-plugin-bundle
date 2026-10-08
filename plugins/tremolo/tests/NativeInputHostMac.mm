#import <Cocoa/Cocoa.h>
#include "../Parameters.hpp"
#include "common/runtime/Analysis.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace Steinberg;using namespace Steinberg::Vst;
namespace t=just::tremolo;
static void require(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
static std::vector<std::uint8_t> soundBytes(IComponent* component){MemoryStream stream;require(component->getState(&stream)==kResultOk,"complete processor state");auto* data=reinterpret_cast<const std::uint8_t*>(stream.getData());return {data,data+stream.getSize()};}

// This host only records events delivered to its own window. It never generates
// NSEvent/CGEvent input or invokes a control's mouse/key/delegate methods.
class EditLog final:public FObject,public IComponentHandler {
public:
    unsigned begins=0,writes=0,ends=0;NSInteger lastNativeEvent=0;
    std::map<ParamID,double> pending;std::ofstream events;
    explicit EditLog(const std::string& path):events(path){events<<std::setprecision(17);}
    void record(const char* kind,ParamID id,double value=0){events<<"{\"kind\":\""<<kind<<"\",\"id\":"<<id<<",\"value\":"<<value<<",\"native_event\":"<<lastNativeEvent<<"}\n";events.flush();}
    tresult PLUGIN_API beginEdit(ParamID id)override{++begins;record("begin",id);return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value)override{if(!std::isfinite(value) || value<0 || value>1)return kInvalidArgument;++writes;pending[id]=value;record("write",id,value);return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id)override{++ends;record("end",id);return kResultOk;}
    tresult PLUGIN_API restartComponent(int32)override{return kResultOk;}
    OBJ_METHODS(EditLog,FObject) DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
class AnalysisLog final:public FObject,public IConnectionPoint {
public:
    IPtr<IConnectionPoint> destination;just::AnalysisWindow latest{};unsigned windows=0;
    explicit AnalysisLog(IConnectionPoint* controller):destination(controller){}
    tresult PLUGIN_API connect(IConnectionPoint*)override{return kResultOk;}
    tresult PLUGIN_API disconnect(IConnectionPoint*)override{return kResultOk;}
    tresult PLUGIN_API notify(IMessage* message)override{
        if(message && message->getMessageID() && !std::strcmp(message->getMessageID(),"JUST.Analysis.v2")){
            const void* bytes=nullptr;uint32 length=0;auto* attributes=message->getAttributes();
            require(attributes && attributes->getBinary("Data",bytes,length)==kResultOk && length==sizeof(just::AnalysisMessage),"real analysis transport");
            just::AnalysisMessage data;std::memcpy(&data,bytes,sizeof(data));require(data.valid(),"valid real analysis message");
            if(data.kind==just::AnalysisKind::envelope){latest=data.window;++windows;}
        }
        return destination->notify(message);
    }
    OBJ_METHODS(AnalysisLog,FObject) DEFINE_INTERFACES DEF_INTERFACE(IConnectionPoint) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
class InputFrame final:public FObject,public IPlugFrame {
public:
    NSWindow* window=nil;NSView* root=nil;
    tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* rect)override{
        if(!view || !rect || rect->getWidth()<1 || rect->getHeight()<1)return kInvalidArgument;
        [window setContentSize:NSMakeSize(rect->getWidth(),rect->getHeight())];root.frame=NSMakeRect(0,0,rect->getWidth(),rect->getHeight());return view->onSize(rect);
    }
    OBJ_METHODS(InputFrame,FObject) DEFINE_INTERFACES DEF_INTERFACE(IPlugFrame) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
@interface TremoloInputWindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic,assign) IPlugView* pluginView;
@end
@implementation TremoloInputWindowDelegate
- (NSSize)windowWillResize:(NSWindow*)window toSize:(NSSize)size {
    const auto content=[window contentRectForFrameRect:NSMakeRect(0,0,size.width,size.height)].size;
    ViewRect rect{0,0,int(content.width),int(content.height)};self.pluginView->checkSizeConstraint(&rect);
    return [window frameRectForContentRect:NSMakeRect(0,0,rect.getWidth(),rect.getHeight())].size;
}
- (void)windowDidResize:(NSNotification*)notification {
    NSWindow* window=notification.object;const auto size=window.contentView.bounds.size;
    ViewRect rect{0,0,int(size.width),int(size.height)};self.pluginView->onSize(&rect);
}
@end
int main(int argc,char** argv){
    // No accidental window in CTest or a normal command invocation.
    if(argc!=4 || std::strcmp(argv[3],"--interactive")){std::cerr<<"usage: <VST3> <evidence-dir> --interactive (serial QA only)\n";return 2;}
    @autoreleasepool {
        const std::string path=argv[2];std::filesystem::create_directories(path);
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);require(bool(module),error.c_str());
        HostApplication host;auto classes=module->getFactory().classInfos();require(classes.size()==2,"processor/controller pair");
        auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        require(component && controller && component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"actual VST3 initialization");
        auto handler=owned(new EditLog(path+"/host-edits.jsonl"));controller->setComponentHandler(handler.get());
        auto initial=soundBytes(component);MemoryStream initialStream(initial.data(),initial.size());require(controller->setComponentState(&initialStream)==kResultOk,"initial complete sound targets");
        {std::ofstream file(path+"/initial-sound-state.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(initial.data()),initial.size());}
        FUnknownPtr<IAudioProcessor> processor(component);FUnknownPtr<IConnectionPoint> pConnection(component),cConnection(controller);
        auto analysis=owned(new AnalysisLog(cConnection));require(pConnection && cConnection && pConnection->connect(analysis)==kResultOk && cConnection->connect(pConnection)==kResultOk,"connected real analysis bridge");
        constexpr int samples=1536;ProcessSetup setup{kRealtime,kSample64,samples,48000};require(processor->setupProcessing(setup)==kResultOk && component->setActive(true)==kResultOk,"prepare real stereo processor");processor->setProcessing(true);
        std::array<double,samples> left{},right{},outLeft{},outRight{};double* inputs[]={left.data(),right.data()};double* outputs[]={outLeft.data(),outRight.data()};
        AudioBusBuffers input{},output{};input.numChannels=output.numChannels=2;input.channelBuffers64=inputs;output.channelBuffers64=outputs;
        ProcessContext context{};context.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;context.tempo=137;context.sampleRate=48000;
        ProcessData data{};data.numSamples=samples;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;data.processContext=&context;
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,900,550) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.title=@"JUST Tremolo · isolated native input QA";
        auto frame=owned(new InputFrame);frame->window=window;frame->root=window.contentView;
        auto view=owned(controller->createView(ViewType::kEditor));require(view && view->setFrame(frame)==kResultOk && view->attached((__bridge void*)frame->root,kPlatformTypeNSView)==kResultOk,"actual editor attached");
        auto* delegate=[[TremoloInputWindowDelegate alloc] init];delegate.pluginView=view;window.delegate=delegate;
        ViewRect size;require(view->getSize(&size)==kResultOk,"editor size");size.right=size.left+900;size.bottom=size.top+550;frame->resizeView(view,&size);
        std::ofstream nativeEvents(path+"/native-events.jsonl");unsigned nativeCount=0;
        auto recordNative=[&](NSEvent* event){
            if(event.window==window){handler->lastNativeEvent=event.eventNumber;++nativeCount;nativeEvents<<"{\"type\":"<<unsigned(event.type)<<",\"number\":"<<event.eventNumber<<",\"flags\":"<<event.modifierFlags<<",\"key_window\":"<<bool(window.isKeyWindow)<<"}\n";nativeEvents.flush();}
            return event;
        };
        id monitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskAny handler:^NSEvent*(NSEvent* event){return recordNative(event);}];
        std::uint64_t position=0;unsigned blocks=0;
        auto render=[&](){
            for(unsigned i=0;i<samples;++i){left[i]=.4*(.85+.15*std::sin((position+i)*.071));right[i]=-.6*left[i];}
            ParameterChanges events;for(const auto& [id,value]:handler->pending){int32 queue=0,index=0;events.addParameterData(id,queue)->addPoint(0,value,index);}handler->pending.clear();data.inputParameterChanges=&events;
            require(processor->process(data)==kResultOk,"continuous real process");data.inputParameterChanges=nullptr;
            position+=samples;context.projectTimeSamples=position;context.projectTimeMusic=position*137./(60*48000);++blocks;
            std::ofstream snapshot(path+"/native-state.json");snapshot<<std::setprecision(17)<<"{\"key\":"<<bool(window.isKeyWindow)<<",\"active\":"<<bool(NSApp.isActive)<<",\"native_events\":"<<nativeCount<<",\"blocks\":"<<blocks<<",\"begins\":"<<handler->begins<<",\"writes\":"<<handler->writes<<",\"ends\":"<<handler->ends<<",\"analysis_windows\":"<<analysis->windows<<",\"analysis_source_ns\":"<<analysis->latest.header.sourceNanoseconds<<",\"analysis_flags\":"<<analysis->latest.header.flags<<",\"effect_flags\":"<<analysis->latest.effectFlags<<",\"effective_hz\":"<<analysis->latest.effectiveHz<<",\"gain_min\":["<<analysis->latest.modulationMin[0]<<','<<analysis->latest.modulationMin[1]<<"],\"gain_max\":["<<analysis->latest.modulationMax[0]<<','<<analysis->latest.modulationMax[1]<<"],\"parameters\":[";
            for(unsigned i=0;i<t::registry.count;++i){const auto& parameter=t::parameters[i];if(i)snapshot<<',';const double n=controller->getParamNormalized(parameter.id);snapshot<<"{\"id\":"<<parameter.id<<",\"normalized\":"<<n<<",\"physical\":"<<t::physicalValue(parameter,n)<<'}';}snapshot<<"]}\n";
            auto state=soundBytes(component);std::ofstream file(path+"/current-sound-state.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(state.data()),state.size());
        };
        NSTimer* timer=[NSTimer timerWithTimeInterval:.032 repeats:YES block:^(NSTimer*){render();}];
        [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        while(window.isVisible){
            NSEvent* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:.05] inMode:NSDefaultRunLoopMode dequeue:YES];
            if(event)[NSApp sendEvent:event]; // forward the real OS queue, never manufacture input
            [NSApp updateWindows];
        }
        [timer invalidate];[NSEvent removeMonitor:monitor];window.delegate=nil;delegate.pluginView=nullptr;require(view->removed()==kResultOk,"editor removed");view.reset();
        {std::ofstream final(path+"/closed-gestures.json");final<<"{\"begins\":"<<handler->begins<<",\"writes\":"<<handler->writes<<",\"ends\":"<<handler->ends<<"}\n";}
        processor->setProcessing(false);component->setActive(false);pConnection->disconnect(analysis);cConnection->disconnect(pConnection);
        controller->setComponentHandler(nullptr);controller->terminate();component->terminate();
        std::cout<<"STOP native input fixture; inspect actual event and edit logs, no automatic acceptance claimed\n";
    }
}
