#import <Cocoa/Cocoa.h>
#include "plugins/gate/UIModel.hpp"
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
#include <array>
#include <filesystem>
#include <limits>
#include <chrono>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace g=just::gate;
static unsigned checks=0;
void check(bool value,const char* label){++checks;if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
void near(double a,double b,double e,const char* label){check(std::abs(a-b)<=e,label);}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;ParamID last=0;ParamValue value=0;
    tresult PLUGIN_API beginEdit(ParamID id) override{++starts;last=id;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v) override{++writes;last=id;value=v;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{++ends;last=id;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
struct Pair {
    HostApplication host;
    IPtr<IComponent> component;IPtr<IEditController> controller;FUnknownPtr<IAudioProcessor> processor;
    IPtr<Handler> handler;double rate=48000;unsigned scChannels=0;
    explicit Pair(VST3::Hosting::Module& module,unsigned sidechainChannels=0):scChannels(sidechainChannels) {
        auto classes=module.getFactory().classInfos();check(classes.size()==2,"factory has fixed pair");
        component=module.getFactory().createInstance<IComponent>(classes[0].ID());controller=module.getFactory().createInstance<IEditController>(classes[1].ID());
        check(component && controller,"components instantiate");
        check(component->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");
        processor=FUnknownPtr<IAudioProcessor>(component);check(bool(processor),"audio interface");
        handler=owned(new Handler);controller->setComponentHandler(handler.get());
        check(controller->getParameterCount()==int(g::parameterCount),"actual registry complete");
        SpeakerArrangement inputs[]={SpeakerArr::kStereo,scChannels==2?SpeakerArr::kStereo:scChannels==1?SpeakerArr::kMono:SpeakerArr::kEmpty},output=SpeakerArr::kStereo;
        check(processor->setBusArrangements(inputs,2,&output,1)==kResultOk,"stereo optional absent SC negotiated");
        check(component->activateBus(kAudio,kInput,1,scChannels>0)==kResultOk,"actual optional SC bus activation");
        ProcessSetup setup{kRealtime,kSample64,256,rate};check(processor->setupProcessing(setup)==kResultOk,"setup f64");
        component->setActive(true);processor->setProcessing(true);
    }
    ~Pair(){processor->setProcessing(false);component->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();}
    void restore(const just::SoundState& state) {
        MemoryStream stream;check(just::writeSoundState(&stream,state,g::registry),"encode whole targets");stream.seek(0,IBStream::kIBSeekSet,nullptr);
        check(component->setState(&stream)==kResultOk,"processor accepts whole state");stream.seek(0,IBStream::kIBSeekSet,nullptr);
        check(controller->setComponentState(&stream)==kResultOk,"controller accepts hidden targets");
        ProcessData zero{};zero.symbolicSampleSize=kSample64;check(processor->process(zero)==kResultOk,"zero-sample state publication");
    }
    just::SoundState state() {
        MemoryStream stream;check(component->getState(&stream)==kResultOk,"get actual processor state");stream.seek(0,IBStream::kIBSeekSet,nullptr);just::SoundState s;
        check(just::readSoundState(&stream,just::pluginIdentities[6].processor,g::registry,s),"decode actual processor state");return s;
    }
    void render(const std::array<double,256>& left,const std::array<double,256>& right,std::array<double,256>& a,std::array<double,256>& b,IParameterChanges* events=nullptr,int count=256,
                const std::array<double,256>* sideL=nullptr,const std::array<double,256>* sideR=nullptr,std::uint64_t scSilence=0) {
        double* in[]={const_cast<double*>(left.data()),const_cast<double*>(right.data())};double* out[]={a.data(),b.data()};AudioBusBuffers inputs[2]{},output{};
        inputs[0].numChannels=output.numChannels=2;inputs[0].channelBuffers64=in;output.channelBuffers64=out;
        double* side[]={sideL?const_cast<double*>(sideL->data()):nullptr,sideR?const_cast<double*>(sideR->data()):nullptr};
        inputs[1].numChannels=scChannels;inputs[1].channelBuffers64=side;inputs[1].silenceFlags=scSilence;
        ProcessData data{};data.numSamples=count;data.symbolicSampleSize=kSample64;data.numInputs=(sideL || sideR || scSilence)?2:1;data.numOutputs=1;
        data.inputs=inputs;data.outputs=&output;data.inputParameterChanges=events;
        check(processor->process(data)==kResultOk,"actual plugin render");
    }
};
NSButton* button(NSView* parent,NSString* title) {
    NSString* action=[title isEqualToString:@"Advanced"]?@"changeView:":[title isEqualToString:@"Bypass"]?@"changeBypass:":[title isEqualToString:@"Preset"]?@"showPresets:":nil;
    for(NSView* child in parent.subviews){if([child isKindOfClass:NSButton.class] && ((action && [(NSButton*)child action] && [NSStringFromSelector([(NSButton*)child action]) isEqualToString:action]) || [[(NSButton*)child title] isEqualToString:title]))return (NSButton*)child;if(auto nested=button(child,title))return nested;}return nil;
}
NSView* rotary(NSView* parent,NSInteger id) {
    NSString* key=[NSString stringWithFormat:@"gate.param.%ld",(long)id];
    for(NSView* child in parent.subviews){if([child.accessibilityIdentifier isEqualToString:key])return child;if(auto nested=rotary(child,id))return nested;}return nil;
}
NSTextField* valueField(NSView* parent,NSInteger id) {
    for(NSView* child in rotary(parent,id).subviews)if([child isKindOfClass:NSTextField.class] && [(NSTextField*)child delegate])return (NSTextField*)child;return nil;
}
unsigned sliders(NSView* parent){unsigned n=[parent isKindOfClass:NSSlider.class]?1:0;for(NSView* child in parent.subviews)n+=sliders(child);return n;}
NSControl* control(NSView* parent,NSInteger tag,Class kind) {
    for(NSView* child in parent.subviews){if([child isKindOfClass:kind] && child.tag==tag)return (NSControl*)child;if(auto nested=control(child,tag,kind))return nested;}return nil;
}
void refresh(Pair& pair,NSView* root) {
    // A timer refresh without synthetic parameter writes.
    auto* advanced=button(root,@"Advanced");check(advanced!=nil,"Advanced shell button");
    [advanced performClick:nil];[advanced performClick:nil];
    check(pair.handler->writes==0,"view refresh writes no sound parameter");
}
void screenshot(NSView* root,const std::filesystem::path& filename) {
    [root layoutSubtreeIfNeeded];auto* bitmap=[root bitmapImageRepForCachingDisplayInRect:root.bounds];[root cacheDisplayInRect:root.bounds toBitmapImageRep:bitmap];
    auto* bytes=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([bytes writeToFile:[NSString stringWithUTF8String:filename.string().c_str()] atomically:YES],"native screenshot captured");
}
void set(just::SoundState& s,ParamID id,double value){s.targets[g::registry.index(id)]=g::spec(id).toNormalized(value);}
void tests(VST3::Hosting::Module& module,const std::filesystem::path& capture) {
    Pair a(module),b(module);auto defaults=just::initialState(just::pluginIdentities[6].processor,g::registry);a.restore(defaults);b.restore(defaults);
    NSView* root=[[NSView alloc] initWithFrame:NSMakeRect(0,0,720,420)];
    auto view=owned(a.controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"native attach");
    ViewRect size;view->getSize(&size);root.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());
    check(rotary(root,110) && rotary(root,111) && rotary(root,121) && rotary(root,123) && sliders(root)==0,"four shared mapped Simple controls including Attack; no private NSSlider");
    if(!capture.empty()){std::filesystem::create_directories(capture);screenshot(root,capture/"simple.png");}
    ViewRect minimum{0,0,480,260};check(view->checkSizeConstraint(&minimum)==kResultTrue && minimum.getWidth()==720 && minimum.getHeight()==420,"small host request clamps to readable module minimum");check(view->onSize(&minimum)==kResultOk,"minimum editor size accepted");root.frame=NSMakeRect(0,0,minimum.getWidth(),minimum.getHeight());[root layoutSubtreeIfNeeded];
    for(auto id:g::simpleIDs){auto* dial=rotary(root,id);check(NSContainsRect(root.bounds,[dial convertRect:dial.bounds toView:root]),"minimum size keeps dial inside editor");}
    if(!capture.empty())screenshot(root,capture/"simple-minimum.png");
    ViewRect normal{0,0,720,420};check(view->onSize(&normal)==kResultOk,"normal size restored");root.frame=NSMakeRect(0,0,720,420);
    auto custom=defaults;set(custom,g::mode,0);set(custom,g::threshold,-30);set(custom,g::range,40);set(custom,g::hold,200);set(custom,g::hysteresis,6);set(custom,g::release,120);set(custom,g::ratio,11);set(custom,g::knee,13);set(custom,g::scHPHz,678);
    a.restore(custom);b.restore(custom);refresh(a,root);
    auto* advanced=button(root,@"Advanced");[advanced performClick:nil];
    check(!valueField(root,112).enabled && valueField(root,122).enabled,"Gate disables saved Ratio and enables Hold");
    check(!valueField(root,134).enabled && !valueField(root,150).enabled,"Off HP and unavailable Lookahead stay disabled");
    if(!capture.empty())screenshot(root,capture/"advanced.png");
    [advanced performClick:nil];
    std::array<double,256> left{},right{},outA{},outAR{},outB{},outBR{};
    for(unsigned block=0;block<250;++block) {
        for(unsigned i=0;i<256;++i){const double amplitude=block<20?.2:1e-7;left[i]=amplitude*std::sin((block*256+i)*.2);right[i]=-left[i];}
        if(block%5==0){[advanced performClick:nil];[advanced performClick:nil];}
        if(block==25 || block==30 || block==100) {
            view->removed();view.reset();view=owned(a.controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"reopen mid Hold / release");advanced=button(root,@"Advanced");
        }
        a.render(left,right,outA,outAR);b.render(left,right,outB,outBR);
        check(outA==outB && outAR==outBR,"view switches / closes leave identical audio including Hold");
    }
    check(a.handler->writes==0 && a.handler->starts==0,"UI lifecycle never starts a sound gesture");
    auto saved=a.state();check(saved.targets==custom.targets,"all hidden targets unchanged after UI lifecycle");
    MemoryStream prefs;check(a.controller->getState(&prefs)==kResultOk,"view preferences save");
    [advanced performClick:nil];view->removed();view.reset();
    prefs.seek(0,IBStream::kIBSeekSet,nullptr);check(a.controller->setState(&prefs)==kResultOk,"preferences restore");
    check(a.state().targets==custom.targets,"preferences cannot overwrite sound");
    view=owned(a.controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"reopen after preferences");
    // Headless delegate checks exercise the real loaded shared control binding.
    // They are not OS mouse/keyboard acceptance; that remains serial QA.
    auto* number=valueField(root,110);check(number && number.delegate,"shared threshold value editor exists");
    [number.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:number]];
    number.stringValue=@"-35 dBFS";
    [number.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:number]];
    check(a.handler->starts==1 && a.handler->writes==1 && a.handler->ends==1 && a.handler->last==110,"shared numeric delegate sends one balanced actual host gesture");
    near(a.controller->getParamNormalized(110),55./90.,1e-12,"Simple physical mapping writes true parameter");
    const auto preciseThreshold=g::spec(g::threshold).toNormalized(-35.123456789);
    a.controller->setParamNormalized(g::threshold,preciseThreshold);
    const auto unchangedWrites=a.handler->writes;
    [number.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:number]];
    check(std::abs(number.doubleValue+35.123456789)<1e-11,"shared numeric editor exposes exact unrounded physical target");
    [number.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:number]];
    check(a.handler->writes==unchangedWrites && a.controller->getParamNormalized(g::threshold)==preciseThreshold,"unchanged focused text cannot quantize Gate target");
    auto* attack=valueField(root,121);check(attack && attack.delegate,"Attack is editable in Simple");
    [attack.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:attack]];attack.stringValue=@"5.123456789 ms";
    [attack.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:attack]];
    near(g::spec(g::attack).toPhysical(a.controller->getParamNormalized(121)),5.123456789,1e-11,"Simple Attack edits exact existing log-mapped target without display quantization");
    check(a.handler->starts==2 && a.handler->ends==2 && a.handler->last==121,"Attack host gesture is balanced");
    view->removed();view.reset();
    // Independent sample-curve oracle: input trim from 0 dB to +24 dB and
    // back, 3 ms physical ramp; Bypass switches at sample 64 with 5 ms fade.
    Pair curve(module);auto unit=defaults;set(unit,g::range,0);curve.restore(unit);
    for(auto& x:left)x=.1;for(auto& x:right)x=-.1;
    ParameterChanges events;int32 queue=0,point=0;auto* trim=events.addParameterData(140,queue);trim->addPoint(31,1,point);trim->addPoint(79,.5,point);
    auto* bypass=events.addParameterData(0,queue);bypass->addPoint(64,1,point);
    auto* mode=events.addParameterData(100,queue);mode->addPoint(47,1,point);
    curve.render(left,right,outA,outAR,&events,128);
    double effective=0,previousTarget=0,increment=0;unsigned remaining=0;
    for(unsigned sample=0;sample<128;++sample) {
        double norm=sample<=31?.5+(sample+1)*.5/32:sample<=79?1-(sample-31)*.5/48:.5;
        const double target=-24+48*norm;
        if(target!=previousTarget){previousTarget=target;increment=(target-effective)/144;remaining=144;}
        if(remaining){effective+=increment;if(!--remaining)effective=target;}
        const double blend=sample<64?0:(sample-64+1)/240.;
        const double expected=.1*(blend+(1-blend)*g::linear(effective));
        near(outA[sample],expected,1e-13,"all queue points and discrete sample offsets match independent oracle");near(outAR[sample],-expected,1e-13,"linked stereo curve oracle");
    }
    saved=curve.state();near(g::target(saved,0),1,0,"automated bypass target saved");near(g::target(saved,100),2,0,"automated Duck target saved");near(g::target(saved,140),0,0,"last trim point saved");
    MemoryStream original;check(just::writeSoundState(&original,saved,g::registry),"saved full state encode");
    original.seek(0,IBStream::kIBSeekSet,nullptr);check(curve.component->setState(&original)==kResultOk,"state restores before audio");check(curve.state().targets==saved.targets,"immediate pending full state visible");
    check(curve.processor->getLatencySamples()==0 && curve.processor->getTailSamples()==kNoTail,"actual PDC / tail default zero");
    Pair pending(module),reference(module);auto delayed=defaults;set(delayed,g::lookahead,8.5);pending.restore(delayed);reference.restore(defaults);
    near(g::target(pending.state(),g::lookahead),8.5,1e-12,"nonzero Lookahead target survives immediate full-state restore");
    ParameterChanges lookaheadEvents;auto* q=lookaheadEvents.addParameterData(150,queue);q->addPoint(13,.23,point);q->addPoint(129,.65,point);
    for(unsigned block=0;block<40;++block){
        for(unsigned i=0;i<256;++i){left[i]=1e-4*std::sin((block*256+i)*.03);right[i]=-left[i];}
        pending.render(left,right,outA,outAR,block==4?&lookaheadEvents:nullptr);reference.render(left,right,outB,outBR);
        check(outA==outB && outAR==outBR,"saved / automated Lookahead target adds no delay or effect at Maximum 0");
        check(pending.processor->getLatencySamples()==0,"nonzero saved and automated Lookahead never changes PDC");
    }
    near(g::target(pending.state(),g::lookahead),6.5,1e-12,"host automated Lookahead target retained rather than clamped back to effective 0");
    std::array<double,256> sideL{},sideR{};left.fill(.2);right.fill(-.1);sideL.fill(.1);sideR.fill(-.1);
    for(unsigned channels:{1u,2u})for(unsigned mode=0;mode<3;++mode){
        Pair actualSC(module,channels);auto scState=defaults;set(scState,g::mode,mode);set(scState,g::scSource,1);set(scState,g::detector,0);set(scState,g::threshold,-40);set(scState,g::release,5);set(scState,g::hold,0);actualSC.restore(scState);
        actualSC.render(left,right,outA,outAR);check(outA==left && outAR==right,"loaded VST3 distinguishes missing External SC");
        for(unsigned block=0;block<80;++block)actualSC.render(left,right,outA,outAR,nullptr,256,&sideL,channels==2?&sideR:nullptr);
        const double active=mode==2?g::linear(-24):1.;near(outA.back(),.2*active,1e-12,"loaded mono/stereo External SC active behavior");near(outAR.back(),-.1*active,1e-12,"loaded SC antiphase / mono detector links identical main gain");
        for(unsigned block=0;block<80;++block)actualSC.render(left,right,outA,outAR,nullptr,256,nullptr,nullptr,(1ull<<channels)-1);
        const double silent=mode==2?1.:g::linear(-24);near(outA.back(),.2*silent,1e-12,"loaded connected silent External SC follows mode");
        for(unsigned block=0;block<4;++block)actualSC.render(left,right,outA,outAR);
        check(outA==left && outAR==right,"loaded missing SC returns smoothly to exact unity");
    }
    Pair callback(module,2);auto busy=defaults;set(busy,g::scSource,1);set(busy,g::scHPEnabled,1);set(busy,g::scLPEnabled,1);callback.restore(busy);
    std::vector<double> timings;timings.reserve(1500);
    for(unsigned i=0;i<1600;++i){auto start=std::chrono::steady_clock::now();callback.render(left,right,outA,outAR,nullptr,128,&sideL,&sideR);
        if(i>=100)timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
    std::sort(timings.begin(),timings.end());const double p99=timings[1485];
    std::cout<<"MEASURE loaded VST3 48kHz/128/stereo external SC+HP+LP/RMS: P99="<<p99<<" us, "<<100*p99/(128./48000*1e6)<<"% block; GUI closed (measurement, not DAW certification)\n";
}
int main(int argc,char** argv) {@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    const char* path=argc>1?argv[1]:JUST_GATE_BUNDLE_PATH;std::string error;auto module=VST3::Hosting::Module::create(path,error);check(bool(module),error.c_str());
    std::filesystem::path capture=argc>2?argv[2]:"";tests(*module,capture);
    std::cout<<"PASS Gate loaded VST3/state/automation/native UI checks="<<checks<<"; 100+ view switches; reopen inside Hold/release; shared numeric binding; headless delegate checks, no OS-input acceptance\n";
}}
