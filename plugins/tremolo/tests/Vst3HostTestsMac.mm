#import <Cocoa/Cocoa.h>
#include "common/ui/ObjCNames.hpp"
#include "../Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace t=just::tremolo;
static unsigned checks=0;
static void check(bool value,const char* label) {
    ++checks;if(!value){std::cerr<<"FAIL loaded VST3: "<<label<<'\n';std::exit(1);}
}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;ParamID active=~ParamID(0),lastID=0;ParamValue lastValue=0;
    tresult PLUGIN_API beginEdit(ParamID id) override {
        check(t::registry.index(id)<t::registry.count && active==~ParamID(0),"valid balanced beginEdit");
        active=id;++starts;return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override {
        check(active==id && std::isfinite(value) && value>=0 && value<=1,"finite normalized UI edit");
        lastID=id;lastValue=value;++writes;return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        check(active==id,"balanced endEdit");active=~ParamID(0);++ends;return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static NSButton* button(NSView* parent,NSString* title) {
    for(NSView* view in parent.subviews) {
        if([view isKindOfClass:NSButton.class] && [[(NSButton*)view title] isEqualToString:title])return (NSButton*)view;
        if(auto* nested=button(view,title))return nested;
    }return nil;
}
static NSView* amountControl(NSView* parent) {
    for(NSView* view in parent.subviews) {
        if([view.identifier isEqualToString:[NSString stringWithUTF8String:just::rotaryViewIdentifier]] && [view.accessibilityIdentifier isEqualToString:@"just.tremolo.primary.4352"])return view;
        if(auto* nested=amountControl(view))return nested;
    }return nil;
}
static void saveImage(NSView* view,const std::string& path) {
    [view layoutSubtreeIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    auto* data=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    check([data writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"native screenshot saved");
}
static std::vector<std::uint8_t> savedBytes(IComponent* component) {
    MemoryStream stream;check(component->getState(&stream)==kResultOk,"component getState");
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(stream.getData());return {bytes,bytes+stream.getSize()};
}
static just::SoundState decode(const std::vector<std::uint8_t>& bytes) {
    just::SoundState sound;
    check(just::decodeState(bytes.data(),bytes.size(),just::pluginIdentities[3].processor,t::registry,sound)==just::StateResult::ok,"decode actual component state");
    return sound;
}
static void restore(IComponent* component,IEditController* controller,const just::SoundState& sound) {
    auto bytes=just::encodeState(sound,t::registry);MemoryStream stream(bytes.data(),bytes.size());
    check(component->setState(&stream)==kResultOk,"complete component restore");
    if(controller){stream.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&stream)==kResultOk,"controller restores complete sound state");}
}
static void addPoint(ParameterChanges& changes,ParamID id,int offset,double value) {
    int32 index=0;auto* queue=changes.addParameterData(id,index);check(queue,"host automation queue");
    check(queue->addPoint(offset,value,index)==kResultOk,"host automation point");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc>=2,"usage: tremolo_vst3_host_tests <absolute-vst3-path> [screenshot-directory]");
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());
        HostApplication host;auto classes=module->getFactory().classInfos();check(classes.size()==2,"one processor/controller factory pair");
        auto first=module->getFactory().createInstance<IComponent>(classes[0].ID());
        auto second=module->getFactory().createInstance<IComponent>(classes[0].ID());
        auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        check(first && second && controller,"actual VST3 instances");
        check(first->initialize(&host)==kResultOk && second->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"actual host initialize");
        auto handler=owned(new Handler);check(controller->setComponentHandler(handler.get())==kResultOk,"component handler");
        check(controller->getParameterCount()==14,"approved 14 host parameters");
        for(unsigned i=0;i<t::registry.count;++i) {
            ParameterInfo info{};check(controller->getParameterInfo(i,info)==kResultOk,"host parameter info");
            const auto& spec=t::parameters[i];
            check(info.id==spec.id && info.stepCount==spec.stepCount && info.defaultNormalizedValue==spec.toNormalized(spec.initial),"IDs, enum counts and exact normalized Init");
            check((info.flags&ParameterInfo::kCanAutomate)!=0,"parameter automatable");
            check(bool(info.flags&ParameterInfo::kIsBypass)==(spec.id==t::bypass),"sole bypass flag");
            for(double value:{0.,0.5,1.}) {
                // Enum midpoints snap to legal ordinals under the fixed contract.
                const double expected=spec.toNormalized(spec.toPhysical(value));
                check(std::abs(controller->plainParamToNormalized(spec.id,controller->normalizedParamToPlain(spec.id,value))-expected)<1e-12,"actual host normalized mapping/enum snapping");
            }
        }
        FUnknownPtr<IAudioProcessor> a(first),b(second);check(a && b,"actual audio interfaces");
        ProcessSetup setup{kRealtime,kSample64,256,48000};
        check(a->setupProcessing(setup)==kResultOk && b->setupProcessing(setup)==kResultOk,"64-bit stereo setup");
        check(first->setActive(true)==kResultOk && second->setActive(true)==kResultOk,"activate processors");
        // The fixed SDK's default AudioEffect has no additional start/stop work.
        // A host may process when the optional callback returns kNotImplemented.
        const auto startA=a->setProcessing(true),startB=b->setProcessing(true);
        check((startA==kResultOk || startA==kNotImplemented) && (startB==kResultOk || startB==kNotImplemented),"start actual processing");
        check(a->getLatencySamples()==0 && a->getTailSamples()==0,"zero PDC and tail");
        auto initBytes=savedBytes(second.get());auto initial=decode(initBytes);
        restore(second.get(),controller.get(),initial);
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,760,460)];
        auto view=owned(controller->createView(ViewType::kEditor));check(bool(view),"actual VST3 editor create");
        check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"actual native editor attach");
        ViewRect rect;check(view->getSize(&rect)==kResultOk,"editor size");parent.frame=NSMakeRect(0,0,rect.getWidth(),rect.getHeight());
        auto* advanced=button(parent,@"Advanced");check(advanced && button(parent,@"Bypass"),"shared shell controls");
        if(argc>2){std::filesystem::create_directories(argv[2]);saveImage(parent,std::string(argv[2])+"/simple.png");}
        [advanced performClick:nil];
        ViewRect expanded(0,0,rect.getWidth(),420);
        check(view->onSize(&expanded)==kResultOk,"actual Advanced resize");parent.frame=NSMakeRect(0,0,expanded.getWidth(),expanded.getHeight());
        if(argc>2)saveImage(parent,std::string(argv[2])+"/advanced.png");
        check(view->onSize(&rect)==kResultOk,"actual Simple size restore");parent.frame=NSMakeRect(0,0,rect.getWidth(),rect.getHeight());
        [advanced performClick:nil];check(handler->writes==0,"preview view switches do not edit sound");
        auto custom=initial;custom.seed=0x1234ABCD;
        auto set=[&](ParamID id,double value){custom.targets[t::registry.index(id)]=t::spec(id).toNormalized(value);};
        set(t::depth,75);set(t::mix,65);set(t::shape,3);set(t::phase,360);set(t::stereoPhase,180);set(t::duty,73);set(t::edgeMs,6.7);
        restore(first.get(),nullptr,custom);restore(second.get(),controller.get(),custom);
        std::array<double,256> input,leftA,rightA,leftB,rightB;input.fill(0.75);
        double* ins[]={input.data(),input.data()};double* outsA[]={leftA.data(),rightA.data()};double* outsB[]={leftB.data(),rightB.data()};
        AudioBusBuffers in{},outA{},outB{};in.numChannels=outA.numChannels=outB.numChannels=2;
        in.channelBuffers64=ins;outA.channelBuffers64=outsA;outB.channelBuffers64=outsB;
        ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;
        auto renderBoth=[&]() {
            data.outputs=&outA;check(a->process(data)==kResultOk,"first actual processor callback");
            data.outputs=&outB;check(b->process(data)==kResultOk,"second actual processor callback");
            check(leftA==leftB && rightA==rightB,"editor operations preserve bit-exact actual LFO audio");
            for(double sample:leftB)check(std::isfinite(sample) && sample>=0 && sample<=0.75,"actual nonnegative AM output");
        };
        renderBoth();check(decode(savedBytes(second.get())).targets==custom.targets,"all hidden targets restored");
        double departure=0;
        for(unsigned i=0;i<16;++i) {
            [advanced performClick:nil];renderBoth();
            for(double sample:leftB)departure=std::max(departure,0.75-sample);
            for(unsigned p=0;p<t::registry.count;++p)check(controller->getParamNormalized(t::parameters[p].id)==custom.targets[p],"view toggle preserves every controller target");
            check(handler->writes==0,"view toggles produce no host gestures");
        }
        check(departure>0.1,"loaded VST3 produces real tremolo rather than pass-through");
        ParameterChanges changes;addPoint(changes,t::depth,0,0.2);addPoint(changes,t::depth,96,0.9);addPoint(changes,t::depth,255,0.6);
        addPoint(changes,t::shape,16,0.25);addPoint(changes,t::shape,128,1.0);
        data.inputParameterChanges=&changes;renderBoth();data.inputParameterChanges=nullptr;
        auto automated=decode(savedBytes(second.get()));
        check(automated.targets[t::registry.index(t::depth)]==0.6 && automated.targets[t::registry.index(t::shape)]==1,"actual multi-point automation final targets");
        auto automatedBytes=savedBytes(second.get());MemoryStream autoStream(automatedBytes.data(),automatedBytes.size());
        check(controller->setComponentState(&autoStream)==kResultOk,"host sends automated state to controller");
        [advanced performClick:nil];MemoryStream ui;check(controller->getState(&ui)==kResultOk,"separate actual editor preferences saved");
        check(view->removed()==kResultOk,"actual editor removed");view.reset();renderBoth();
        view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"actual editor reopened");
        check(button(parent,@"Advanced").state==NSControlStateValueOn,"view preference persists on reopen");renderBoth();
        check(handler->writes==0,"close/reopen does not edit sound");
        auto beforeBad=savedBytes(second.get());MemoryStream truncated(beforeBad.data(),beforeBad.size()-1);
        check(second->setState(&truncated)!=kResultOk,"truncated actual sound restore rejected");
        check(savedBytes(second.get())==beforeBad,"failed actual restore is transactional");
        check(controller->setParamNormalized(t::depth,std::numeric_limits<double>::infinity())!=kResultOk,"infinity never accepted as host target");
        [button(parent,@"Advanced") performClick:nil];auto* amount=amountControl(parent);check(amount && !amount.isHiddenOrHasHiddenAncestor,"loaded Simple uses shared Amount control");
        // Host automation fixture, deliberately separate from OS-input QA.
        check(controller->setParamNormalized(t::depth,1)==kResultOk,"finite host Depth endpoint");
        check(controller->getParamNormalized(t::mix)==custom.targets[t::registry.index(t::mix)],"host Depth endpoint preserves Mix");
        ParameterChanges endpoint;addPoint(endpoint,t::depth,0,1);data.inputParameterChanges=&endpoint;data.outputs=&outB;
        check(b->process(data)==kResultOk,"host delivers endpoint automation");data.inputParameterChanges=nullptr;
        check(decode(savedBytes(second.get())).targets[t::registry.index(t::depth)]==1,"actual processor receives finite Depth target");
        check(controller->setParamNormalized(t::bypass,1)==kResultOk,"configure actual bypass target");
        ui.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setState(&ui)==kResultOk && controller->getParamNormalized(t::bypass)==1,"editor state restore cannot overwrite sound targets");
        check(view->removed()==kResultOk,"final editor teardown");view.reset();
        a->setProcessing(false);b->setProcessing(false);first->setActive(false);second->setActive(false);
        controller->setComponentHandler(nullptr);controller->terminate();first->terminate();second->terminate();
        std::cout<<"PASS loaded Tremolo VST3: "<<checks<<" checks; approved14 parameters, actual AM, multi-point automation, complete/transactional state,16 view toggles, close/reopen bit-exact audio and passive shared-control layout; OS-input QA pending\n";
    }
}
