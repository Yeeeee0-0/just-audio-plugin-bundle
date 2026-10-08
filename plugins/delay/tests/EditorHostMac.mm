#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../Parameters.hpp"
#include "../DelayEngine.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <iostream>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool condition,const char* label){if(!condition){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;
    std::ofstream events;std::vector<std::pair<ParamID,ParamValue>> pending;
    void record(const char* kind,ParamID id,double value){if(events.is_open()){events<<std::setprecision(17)<<"{\"event\":\""<<kind<<"\",\"id\":"<<id<<",\"normalized\":"<<value<<",\"begins\":"<<starts<<",\"writes\":"<<writes<<",\"ends\":"<<ends<<"}\n";events.flush();}}
    tresult PLUGIN_API beginEdit(ParamID id) override{check(just::delay::registry.index(id)<just::delay::registry.count,"known gesture ID");++starts;record("begin",id,0);return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override{check(just::delay::registry.index(id)<just::delay::registry.count && std::isfinite(value),"gesture value");++writes;if(events.is_open())pending.emplace_back(id,value);record("write",id,value);return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{check(just::delay::registry.index(id)<just::delay::registry.count,"known end ID");++ends;record("end",id,0);return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static NSButton* button(NSView* parent,NSString* name) {
    for(NSView* view in parent.subviews) {
        if([view isKindOfClass:NSButton.class] && ([[view identifier] isEqualToString:@"just.delay.ping-pong"] && [name isEqualToString:@"Ping Pong"] || [[(NSButton*)view title] isEqualToString:name]))return (NSButton*)view;
        if(auto* nested=button(view,name))return nested;
    }
    return nil;
}
static NSButton* actionButton(NSView* parent,SEL action) {
    for(NSView* child in parent.subviews) {
        if([child isKindOfClass:NSButton.class] && ((NSButton*)child).action==action)return (NSButton*)child;
        if(auto* nested=actionButton(child,action))return nested;
    }
    return nil;
}
static NSView* exactClassView(NSView* root,Class expected) {
    if(object_getClass(root)==expected)return root;
    for(NSView* child in root.subviews)if(auto* found=exactClassView(child,expected))return found;
    return nil;
}
static void checkDelayRuntimeClasses(NSView* root,const char* bundle) {
    const auto binary=std::filesystem::path(bundle)/"Contents/MacOS/Just_delay";
    for(NSString* name in @[@"JustDelayContentView",@"JustDelayPingButton",@"JustDelayDocumentView"]) {
        Class cls=NSClassFromString(name);check(cls && cls==objc_getClass(name.UTF8String),"Delay exact runtime class lookup");
        const char* image=class_getImageName(cls);check(image && std::filesystem::equivalent(image,binary),"Delay runtime class belongs to its loaded VST3 binary");
        check(exactClassView(root,cls),"Delay attached subtree contains the exact module runtime class");
    }
    for(const char* oldName:{"JDView","JDSlider","JDDocument"})check(objc_getClass(oldName)==Nil,"Delay does not register legacy ambiguous runtime classes");
}
static void saveImage(NSView* view,const char* path) {
    [view layoutSubtreeIfNeeded];NSBitmapImageRep* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    NSData* png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    check([png writeToFile:[NSString stringWithUTF8String:path] atomically:YES],"UI screenshot written");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc>=2,"usage: just_editor_host <absolute-vst3-path> [screenshot-dir]");
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);
        check(bool(module),error.c_str());HostApplication host;
        auto classes=module->getFactory().classInfos();
        check(classes.size()==2,"exact processor/controller factory pair");
        auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());
        auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        check(component && controller,"factory creates both components");
        check(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"host initializes both");
        auto handler=owned(new Handler);controller->setComponentHandler(handler.get());
        FUnknownPtr<IAudioProcessor> processor(component);check(bool(processor),"audio interface");
        ProcessSetup setup{kRealtime,kSample64,64,48000};
        check(processor->setupProcessing(setup)==kResultOk,"audio setup");component->setActive(true);processor->setProcessing(true);
        MemoryStream sound;check(component->getState(&sound)==kResultOk,"sound state before editor");
        sound.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&sound)==kResultOk,"controller restores sound state");
        MemoryStream oldUI;IBStreamer oldWriter(&oldUI,kLittleEndian);oldWriter.writeInt32(1);oldWriter.writeInt32(480);oldWriter.writeInt32(260);oldWriter.writeDouble(1);oldWriter.writeBool(false);oldUI.seek(0,IBStream::kIBSeekSet,nullptr);
        check(controller->setState(&oldUI)==kResultOk,"legacy schema-1 editor size restores");
        std::array<double,64> left{},right{},outLeft{},outRight{};
        for(unsigned i=0;i<64;++i){left[i]=double(i)/19-1;right[i]=-left[i];}
        double* inputs[]={left.data(),right.data()};double* outputs[]={outLeft.data(),outRight.data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers64=inputs;out.channelBuffers64=outputs;
        ProcessData data{};data.numSamples=64;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
        just::delay::DelayEngine reference;check(reference.prepare({48000,64,2,2,0,just::SampleFormat::float64,false}),"reference setup");
        auto referenceState=just::initialState({},just::delay::registry);reference.applyTargets(referenceState,0);
        std::array<double,64> referenceL{},referenceR{};
        auto advanceReference=[&](){reference.process(just::AudioBlock<double>{{left.data(),right.data()},{referenceL.data(),referenceR.data()},2,2,64,in.silenceFlags},{});reference.endBlock();};
        check(processor->process(data)==kResultOk,"baseline Delay audio");advanceReference();check(outLeft==referenceL && outRight==referenceR,"initial audio bit-identical to reference");in.silenceFlags=3;
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,760,460)];
        auto view=owned(controller->createView(ViewType::kEditor));check(bool(view),"editor create");
        ViewRect initial;check(view->getSize(&initial)==kResultOk && initial.getWidth()==640 && initial.getHeight()==480,"legacy Delay editor opens at approved layout minimum");
        ViewRect tooSmall(0,0,480,260);check(view->checkSizeConstraint(&tooSmall)==kResultTrue && tooSmall.getWidth()==640 && tooSmall.getHeight()==480,"host resize clamps to Delay minimum");
        check(view->onSize(&tooSmall)==kResultOk && tooSmall.getWidth()==640 && tooSmall.getHeight()==480,"onSize applies compact Delay minimum");
        ViewRect full(0,0,1120,640);check(view->onSize(&full)==kResultOk,"Delay editor can grow after compact restore");
        check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native attach");
        checkDelayRuntimeClasses(parent,argv[1]);
        ViewRect size;view->getSize(&size);parent.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());
        if(argc>=4 && std::strcmp(argv[2],"--interactive")==0){
            // This mode is launched only by the parent's serial QA. It emits
            // no synthetic pointer/key events or control actions.
            const std::filesystem::path evidence=argv[3];std::filesystem::create_directories(evidence);
            handler->events.open(evidence/"native-events.jsonl");
            FUnknownPtr<IConnectionPoint> pConnection(component),cConnection(controller);
            check(pConnection && cConnection && pConnection->connect(cConnection)==kResultOk && cConnection->connect(pConnection)==kResultOk,"real analysis connection");
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
            NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(120,120,size.getWidth(),size.getHeight()+64) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
            window.releasedWhenClosed=NO;window.title=@"JUST Delay 0.3 — serial native QA";
            parent.frame=NSMakeRect(0,64,size.getWidth(),size.getHeight());[window.contentView addSubview:parent];
            auto* report=[NSTextField wrappingLabelWithString:@"OS inputs only · Generated impulse audio through real VST3 · 45 targets logged"];
            report.frame=NSMakeRect(16,8,size.getWidth()-32,48);report.font=[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];[window.contentView addSubview:report];
            auto* liveLeft=&left;auto* liveRight=&right;auto* liveInput=&in;auto* liveData=&data;
            __block std::uint64_t sampleClock=0;__block unsigned ticks=0;
            NSTimer* timer=[NSTimer scheduledTimerWithTimeInterval:1./30 repeats:YES block:^(NSTimer*){
                auto& left=*liveLeft;auto& right=*liveRight;auto& in=*liveInput;auto& data=*liveData;
                ParameterChanges changes;int32 queueIndex=0,pointIndex=0;
                for(const auto& edit:handler->pending)changes.addParameterData(edit.first,queueIndex)->addPoint(0,edit.second,pointIndex);
                handler->pending.clear();
                Steinberg::Vst::ProcessContext context{};context.state=Steinberg::Vst::ProcessContext::kTempoValid|Steinberg::Vst::ProcessContext::kPlaying|Steinberg::Vst::ProcessContext::kContTimeValid;context.sampleRate=48000;context.tempo=120;
                for(unsigned block=0;block<25;++block){
                    bool impulse=false;for(unsigned i=0;i<64;++i){const bool hit=(sampleClock+i)%48000==0;left[i]=hit?1:0;right[i]=hit?.6:0;impulse=impulse || hit;}
                    in.silenceFlags=impulse?0:3;context.projectTimeSamples=context.continousTimeSamples=sampleClock;
                    data.processContext=&context;data.inputParameterChanges=block?nullptr:&changes;
                    check(processor->process(data)==kResultOk,"native QA real audio processing");sampleClock+=64;
                }
                data.inputParameterChanges=nullptr;data.processContext=nullptr;++ticks;
                std::ofstream snapshot(evidence/"native-state.json");snapshot<<std::setprecision(17)<<"{\"begins\":"<<handler->starts<<",\"writes\":"<<handler->writes<<",\"ends\":"<<handler->ends<<",\"ticks\":"<<ticks<<",\"latencySamples\":"<<processor->getLatencySamples()<<",\"targets\":{";
                for(unsigned i=0;i<45;++i){if(i)snapshot<<',';snapshot<<'\"'<<just::delay::parameters[i].id<<"\":"<<controller->getParamNormalized(just::delay::parameters[i].id);}snapshot<<"}}\n";
                MemoryStream current;check(component->getState(&current)==kResultOk,"native QA complete component state");std::ofstream bytes(evidence/"native-component.state",std::ios::binary);bytes.write(static_cast<const char*>(current.getData()),current.getSize());
                report.stringValue=[NSString stringWithFormat:@"begin=%u write=%u end=%u · Time L %.8g ms · R %.8g ms · route %.1f · PDC %u",handler->starts,handler->writes,handler->ends,just::delay::spec(just::delay::timeL).toPhysical(controller->getParamNormalized(just::delay::timeL)),just::delay::spec(just::delay::timeR).toPhysical(controller->getParamNormalized(just::delay::timeR)),controller->getParamNormalized(just::delay::route),processor->getLatencySamples()];
                if(!window.visible)[NSApp stop:nil];
            }];
            [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];[timer invalidate];
            pConnection->disconnect(cConnection);cConnection->disconnect(pConnection);view->removed();view.reset();processor->setProcessing(false);component->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();
            std::cout<<"Native QA fixture closed; inspect OS interaction evidence, no automatic PASS claimed\n";return 0;
        }

        auto* advanced=actionButton(parent,@selector(changeView:));auto* bypass=actionButton(parent,@selector(changeBypass:));
        check(advanced && bypass && actionButton(parent,@selector(showPresets:)),"JUST title bar tools independent of locale and view title");
        if(argc>=3){std::string filename=std::string(argv[2])+"/simple.png";saveImage(parent,filename.c_str());}
        auto* ping=button(parent,@"Ping Pong");check(ping && !ping.hidden,"Ping Pong directly available beside Time");
        [ping performClick:nil];check(controller->getParamNormalized(just::delay::route)==1,"Ping Pong writes existing Route parameter");
        [ping performClick:nil];check(controller->getParamNormalized(just::delay::route)==0,"Ping Pong returns to Stereo");
        controller->setParamNormalized(just::delay::route,.5);
        [ping performClick:nil];[ping performClick:nil];
        check(controller->getParamNormalized(just::delay::route)==.5,"Ping Pong returns to prior Dual route");
        controller->setParamNormalized(just::delay::route,0);
        check(handler->starts==4 && handler->writes==4 && handler->ends==4,"Ping Pong uses balanced Route gestures");
        std::array<double,45> targetsBefore{};for(unsigned p=0;p<45;++p)targetsBefore[p]=controller->getParamNormalized(just::delay::parameters[p].id);
        for(int i=0;i<12;++i) {
            [advanced performClick:nil];
            check(controller->getParamNormalized(0)==0 && handler->writes==4,"view toggle has no sound edit");for(unsigned p=0;p<45;++p)check(controller->getParamNormalized(just::delay::parameters[p].id)==targetsBefore[p],"all sound targets unchanged");
            for(int block=0;block<64;++block){check(processor->process(data)==kResultOk,"view toggles preserve processing");advanceReference();check(outLeft==referenceL && outRight==referenceR,"editor toggles preserve bit-identical live tail");}
        }
        [advanced performClick:nil];
        if(argc>=3){std::string filename=std::string(argv[2])+"/advanced.png";saveImage(parent,filename.c_str());}
        MemoryStream soundAfter;component->getState(&soundAfter);
        check(soundAfter.getSize()==sound.getSize() && std::memcmp(soundAfter.getData(),sound.getData(),std::size_t(sound.getSize()))==0,"complete sound state unchanged after live view toggles");
        MemoryStream ui;check(controller->getState(&ui)==kResultOk,"editor preferences saved separately");
        view->removed();view.reset();
        view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"editor reopen");
        checkDelayRuntimeClasses(parent,argv[1]);
        check(actionButton(parent,@selector(changeView:)).state==NSControlStateValueOn && controller->getParamNormalized(0)==0,"reopen retains view without sound edit");for(int block=0;block<64;++block){processor->process(data);advanceReference();check(outLeft==referenceL && outRight==referenceR,"editor reopen preserves live tail");}
        [actionButton(parent,@selector(changeBypass:)) performClick:nil];
        check(controller->getParamNormalized(0)==1 && handler->starts==5 && handler->writes==5 && handler->ends==5,"UI bypass sends one complete gesture");
        view->removed();view.reset();
        ui.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setState(&ui)==kResultOk && controller->getParamNormalized(0)==1,"UI state restore cannot overwrite sound");
        processor->setProcessing(false);component->setActive(false);
        controller->setComponentHandler(nullptr);controller->terminate();component->terminate();
        std::cout<<"PASS Delay loaded factory/native content attach, 13 view toggles with bit-identical live tails, reopen, isolated editor state, bypass gesture\n";
    }
}
