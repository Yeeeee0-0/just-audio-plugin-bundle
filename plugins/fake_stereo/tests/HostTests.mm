#import <Cocoa/Cocoa.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "plugins/fake_stereo/Parameters.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool v,const char* m){if(!v){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;
    tresult PLUGIN_API beginEdit(ParamID id) override{check(just::stereo::registry.index(id)<just::stereo::registry.count,"gesture known ID");++starts;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue n) override{check(std::isfinite(n) && n>=0 && n<=1,"finite normalized UI edit");++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
struct Instance {
    IPtr<IComponent> component;IPtr<IEditController> controller;FUnknownPtr<IAudioProcessor> processor;
    Instance(const VST3::Hosting::Module::Ptr& module,HostApplication& host) {
        auto classes=module->getFactory().classInfos();check(classes.size()==2,"factory identities");
        component=module->getFactory().createInstance<IComponent>(classes[0].ID());controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        check(component && controller,"factory instantiation");
        check(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");processor=component;
    }
    ~Instance(){processor->setProcessing(false);component->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();}
    void prepare(unsigned ni=2,unsigned no=2,double fs=48000,int32 sampleSize=kSample64) {
        SpeakerArrangement in=ni==1?SpeakerArr::kMono:SpeakerArr::kStereo,out=no==1?SpeakerArr::kMono:SpeakerArr::kStereo;
        check(processor->setBusArrangements(&in,1,&out,1)==kResultOk,"valid bus arrangement");
        ProcessSetup setup{kRealtime,sampleSize,64,fs};check(processor->setupProcessing(setup)==kResultOk,"prepare");
        component->setActive(true);processor->setProcessing(true);
        check(processor->getLatencySamples()==0 && processor->getTailSamples()==uint32(2*fs),"actual zero PDC / 2s tail");
    }
    void restore(const just::SoundState& s) {
        auto bytes=just::encodeState(s,just::stereo::registry);MemoryStream stream(bytes.data(),TSize(bytes.size()));
        check(component->setState(&stream)==kResultOk,"restore full sound");stream.seek(0,IBStream::kIBSeekSet,nullptr);
        check(controller->setComponentState(&stream)==kResultOk,"controller target restore");
    }
    just::SoundState state() {
        MemoryStream stream;check(component->getState(&stream)==kResultOk,"get complete sound");just::SoundState s;
        check(just::decodeState(reinterpret_cast<const std::uint8_t*>(stream.getData()),stream.getSize(),just::pluginIdentities[8].processor,just::stereo::registry,s)==just::StateResult::ok,"decode complete state");return s;
    }
};
NSButton* button(NSView* root,NSString* title) {
    NSString* action=[title isEqualToString:@"Advanced"]?@"changeView:":[title isEqualToString:@"Bypass"]?@"changeBypass:":[title isEqualToString:@"Preset"]?@"showPresets:":nil;
    for(NSView* v in root.subviews){if([v isKindOfClass:NSButton.class] && ((action && [(NSButton*)v action]==NSSelectorFromString(action)) || [[(NSButton*)v title] isEqualToString:title]))return (NSButton*)v;if(auto* nested=button(v,title))return nested;}return nil;
}
NSControl* control(NSView* root,NSString* identifier) {
    for(NSView* v in root.subviews){if([v isKindOfClass:NSControl.class] && [v.accessibilityIdentifier isEqualToString:identifier])return (NSControl*)v;if(auto* nested=control(v,identifier))return nested;}return nil;
}
NSView* identified(NSView* root,NSString* identifier) {
    if([root.accessibilityIdentifier isEqualToString:identifier])return root;
    for(NSView* v in root.subviews)if(auto* found=identified(v,identifier))return found;return nil;
}
NSTextField* editable(NSView* root) {
    if([root isKindOfClass:NSTextField.class] && [(NSTextField*)root isEditable])return (NSTextField*)root;
    for(NSView* v in root.subviews)if(auto* found=editable(v))return found;return nil;
}
void screenshot(NSView* root,const std::string& filename) {
    [root layoutSubtreeIfNeeded];auto* image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];[root cacheDisplayInRect:root.bounds toBitmapImageRep:image];
    auto* bytes=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([bytes writeToFile:[NSString stringWithUTF8String:filename.c_str()] atomically:YES],"actual editor screenshot");
}
template<class T> void loadedAudioLayout(Instance& instance,unsigned ni,unsigned no,int32 format) {
    std::array<T,64> l{},r{},yl{},yr{};T* in[]={l.data(),ni==2?r.data():nullptr},*out[]={yl.data(),no==2?yr.data():nullptr};
    AudioBusBuffers ib{},ob{};ib.numChannels=ni;ob.numChannels=no;
    if constexpr(sizeof(T)==4){ib.channelBuffers32=in;ob.channelBuffers32=out;}
    else {ib.channelBuffers64=in;ob.channelBuffers64=out;}
    ProcessData data{};data.numInputs=data.numOutputs=1;data.numSamples=64;data.symbolicSampleSize=format;data.inputs=&ib;data.outputs=&ob;
    double change=0;
    for(unsigned block=0;block<3;++block) {
        for(unsigned i=0;i<64;++i){l[i]=T(std::sin((block*64+i)*.097));r[i]=T(std::cos((block*64+i)*.023)*.5);}
        check(instance.processor->process(data)==kResultOk,"loaded audio layout process");
        for(unsigned i=0;i<64;++i) {
            const double mid=ni==1?double(l[i]):.5*double(l[i])+.5*double(r[i]);
            const double folded=no==1?double(yl[i]):.5*double(yl[i])+.5*double(yr[i]);
            check(std::isfinite(yl[i]) && (no==1 || std::isfinite(yr[i])),"loaded layout finite samples");
            check(std::abs(mid-folded)<(sizeof(T)==4?1e-6:1e-12),"loaded mono/stereo layout Mid identity");
            if(no==2)change+=std::abs(double(yl[i])-double(l[i]));
        }
    }
    if(no==2)check(change>1e-5,"loaded effect generates Side in stereo-output layouts");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc>=2,"usage: fake_stereo_host_tests VST3 [image-dir]");[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());HostApplication host;
        Instance subject(module,host),reference(module,host);subject.prepare();reference.prepare();
        check(subject.controller->getParameterCount()==18,"host sees reviewed registry");
        for(int32 i=0;i<18;++i){ParameterInfo info{};check(subject.controller->getParameterInfo(i,info)==kResultOk && info.id==just::stereo::parameters[i].id,"host golden IDs");}
        auto s=just::initialState(just::pluginIdentities[8].processor,just::stereo::registry);s.seed=20261002;
        auto set=[&](just::ParamID id,double physical){s.targets[just::stereo::registry.index(id)]=just::stereo::spec(id).toNormalized(physical);};
        set(just::stereo::Width,134);set(just::stereo::Existing,37);set(just::stereo::Mix,58);set(just::stereo::High,12345);
        subject.restore(s);reference.restore(s);check(subject.state().targets==s.targets,"pending state getState retains all hidden targets");
        auto handler=owned(new Handler);subject.controller->setComponentHandler(handler.get());
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,720,420)];
        auto editor=owned(subject.controller->createView(ViewType::kEditor));
        check(editor && editor->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native editor attach");
        auto* advanced=button(parent,@"Advanced");check(advanced && editable(identified(parent,@"stereo.fader.60")) && control(parent,@"stereo.input-mode"),"real core controls");
        const unsigned untouched=handler->writes;
        if(argc>=3)screenshot(parent,std::string(argv[2])+"/simple.png");
        std::array<double,64> l{},r{},yl{},yr{},referenceL{},referenceR{};
        double* in[]={l.data(),r.data()},*out[]={yl.data(),yr.data()},*ref[]={referenceL.data(),referenceR.data()};
        AudioBusBuffers ib{},ob{};ib.numChannels=ob.numChannels=2;ib.channelBuffers64=in;ob.channelBuffers64=out;
        ProcessData data{};data.numInputs=data.numOutputs=1;data.numSamples=64;data.symbolicSampleSize=kSample64;data.inputs=&ib;data.outputs=&ob;
        for(unsigned block=0;block<100;++block) {
            for(unsigned i=0;i<64;++i){l[i]=std::sin((block*64+i)*.097);r[i]=std::cos((block*64+i)*.023)*.5;}
            [advanced performClick:nil];check(handler->writes==untouched,"Simple/Advanced never writes sound");
            check(subject.processor->process(data)==kResultOk,"subject process");ob.channelBuffers64=ref;
            check(reference.processor->process(data)==kResultOk,"reference process");ob.channelBuffers64=out;
            check(yl==referenceL && yr==referenceR,"bit-identical active effect across 100 view switches");
            for(unsigned i=0;i<64;++i)check(std::abs(.5*yl[i]+.5*yr[i]-(.5*l[i]+.5*r[i]))<1e-12,"loaded effect Mid invariant");
            if(block==50){editor->removed();editor.reset();editor=owned(subject.controller->createView(ViewType::kEditor));check(editor->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"close/reopen during side tail");advanced=button(parent,@"Advanced");}
        }
        [advanced performClick:nil];if(argc>=3)screenshot(parent,std::string(argv[2])+"/advanced.png");
        MemoryStream editorState;check(subject.controller->getState(&editorState)==kResultOk,"UI preferences encode");
        editor->removed();editor.reset();editorState.seek(0,IBStream::kIBSeekSet,nullptr);subject.controller->setState(&editorState);
        check(subject.state().targets==s.targets,"view preferences cannot overwrite processor sound");
        editor=owned(subject.controller->createView(ViewType::kEditor));check(editor->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"second reopen");
        [(NSButton*)control(parent,@"stereo.mono-check") performClick:nil];
        check(subject.controller->getParamNormalized(just::stereo::Mono)==1 && handler->starts==handler->ends && handler->writes==untouched+1,"native Mono sends one complete gesture");
        editor->removed();editor.reset();
        // All points arrive at their sample offset through the actual VST3 bridge.
        ParameterChanges changes;int32 index=0,point=0;
        changes.addParameterData(just::stereo::Width,index)->addPoint(7,1,point);
        changes.addParameterData(just::stereo::Mix,index)->addPoint(7,.37,point);
        changes.addParameterData(just::stereo::Character,index)->addPoint(7,1,point);
        data.inputParameterChanges=&changes;check(subject.processor->process(data)==kResultOk,"host automation curves");
        auto end=subject.state();check(end.targets[just::stereo::registry.index(just::stereo::Width)]==1 && end.targets[just::stereo::registry.index(just::stereo::Mix)]==.37 && end.targets[just::stereo::registry.index(just::stereo::Character)]==1,"same-offset host targets retained");
        ParameterChanges flush;flush.addParameterData(just::stereo::Mono,index)->addPoint(0,1,point);data.numSamples=0;data.inputParameterChanges=&flush;
        check(subject.processor->process(data)==kResultOk && subject.state().targets[just::stereo::registry.index(just::stereo::Mono)]==1,"zero-sample automation flush");
        // Development host benchmark with the native editor attached. UI work is
        // outside the timed process region; this is not target-DAW scheduling QA.
        subject.restore(s);editor=owned(subject.controller->createView(ViewType::kEditor));
        check(editor->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"benchmark editor attached");
        auto* widthField=(NSTextField*)editable(identified(parent,@"stereo.fader.60"));check(widthField!=nil,"width numeric field is accessible");
        const auto preciseWidth=just::stereo::spec(just::stereo::FieldWidth).toNormalized(1.340123456789);
        subject.controller->setParamNormalized(just::stereo::FieldWidth,preciseWidth);const auto focusWrites=handler->writes;
        [widthField.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:widthField]];
        check(std::abs(widthField.doubleValue-1.340123456789)<1e-11,"width focus exposes original physical precision");
        [widthField sendAction:widthField.action to:widthField.target];
        check(handler->writes==focusWrites && subject.controller->getParamNormalized(just::stereo::FieldWidth)==preciseWidth,"unmodified width text commits no gesture or quantization");
        [widthField.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:widthField]];
        data.numSamples=64;data.inputParameterChanges=nullptr;
        for(unsigned n=0;n<200;++n)subject.processor->process(data);
        std::vector<double> durations;durations.reserve(10000);
        for(unsigned n=0;n<10000;++n) {
            const auto start=std::chrono::steady_clock::now();const auto result=subject.processor->process(data);
            const auto endTime=std::chrono::steady_clock::now();check(result==kResultOk,"benchmark process");
            durations.push_back(std::chrono::duration<double,std::micro>(endTime-start).count());
            if(n%128==0)[NSRunLoop.currentRunLoop runUntilDate:NSDate.date];
        }
        std::sort(durations.begin(),durations.end());
        const double p99=durations[9900];std::cout<<"BENCH 48kHz/64/stereo/native-editor-attached process p99 "<<p99<<" us (block budget "<<64./48000*1e6<<" us)\n";
        editor->removed();editor.reset();
        for(auto fs:{44100.,48000.,96000.,192000.})for(unsigned ni:{1u,2u})for(unsigned no:{1u,2u})for(auto format:{kSample32,kSample64}) {
            if(ni==2 && no==1)continue;Instance layout(module,host);layout.prepare(ni,no,fs,format);
            if(format==kSample32)loadedAudioLayout<float>(layout,ni,no,format);else loadedAudioLayout<double>(layout,ni,no,format);
        }
        std::cout<<"PASS loaded VST3 reviewed IDs, 24 float/double/rate/audio-layout cases, PDC/tail, complete state, sample-offset/zero-block automation, native controls, 100 pure view switches and reopen with bit-identical effect audio\n";
    }
}
