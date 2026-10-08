// Load two test-only effect bundles into one process. No common native classes
// are linked into this host: provenance must point to each actual loaded bundle.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include "common/ui/ObjCNames.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
static NSView* find(NSView* parent,const char* identifier){
    for(NSView* v in parent.subviews){
        if([v.identifier isEqualToString:[NSString stringWithUTF8String:identifier]])return v;
        if(auto* nested=find(v,identifier))return nested;
    }return nil;
}
static NSButton* button(NSView* parent,NSString* title){
    for(NSView* v in parent.subviews){
        if([v isKindOfClass:NSButton.class] && [[(NSButton*)v title] isEqualToString:title])return (NSButton*)v;
        if(auto* nested=button(v,title))return nested;
    }return nil;
}
static std::vector<std::uint8_t> state(IComponent* c){
    MemoryStream s;check(c->getState(&s)==kResultOk,"complete component state");
    auto* bytes=reinterpret_cast<const std::uint8_t*>(s.getData());return {bytes,bytes+s.getSize()};
}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0,depth=0;
    std::vector<std::pair<ParamID,ParamValue>> events;
    tresult PLUGIN_API beginEdit(ParamID id) override{check(id==1 && depth==0,"isolated rotary gesture begins");++starts;++depth;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override{
        check(id==1 && depth==1 && std::isfinite(value),"rotary writes within one gesture");++writes;events.emplace_back(id,value);return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override{check(id==1 && depth==1,"rotary gesture balances");++ends;--depth;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
struct Instance {
    VST3::Hosting::Module::Ptr module;
    IPtr<IComponent> component,reference;
    IPtr<IEditController> controller;
    IPtr<Handler> handler;
    FUnknownPtr<IAudioProcessor> processor,twin;
    FUnknownPtr<IConnectionPoint> pConnection,cConnection;
    IPtr<IPlugView> editor;
    NSWindow* window=nil;
    std::string bundle,prefix;
    std::array<Class,3> classes{};
    std::array<float,64> left{},right{},outLeft{},outRight{},refLeft{},refRight{};
    unsigned blocks=0;
    bool changed=false,tail=false;
    Instance(const char* path,const char* name,HostApplication& host):bundle(path),prefix(name){
        std::string error;module=VST3::Hosting::Module::create(path,error);check(bool(module),error.c_str());
        auto infos=module->getFactory().classInfos();check(infos.size()==2,"loaded processor/controller pair");
        component=module->getFactory().createInstance<IComponent>(infos[0].ID());
        reference=module->getFactory().createInstance<IComponent>(infos[0].ID());
        controller=module->getFactory().createInstance<IEditController>(infos[1].ID());
        check(component && reference && controller,"loaded independent twins/controller");
        check(component->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize loaded effect twins");
        handler=owned(new Handler);controller->setComponentHandler(handler);
        processor=FUnknownPtr<IAudioProcessor>(component);twin=FUnknownPtr<IAudioProcessor>(reference);
        pConnection=FUnknownPtr<IConnectionPoint>(component);cConnection=FUnknownPtr<IConnectionPoint>(controller);
        check(processor && twin && pConnection && cConnection,"loaded processing and connection interfaces");
        check(pConnection->connect(cConnection)==kResultOk && cConnection->connect(pConnection)==kResultOk,"connect each processor to its own controller");
        MemoryStream initial;check(component->getState(&initial)==kResultOk,"initial complete state");
        initial.seek(0,IBStream::kIBSeekSet,nullptr);check(reference->setState(&initial)==kResultOk,"reference state/seed match");
        initial.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&initial)==kResultOk,"controller state match");
        ProcessSetup setup{kRealtime,kSample32,64,48000};
        check(processor->setupProcessing(setup)==kResultOk && twin->setupProcessing(setup)==kResultOk,"matching prepared processors");
        check(component->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"activate twins");
        // SDK AudioEffect's optional setProcessing hook returns kNotImplemented.
        // The actual process calls and reference comparisons below remain required.
        auto start=processor->setProcessing(true),refStart=twin->setProcessing(true);
        check((start==kResultOk || start==kNotImplemented) && (refStart==kResultOk || refStart==kNotImplemented),"start twins using supported or optional SDK hook");
        window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,760,460) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed=NO;attach();
    }
    void attach(){
        editor=owned(controller->createView(ViewType::kEditor));check(editor && editor->attached((__bridge void*)window.contentView,kPlatformTypeNSView)==kResultOk,"attach actual native editor");
        ViewRect r;check(editor->getSize(&r)==kResultOk,"editor size");[window setContentSize:NSMakeSize(r.getWidth(),r.getHeight())];
        provenance();
    }
    void detach(){
        check(editor->removed()==kResultOk,"remove editor");editor.reset();
        check(!handler->depth && handler->starts==handler->ends,"editor removal balances pending gesture");
        check(window.contentView.subviews.count==0,"owned native subtree removed");
    }
    void provenance(){
        const char* identifiers[]={just::foundationViewIdentifier,just::rotaryViewIdentifier,just::analysisViewIdentifier};
        const char* suffixes[]={"FoundationView","RotaryView","AnalysisView"};
        const SEL selectors[]={@selector(layout),@selector(mouseDragged:),@selector(drawRect:)};
        NSString* bundlePath=[[NSString stringWithUTF8String:bundle.c_str()] stringByResolvingSymlinksInPath];
        for(unsigned i=0;i<3;++i){
            NSView* v=find(window.contentView,identifiers[i]);check(v!=nil,"stable owned view identifier exists");
            Class cls=object_getClass(v);std::string expected=prefix+"_"+suffixes[i];
            check(expected==class_getName(cls),"per-product runtime class name");
            check(objc_getClass(expected.c_str())==cls,"global name resolves its own class");
            const char* image=class_getImageName(cls);check(image!=nullptr,"class image reported");
            NSString* imagePath=[[NSString stringWithUTF8String:image] stringByResolvingSymlinksInPath];
            check([imagePath hasPrefix:[bundlePath stringByAppendingString:@"/"]],"class defined in correct loaded bundle");
            Method method=class_getInstanceMethod(cls,selectors[i]);check(method!=nullptr,"own implementation present");
            Dl_info info{};check(dladdr(reinterpret_cast<const void*>(method_getImplementation(method)),&info)!=0 && info.dli_fname,"method image reported");
            NSString* impPath=[[NSString stringWithUTF8String:info.dli_fname] stringByResolvingSymlinksInPath];
            check([impPath isEqualToString:imagePath],"method IMP originates in same product image");
            if(classes[i])check(classes[i]==cls,"reopen retains correct class identity");classes[i]=cls;
            std::cout<<expected<<" class/IMP "<<image<<"\n";
        }
    }
    void render(bool silent=false){
        for(unsigned i=0;i<64;++i){left[i]=silent?0:float(.7*std::sin((blocks*64+i)*.071));right[i]=silent?0:float(-.4*std::cos((blocks*64+i)*.113));}
        float* input[]={left.data(),right.data()};float* output[]={outLeft.data(),outRight.data()};float* ref[]={refLeft.data(),refRight.data()};
        AudioBusBuffers in{},out{},refOut{};in.numChannels=out.numChannels=refOut.numChannels=2;
        in.channelBuffers32=input;out.channelBuffers32=output;refOut.channelBuffers32=ref;in.silenceFlags=silent?3:0;
        ParameterChanges events;for(const auto& e:handler->events){int32 index=0,point=0;events.addParameterData(e.first,index)->addPoint(0,e.second,point);}handler->events.clear();
        ProcessData data{};data.numSamples=64;data.symbolicSampleSize=kSample32;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.inputParameterChanges=&events;
        auto refData=data;refData.outputs=&refOut;
        check(processor->process(data)==kResultOk && twin->process(refData)==kResultOk,"process matching causal effects");
        check(std::memcmp(outLeft.data(),refLeft.data(),sizeof(outLeft))==0 && std::memcmp(outRight.data(),refRight.data(),sizeof(outRight))==0,"simultaneous UI preserves bit-exact reference audio");
        check(state(component)==state(reference) && out.silenceFlags==refOut.silenceFlags,"complete state and output silence match");
        for(unsigned i=0;i<64;++i){check(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]),"finite effect output");changed|=outLeft[i]!=left[i] || outRight[i]!=right[i];tail|=silent && (outLeft[i]!=0 || outRight[i]!=0);}++blocks;
    }
    void down(){NSView* v=find(window.contentView,just::rotaryViewIdentifier);check(v!=nil,"rotary exists");[v mouseDown:mouse(v,NSEventTypeLeftMouseDown,70)];}
    static NSEvent* mouse(NSView* v,NSEventType type,double y){return [NSEvent mouseEventWithType:type location:[v convertPoint:NSMakePoint(56,y) toView:nil] modifierFlags:0 timestamp:0 windowNumber:v.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];}
    void drag(){NSView* v=find(window.contentView,just::rotaryViewIdentifier);check(v!=nil,"rotary drag target exists");down();[v mouseDragged:mouse(v,NSEventTypeLeftMouseDragged,60)];[v mouseUp:mouse(v,NSEventTypeLeftMouseUp,60)];}
    void toggle(){NSButton* b=button(window.contentView,@"Advanced");check(b!=nil,"common Advanced control");auto before=state(component);auto writes=handler->writes;[b performClick:nil];check(state(component)==before && handler->writes==writes,"Advanced changes only view");}
    void resize(){ViewRect r{0,0,840,520};check(editor->checkSizeConstraint(&r)==kResultOk && editor->onSize(&r)==kResultOk,"resize native editor");[window setContentSize:NSMakeSize(r.getWidth(),r.getHeight())];provenance();}
    ~Instance(){if(editor)detach();processor->setProcessing(false);twin->setProcessing(false);component->setActive(false);reference->setActive(false);pConnection->disconnect(cConnection);cConnection->disconnect(pConnection);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();reference->terminate();[window close];}
};
int main(int argc,char** argv){@autoreleasepool {
    check(argc==5,"usage: host bundle namespace bundle namespace");
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];HostApplication host;
    {
        Instance first(argv[1],argv[2],host),second(argv[3],argv[4],host);
        for(unsigned i=0;i<3;++i)check(first.classes[i]!=second.classes[i],"bundles have distinct runtime classes");
        for(const char* old:{"JustFoundationView","JustRotaryView","JustAnalysisView"})check(objc_getClass(old)==nullptr,"legacy collision name absent");
        first.render();second.render();
        double peer=second.controller->getParamNormalized(1);first.drag();check(first.handler->writes>0 && !second.handler->writes && second.controller->getParamNormalized(1)==peer,"first gesture cannot edit peer");
        first.render();second.render();auto own=first.controller->getParamNormalized(1);second.drag();check(first.controller->getParamNormalized(1)==own,"peer gesture cannot edit first");first.render();second.render();
        first.resize();second.resize();for(unsigned i=0;i<8;++i){first.toggle();second.toggle();first.render(i>2);second.render(i>2);}
        first.down();first.detach();first.render(true);second.drag();second.render();first.attach();first.render(true);second.render(true);
        second.down();second.detach();second.render(true);first.drag();first.render();second.attach();first.render(true);second.render(true);
        check(first.changed && second.changed && first.tail && second.tail,"both are causal effects with continuing tails");
    }
    std::cout<<"PASS "<<checks<<" two-bundle same-process class/IMP provenance, isolated gestures, resize, view-only toggles, close/reopen, bit-exact causal twins\n";
}}
