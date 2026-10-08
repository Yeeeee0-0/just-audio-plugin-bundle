#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include "common/ui/ObjCNames.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>
#include <array>
#include <cstring>
#include "plugins/distortion/Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool condition,const char* label){if(!condition){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;
    tresult PLUGIN_API beginEdit(ParamID id) override{check(id<5000,"stable gesture ID");++starts;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override{check(id<5000 && std::isfinite(value),"gesture value");++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{check(id<5000,"gesture end ID");++ends;return kResultOk;}
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
static NSView* privateView(NSView* root,NSString* name){for(NSView* v in root.subviews){if([NSStringFromClass(v.class) isEqualToString:name])return v;if(auto* found=privateView(v,name))return found;}return nil;}
static NSView* identifiedView(NSView* root,const char* id){for(NSView* v in root.subviews){if([v.identifier isEqualToString:[NSString stringWithUTF8String:id]])return v;if(auto* found=identifiedView(v,id))return found;}return nil;}
static void privateIdentity(NSView* root,const char* bundle){
    NSString* path=[[NSString stringWithUTF8String:bundle] stringByResolvingSymlinksInPath];
    const SEL selectors[]={@selector(layout),@selector(isFlipped)};unsigned i=0;
    for(NSString* name in @[@"JustDistortionView",@"JustDistortionDocument"]){
        NSView* v=privateView(root,name);check(v && v.class==NSClassFromString(name) && v.class==objc_getClass(name.UTF8String),"actual loaded private view has exact unique global runtime identity");
        const char* image=class_getImageName(v.class);check(image,"private class defining image exists");NSString* owner=[[NSString stringWithUTF8String:image] stringByResolvingSymlinksInPath];check([owner hasPrefix:[path stringByAppendingString:@"/"]],"private class originates in actual Distortion bundle");
        Method method=class_getInstanceMethod(v.class,selectors[i++]);Dl_info info{};check(method && dladdr(reinterpret_cast<const void*>(method_getImplementation(method)),&info)!=0 && info.dli_fname,"actual private method IMP image exists");check([[[NSString stringWithUTF8String:info.dli_fname] stringByResolvingSymlinksInPath] isEqualToString:owner],"private class and own method IMP originate in same Distortion image");
    }
    for(const char* name:{"JDView","JDSlider","JDDocument"})check(objc_getClass(name)==nullptr && NSClassFromString([NSString stringWithUTF8String:name])==Nil,"legacy module collision class is absent in actual loaded process");
    auto* shell=identifiedView(root,just::foundationViewIdentifier);auto* plot=identifiedView(root,just::analysisViewIdentifier);
    check(shell && shell.class==objc_getClass("Just_distortion_FoundationView") && plot && plot.class==objc_getClass("Just_distortion_AnalysisView"),"stable own-subtree locators resolve exact namespaced common views");
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
        const bool audioOnly=argc>=3 && std::string(argv[2])=="--audio-only";
        if(!audioOnly){[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];}
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
        auto restored=just::initialState(just::pluginIdentities[9].processor,just::distortion::registry);
        check(controller->getParamNormalized(just::distortion::model)==.2,"actual loaded new instance defaults to Soft");
        restored.targets[just::distortion::registry.index(just::distortion::model)]=0;
        restored.seed=73751;restored.targets[just::distortion::registry.index(just::distortion::fold_shape)]=.77;
        restored.targets[just::distortion::registry.index(just::distortion::crush_hold_hz)]=just::distortion::parameters[just::distortion::registry.index(just::distortion::crush_hold_hz)].toNormalized(12345);
        restored.targets[just::distortion::registry.index(just::distortion::quality)]=1;
        auto payload=just::encodeState(restored,just::distortion::registry);MemoryStream restore;
        restore.write(payload.data(),int32(payload.size()),nullptr);restore.seek(0,IBStream::kIBSeekSet,nullptr);
        check(component->setState(&restore)==kResultOk,"actual component hidden state restore");restore.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&restore)==kResultOk,"actual controller hidden state restore");
        auto readSound=[&](){MemoryStream stream;check(component->getState(&stream)==kResultOk,"actual component state save");just::SoundState state;check(just::decodeState(reinterpret_cast<const std::uint8_t*>(stream.getData()),stream.getSize(),restored.plugin,just::distortion::registry,state)==just::StateResult::ok,"decode saved component state");return state;};
        check(readSound().targets==restored.targets && readSound().seed==restored.seed,"complete pending restore visible before process");
        std::array<double,64> left{},right{},outLeft{},outRight{};
        for(unsigned i=0;i<64;++i){left[i]=double(i)/19-1;right[i]=-left[i];}
        double* inputs[]={left.data(),right.data()};double* outputs[]={outLeft.data(),outRight.data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers64=inputs;out.channelBuffers64=outputs;
        ProcessData data{};data.numSamples=64;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
        auto neutral=[&](bool first){
            check(processor->process(data)==kResultOk,"neutral process");
            for(unsigned i=0;i<64;++i){double l=i<32?(first?0:left[i+32]):left[i-32],r=i<32?(first?0:right[i+32]):right[i-32];check(outLeft[i]==l && outRight[i]==r,"Clean exact delayed audio");}
        };
        neutral(true);check(processor->getLatencySamples()==32,"host PDC32");
        check(readSound().targets==restored.targets,"all hidden targets survive process boundary");
        check(controller->getParameterCount()==27,"approved27 host parameters");
        for(unsigned n=0;n<just::distortion::registry.count;++n){ParameterInfo info;check(controller->getParameterInfo(n,info)==kResultOk,"get host ParameterInfo");const auto& p=just::distortion::parameters[n];check(info.id==p.id && info.stepCount==p.stepCount,"host IDs and enum ordinals");check(bool(info.flags&ParameterInfo::kCanAutomate)==p.automatable && info.defaultNormalizedValue==p.toNormalized(p.initial),"host automatable flags and Init");for(double normalized:{0.,.25,.5,.75,1.})check(std::abs(controller->normalizedParamToPlain(p.id,normalized)-p.toPhysical(normalized))<1e-8,"host normalized golden mapping");}
        // A real second loaded processor provides the ongoing nonlinear reference.
        restored.targets[just::distortion::registry.index(just::distortion::model)]=.4;
        restored.targets[just::distortion::registry.index(just::distortion::drive_db)]=1./3;
        payload=just::encodeState(restored,just::distortion::registry);
        auto reference=module->getFactory().createInstance<IComponent>(classes[0].ID());check(reference->initialize(&host)==kResultOk,"reference component initialize");
        FUnknownPtr<IAudioProcessor> referenceProcessor(reference);
        processor->setProcessing(false);component->setActive(false);check(processor->setupProcessing(setup)==kResultOk,"reset actual stream for paired reference");
        check(referenceProcessor->setupProcessing(setup)==kResultOk,"reference prepare");
        MemoryStream paired;paired.write(payload.data(),int32(payload.size()),nullptr);paired.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&paired)==kResultOk,"actual Hard complete state");
        paired.seek(0,IBStream::kIBSeekSet,nullptr);check(reference->setState(&paired)==kResultOk,"reference Hard complete state");paired.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&paired)==kResultOk,"controller Hard complete state");
        component->setActive(true);processor->setProcessing(true);reference->setActive(true);referenceProcessor->setProcessing(true);
        std::array<double,64> referenceLeft{},referenceRight{};double* referenceChannels[]={referenceLeft.data(),referenceRight.data()};
        AudioBusBuffers referenceOutput=out;referenceOutput.channelBuffers64=referenceChannels;ProcessData referenceData=data;referenceData.outputs=&referenceOutput;
        auto compareEffect=[&](){check(processor->process(data)==kResultOk && referenceProcessor->process(referenceData)==kResultOk,"paired loaded nonlinear processing");check(outLeft==referenceLeft && outRight==referenceRight,"loaded Hard reference streams identical");};
        compareEffect();
        if(!audioOnly){
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,720,420)];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:parent.frame styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.contentView=parent;
        auto view=owned(controller->createView(ViewType::kEditor));check(bool(view),"editor create");
        check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native attach");
        ViewRect size;view->getSize(&size);parent.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());privateIdentity(parent,argv[1]);
        auto* advanced=button(parent,@"Advanced");auto* bypass=button(parent,@"Bypass");
        check(advanced && bypass && button(parent,@"Preset"),"JUST title bar tools");
        if(argc>=3){std::string filename=std::string(argv[2])+"/simple.png";saveImage(parent,filename.c_str());}
        for(int i=0;i<12;++i) {
            [advanced performClick:nil];
            check(controller->getParamNormalized(0)==0 && handler->writes==0,"view toggle has no sound edit");
            compareEffect();
        }
        [advanced performClick:nil];
        if(argc>=3){std::string filename=std::string(argv[2])+"/advanced.png";saveImage(parent,filename.c_str());}
        MemoryStream ui;check(controller->getState(&ui)==kResultOk,"editor preferences saved separately");
        view->removed();view.reset();
        view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"editor reopen");privateIdentity(parent,argv[1]);
        check(button(parent,@"Advanced").state==NSControlStateValueOn && controller->getParamNormalized(0)==0,"reopen retains view without sound edit");
        [button(parent,@"Bypass") performClick:nil];
        check(controller->getParamNormalized(0)==1 && handler->starts==1 && handler->writes==1 && handler->ends==1,"UI bypass sends one complete gesture");
        view->removed();view.reset();
        ui.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setState(&ui)==kResultOk && controller->getParamNormalized(0)==1,"UI state restore cannot overwrite sound");
        check(readSound().targets==restored.targets,"loaded editor close/reopen leaves every sound target intact");
        [window orderOut:nil];
        }
        compareEffect();
        ParameterChanges changes;int32 queueIndex=0,pointIndex=0;
        auto* modeQueue=changes.addParameterData(just::distortion::model,queueIndex);modeQueue->addPoint(21,.4,pointIndex);
        auto* driveQueue=changes.addParameterData(just::distortion::drive_db,queueIndex);driveQueue->addPoint(63,.5,pointIndex);
        data.inputParameterChanges=&changes;check(processor->process(data)==kResultOk,"actual SDK simultaneous discrete/continuous automation");
        auto automated=readSound();check(automated.targets[just::distortion::registry.index(just::distortion::model)]==.4 && automated.targets[just::distortion::registry.index(just::distortion::drive_db)]==.5,"host automation final targets persisted");
        check(automated.targets[just::distortion::registry.index(just::distortion::fold_shape)]==.77 && automated.seed==restored.seed,"automation retains hidden model values/seed");
        for(auto y:outLeft)check(std::isfinite(y),"actual automated audio finite");
        changes.clearQueue();auto* mixQueue=changes.addParameterData(just::distortion::mix,queueIndex);mixQueue->addPoint(0,0,pointIndex);
        data.numSamples=0;check(processor->process(data)==kResultOk && processor->getLatencySamples()==32,"zero-sample automation does not change prepared PDC");
        check(readSound().targets[just::distortion::registry.index(just::distortion::mix)]==0,"zero-sample automation target saved");
        processor->setProcessing(false);component->setActive(false);
        referenceProcessor->setProcessing(false);reference->setActive(false);reference->terminate();
        controller->setComponentHandler(nullptr);controller->terminate();component->terminate();
        if(audioOnly)std::cout<<"PASS actual loaded VST3 audio-only: new Soft default, legacy Clean exact delayed audio,27host IDs/mappings, complete hidden state/seed, actual Hard twin audio, PDC32, pendingQuality retained, SDK automation and zero blocks. No NSApplication/NSWindow created.\n";
        else std::cout<<"PASS effect factory,27host IDs/mappings,complete hidden state/seed,PDC32,loaded native UI,13view toggles,Clean exact delayed audio,reopen,isolated editor state,bypass gesture,SDK automation and zero blocks\n";
    }
}
