#import <Cocoa/Cocoa.h>
#include "ShellSelectorsMac.hpp"
#include "../Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
namespace jm=just::module;
static unsigned checks=0;
static void check(bool p,const char* text){++checks;if(!p){std::cerr<<"FAIL: "<<text<<"\n";std::exit(1);}}
static void point(ParameterChanges& changes,ParamID id,double value,int32 offset=0) {
    int32 index=0;auto* queue=changes.addParameterData(id,index);
    check(queue && queue->addPoint(offset,value,index)==kResultOk,"host automation enqueue");
}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;ParamID active=~ParamID(0);ParameterChanges pending{21};
    tresult PLUGIN_API beginEdit(ParamID id) override {
        check(active==~ParamID(0) && jm::registry.index(id)<jm::registry.count,"one known gesture starts");active=id;++starts;return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override {
        check(active==id && std::isfinite(value) && value>=0 && value<=1,"gesture writes bounded same ID");
        point(pending,id,value);++writes;return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        check(active==id,"gesture closes same ID");active=~ParamID(0);++ends;return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
struct Instance {
    IPtr<IComponent> component;IPtr<IEditController> controller;FUnknownPtr<IAudioProcessor> processor;
    Instance(const VST3::Hosting::PluginFactory& factory,HostApplication& host) {
        for(const auto& info:factory.classInfos()) {
            if(info.category()==kVstAudioEffectClass)component=factory.createInstance<IComponent>(info.ID());
            if(info.category()==kVstComponentControllerClass)controller=factory.createInstance<IEditController>(info.ID());
        }
        check(component && controller,"actual bundle creates processor/controller");
        check(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize real instance");
        processor=FUnknownPtr<IAudioProcessor>(component);
        SpeakerArrangement ins[]={SpeakerArr::kStereo,SpeakerArr::kMono},outs[]={SpeakerArr::kStereo};
        check(processor && processor->setBusArrangements(ins,2,outs,1)==kResultOk,"stereo main plus mono optional SC arrangement");
        check(component->activateBus(kAudio,kInput,0,true)==kResultOk && component->activateBus(kAudio,kOutput,0,true)==kResultOk,"main buses active");
        check(component->activateBus(kAudio,kInput,1,true)==kResultOk,"optional mono auxiliary bus active in routed fixture");
        ProcessSetup setup{kRealtime,kSample64,256,48000};
        check(processor->setupProcessing(setup)==kResultOk,"real audio setup");
        check(component->setActive(true)==kResultOk,"real component activation");
        const auto processing=processor->setProcessing(true);
        check(processing==kResultOk || processing==kNotImplemented,"audio processing notification (optional SDK implementation)");
    }
    ~Instance(){processor->setProcessing(false);component->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();}
    void restore(const just::SoundState& s) {
        MemoryStream stream;check(just::writeSoundState(&stream,s,jm::registry),"write complete physical registry snapshot");
        stream.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&stream)==kResultOk,"restore processor snapshot");
        stream.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&stream)==kResultOk,"restore controller sound targets");
    }
    just::SoundState state() {
        MemoryStream stream;check(component->getState(&stream)==kResultOk,"serialize actual processor targets");
        stream.seek(0,IBStream::kIBSeekSet,nullptr);just::SoundState s;
        check(just::readSoundState(&stream,just::pluginIdentities[4].processor,jm::registry,s),"decode complete processor snapshot");return s;
    }
    void flush(ParameterChanges* changes=nullptr) {
        ProcessData data{};data.symbolicSampleSize=kSample64;data.inputParameterChanges=changes;
        check(processor->process(data)==kResultOk,"zero-sample sound restore/automation");
    }
    std::array<double,256> render(double amplitude=.5,double* sc=nullptr,ParameterChanges* changes=nullptr) {
        std::array<double,256> l{},r{},ol{},orr{};l.fill(amplitude);r.fill(-amplitude);
        double* in[]={l.data(),r.data()};double* out[]={ol.data(),orr.data()};double* aux[]={sc};
        AudioBusBuffers inputs[2]{},output{};inputs[0].numChannels=output.numChannels=2;
        inputs[0].channelBuffers64=in;output.channelBuffers64=out;inputs[1].numChannels=sc?1:0;inputs[1].channelBuffers64=aux;
        ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample64;data.numInputs=sc?2:1;data.numOutputs=1;
        data.inputs=inputs;data.outputs=&output;data.inputParameterChanges=changes;
        check(processor->process(data)==kResultOk,"real double audio block");
        for(unsigned i=0;i<256;++i)check(std::isfinite(ol[i]) && ol[i]==-orr[i],"finite linked antiphase stereo output");return ol;
    }
};
static void set(just::SoundState& s,ParamID id,double physical){const auto i=jm::registry.index(id);check(i<jm::registry.count,"state ID exists");s.targets[i]=jm::parameters[i].toNormalized(physical);}
static NSControl* control(NSView* parent,NSInteger tag,Class kind) {
    for(NSView* view in parent.subviews) {
        if([view isKindOfClass:kind] && [(NSControl*)view tag]==tag)return (NSControl*)view;
        if(auto* nested=control(view,tag,kind))return nested;
    }return nil;
}
static NSView* rotary(NSView* parent,just::ParamID id) {
    NSString* identifier=[NSString stringWithFormat:@"just.compressor.parameter.%u",id];
    for(NSView* view in parent.subviews){if([view.accessibilityIdentifier isEqualToString:identifier])return view;if(auto* nested=rotary(view,id))return nested;}return nil;
}
static NSTextField* valueField(NSView* parent) {
    for(NSView* view in parent.subviews){if([view isKindOfClass:NSTextField.class] && [(NSTextField*)view isEditable])return (NSTextField*)view;if(auto* nested=valueField(view))return nested;}return nil;
}
static bool containsText(NSView* parent,NSString* text) {
    for(NSView* view in parent.subviews) {
        if([view isKindOfClass:NSTextField.class] && [[(NSTextField*)view stringValue] containsString:text])return true;
        if(containsText(view,text))return true;
    }return false;
}
static void saveImage(NSView* view,const std::string& path) {
    [view layoutSubtreeIfNeeded];NSBitmapImageRep* rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    check([png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"rendered native UI screenshot");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        const bool explicitBundle=argc>1 && std::string(argv[1])=="--bundle";
        check(!explicitBundle || argc>=3,"usage [evidence-dir] or --bundle <exact-bundle> [evidence-dir]");
        const char* bundlePath=explicitBundle?argv[2]:JUST_COMPRESSOR_BUNDLE_PATH;
        const std::string evidencePath=explicitBundle?(argc>3?argv[3]:""):(argc>1?argv[1]:"");
        std::cout<<"Loaded-bundle path: "<<bundlePath<<"\n";
        std::string error;auto module=VST3::Hosting::Module::create(bundlePath,error);check(bool(module),error.c_str());
        HostApplication host;Instance observed(module->getFactory(),host),reference(module->getFactory(),host);
        check(observed.controller->getParameterCount()==21,"actual host sees all approved 21 parameters");
        for(int i=0;i<21;++i){ParameterInfo info{};check(observed.controller->getParameterInfo(i,info)==kResultOk && info.id==jm::parameters[i].id && info.defaultNormalizedValue==jm::parameters[i].toNormalized(jm::parameters[i].initial),"literal IDs/Init exposed by loaded controller");}
        auto sound=just::initialState(just::pluginIdentities[4].processor,jm::registry);
        set(sound,100,-18);set(sound,101,4);set(sound,105,0);set(sound,107,0);set(sound,119,7.5);
        sound.configurationCount=1;sound.configurations[0]={1000,2};
        observed.restore(sound);reference.restore(sound);observed.flush();reference.flush();
        std::array<double,256> compressed{};
        for(int b=0;b<200;++b){compressed=observed.render();check(compressed==reference.render(),"identical compressor instances warm exactly");}
        const double expected=std::pow(10.0,(-18+(20*std::log10(.5)+18)/4)/20);
        check(std::abs(20*std::log10(compressed.back()/expected))<.001,"actual VST3 has compressor static curve");
        check(observed.processor->getLatencySamples()==0 && observed.processor->getTailSamples()==kNoTail,"truthful zero PDC/no audio tail despite saved lookahead request");
        auto handler=owned(new Handler);check(observed.controller->setComponentHandler(handler.get())==kResultOk,"record native gestures");
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,720,420)];
        auto view=owned(observed.controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"actual module native editor attaches");
        ViewRect bounds;view->getSize(&bounds);parent.frame=NSMakeRect(0,0,bounds.getWidth(),bounds.getHeight());
        auto* advanced=button(parent,@"Advanced");check(advanced && button(parent,@"Bypass"),"shared toolbar exists");
        // These are offscreen structure/automation checks. Actual mouse/keyboard
        // input is reserved for dot's serial native QA and is never simulated here.
        auto* lookahead=rotary(parent,119);check(lookahead && valueField(lookahead) && !valueField(lookahead).enabled,"nonzero lookahead editor is disabled");
        if(!evidencePath.empty())saveImage(parent,evidencePath+"/compressor-simple.png");
        for(int i=0;i<13;++i) {
            [advanced performClick:nil];
            check(handler->writes==0 && observed.state().targets==sound.targets,"Simple/Advanced never edits any sound target");
            check(observed.render()==reference.render(),"view changes preserve real envelope history and exact processed audio");
        }
        for(int id=100;id<=119;++id)check(rotary(parent,id)!=nil,"all frozen controls use actual shared Rotary views");
        if(!evidencePath.empty())saveImage(parent,evidencePath+"/compressor-advanced.png");
        MemoryStream ui;check(observed.controller->getState(&ui)==kResultOk,"view preferences serialize separately");
        view->removed();view.reset();view=owned(observed.controller->createView(ViewType::kEditor));
        check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk && button(parent,@"Advanced").state==NSControlStateValueOn,"reopen retains Advanced preference");
        check(observed.render()==reference.render(),"editor reopen preserves real compressed audio");
        ParameterChanges hostEdits(21);
        const double thresholdTarget=jm::parameters[1].toNormalized(-24),preciseRelease=jm::parameters[4].toNormalized(737.712343);
        point(hostEdits,100,thresholdTarget);point(hostEdits,103,preciseRelease);
        check(observed.controller->setParamNormalized(100,thresholdTarget)==kResultOk && observed.controller->setParamNormalized(103,preciseRelease)==kResultOk,"real host automation updates controller targets");
        observed.flush(&hostEdits);hostEdits.clearQueue();
        check(observed.state().targets[1]==thresholdTarget && observed.state().targets[4]==preciseRelease,"host zero-block automation reaches processor with full precision");
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.04]];
        check([valueField(rotary(parent,103)).stringValue isEqualToString:@"737.7"] && containsText(rotary(parent,103),@"ms"),"common separately renders idle number and persistent unit without quantizing sound");
        check(handler->writes==0 && handler->starts==0 && handler->ends==0,"host updates and native refresh do not emit UI edits");
        auto saved=observed.state();check(saved.configurationCount==1 && saved.configurations[0].value==2 && saved.targets[20]==.75,"unapplied Lookahead request and Maximum survive all UI actions");
        view->removed();view.reset();ui.seek(0,IBStream::kIBSeekSet,nullptr);
        check(observed.controller->setState(&ui)==kResultOk && observed.controller->getParamNormalized(100)==saved.targets[1],"UI preference restore cannot overwrite sound target");
        observed.restore(saved);reference.restore(saved);observed.flush();reference.flush();
        // Fresh setup clears both histories, then both read the same complete snapshot.
        ProcessSetup setup{kRealtime,kSample64,256,48000};
        check(observed.processor->setupProcessing(setup)==kResultOk && reference.processor->setupProcessing(setup)==kResultOk,"matched reprepare for restored reference");
        for(int b=0;b<50;++b)check(observed.render()==reference.render(),"serialized hidden targets restore identical audio");
        ParameterChanges a(21),b(21);const auto t=jm::parameters[1].toNormalized(-30),r=jm::parameters[2].toNormalized(8);
        point(a,100,t,63);point(a,101,r,63);point(a,104,1,63);
        point(b,104,1,63);point(b,101,r,63);point(b,100,t,63);
        check(observed.render(.5,nullptr,&a)==reference.render(.5,nullptr,&b),"same-offset actual automation is independent of queue order");
        check(observed.state().targets[1]==t && observed.state().targets[2]==r && observed.state().targets[5]==1,"automation stores final continuous and discrete targets");
        // External SC is optional; missing and silent buses do not fall back to main audio.
        set(sound,113,1);set(sound,100,-24);observed.restore(sound);observed.flush();
        for(int i=0;i<600;++i)compressed=observed.render();check(std::abs(compressed.back()-.5)<1e-10,"missing external bus releases compression to unity");
        std::array<double,256> sidechain{};sidechain.fill(.8);
        for(int i=0;i<200;++i)compressed=observed.render(.5,sidechain.data());check(compressed.back()<.15,"mono external SC really drives stereo compression");
        sidechain.fill(0);for(int i=0;i<600;++i)compressed=observed.render(.5,sidechain.data());
        check(std::abs(compressed.back()-.5)<1e-10,"connected silent SC releases to unity without internal fallback");
        check(handler->starts==handler->ends,"no outstanding native edit gesture");
        std::cout<<"PASS loaded Compressor VST3 offscreen: 21 parameters, actual compression, paired audio through 13 view toggles/reopen, complete state, shared control structure, host automation, optional SC, truthful PDC: "<<checks<<" checks; actual native input remains serial QA\n";
    }
}
