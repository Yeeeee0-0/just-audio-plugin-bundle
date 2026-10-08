#import <Cocoa/Cocoa.h>
#include "common/ui/ObjCNames.hpp"
#include "common/runtime/Analysis.hpp"
#include "plugins/fake_stereo/FieldModel.hpp"
#if JUST_FEEDBACK_INDEX==7
#include "../Parameters.hpp"
#endif
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>
#include <array>
#include <vector>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <objc/runtime.h>
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0,knownFreshnessFailures=0;static bool requireFreshness=false,activated=false;static const char* reportPath=nullptr;
static void check(bool condition,const char* label){++checks;if(!condition){if(reportPath)std::ofstream(reportPath)<<"FAIL "<<label<<"\n";std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
static void pump(double seconds=.003){[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];}
static NSButton* button(NSView* root,NSString* name){
    NSString* action=[name isEqualToString:@"Advanced"]?@"changeView:":[name isEqualToString:@"Bypass"]?@"changeBypass:":[name isEqualToString:@"Preset"]?@"showPresets:":nil;
    for(NSView* v in root.subviews){if([v isKindOfClass:NSButton.class] && ((action && [(NSButton*)v action]==NSSelectorFromString(action)) || [[(NSButton*)v title] isEqualToString:name]))return (NSButton*)v;if(auto* b=button(v,name))return b;}return nil;
}
static NSView* feedback(NSView* root){if([root.identifier isEqualToString:[NSString stringWithUTF8String:just::analysisViewIdentifier]])return root;for(NSView* v in root.subviews){if([v.accessibilityIdentifier isEqualToString:@"just.wider.measured-field"])return v;if(auto* f=feedback(v))return f;}return nil;}
static void update(NSView* root){NSView* f=feedback(root);check(f!=nil,"real shared native feedback view exists");[f.superview performSelector:@selector(updateAnalysis)];[root layoutSubtreeIfNeeded];}
static NSView* textView(NSView* root,NSString* name){for(NSView* v in root.subviews){if([v isKindOfClass:NSTextField.class] && [[(NSTextField*)v stringValue] isEqualToString:name])return v;if(auto* found=textView(v,name))return found;}return nil;}
static void readable(NSView* view,bool checkbox){check(view!=nil,"actual visible header control");[view displayIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];unsigned dark=0;double scale=double(image.pixelsWide)/view.bounds.size.width;
    for(int y=0;y<image.pixelsHigh;++y)for(int x=checkbox?int(22*scale):0;x<image.pixelsWide;++x){auto* c=[[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];if(c.alphaComponent>.5 && c.redComponent<.45 && c.greenComponent<.45 && c.blueComponent<.45)++dark;}check(dark>=20,"actual key-window header has readable dark text");}
static unsigned largeControls(NSView* root){unsigned n=0;for(NSView* v in root.subviews){if(!v.isHiddenOrHasHiddenAncestor && (([v.accessibilityIdentifier hasPrefix:@"stereo.fader."] && v.bounds.size.height>=60) || ([v.identifier isEqualToString:[NSString stringWithUTF8String:just::rotaryViewIdentifier]] && ![v.accessibilityIdentifier hasPrefix:@"stereo.fader."]))) {check(v.bounds.size.width>=70 && v.bounds.size.height>=84,"actual continuous control has at least 84px hit geometry");++n;}n+=largeControls(v);}return n;}
static std::vector<std::uint8_t> state(IComponent* c){MemoryStream s;check(c->getState(&s)==kResultOk,"read complete state");auto* p=reinterpret_cast<const std::uint8_t*>(s.getData());return {p,p+s.getSize()};}
static void screenshot(NSView* root,const std::string& path){update(root);auto* image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];[root cacheDisplayInRect:root.bounds toBitmapImageRep:image];auto* data=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([data writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"save measured native screenshot");}
class Handler final:public FObject,public IComponentHandler {
public:unsigned writes=0;
    tresult PLUGIN_API beginEdit(ParamID) override{return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue) override{++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject) DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
// A host connection proxy observes the real binary transport and forwards it unchanged.
// It never fabricates a module frame or accesses an engine from the editor.
class Probe final:public FObject,public IConnectionPoint {
public:
    just::SampleFrame latest{};
    IPtr<IConnectionPoint> destination;std::array<std::vector<double>,4> log;
    std::uint64_t base=0,epoch=0;unsigned envelopes=0,frames=0,stopped=0,silent=0,bypassed=0;
    std::uint64_t lastEnd=0,dropped=0;double maxReduction=0;bool capture=true,holdAnalysis=false,dropSamples=false;
    std::vector<IPtr<IMessage>> held;
    explicit Probe(IConnectionPoint* target):destination(target){for(auto& channel:log)channel.resize(400000);held.reserve(128);}
    tresult PLUGIN_API connect(IConnectionPoint*) override{return kResultOk;}
    tresult PLUGIN_API disconnect(IConnectionPoint*) override{return kResultOk;}
    tresult PLUGIN_API notify(IMessage* m) override {
        if(capture && m && m->getMessageID() && !std::strcmp(m->getMessageID(),"JUST.Analysis.v2")){
            const void* bytes=nullptr;uint32 size=0;auto* a=m->getAttributes();
            check(a && a->getBinary("Data",bytes,size)==kResultOk && size==sizeof(just::AnalysisMessage),"real v2 wire frame size");
            just::AnalysisMessage item;std::memcpy(&item,bytes,sizeof(item));check(item.valid(),"real module sends a finite valid frame");
            const auto& h=item.kind==just::AnalysisKind::envelope?item.window.header:item.sampleFrame.header;
            check(h.sampleRate==48000 && h.inputChannels==2 && h.outputChannels==2 && (h.flags&just::analysisInputAligned),"actual stereo rate and PDC pairing");
            check(h.latencySamples==(JUST_FEEDBACK_INDEX==9?32u:0u),"actual fixed module latency");
            if(epoch!=h.epoch){epoch=h.epoch;lastEnd=0;}dropped=std::max(dropped,h.dropped);
            check(base+h.endSample<log[0].size(),"bounded source log");
            if(h.flags&just::analysisTransportKnown && !(h.flags&just::analysisPlaying))++stopped;
            if(h.flags&just::analysisBypassed)++bypassed;
            if(item.kind==just::AnalysisKind::samples){latest=item.sampleFrame;++frames;
                for(unsigned c=0;c<4;++c)for(unsigned i=0;i<item.sampleFrame.count;++i){auto at=base+h.startSample+i-(c<2?h.latencySamples:0);check(item.sampleFrame.samples[c][i]==float(log[c][at]),"captured synchronous raw IO equals real audio, including in-place and PDC");}
            }else {++envelopes;const auto& w=item.window;
                if(w.channels[0].peak==0 && w.channels[1].peak==0)++silent;
                for(unsigned c=0;c<4;++c){double peak=0,power=0;for(std::uint64_t i=h.startSample;i<h.endSample;++i){double x=log[c][base+i-(c<2?h.latencySamples:0)];peak=std::max(peak,std::abs(x));power+=x*x;}check(std::abs(peak-w.channels[c].peak)<1e-8 && std::abs(std::sqrt(power/(h.endSample-h.startSample))-w.channels[c].rms)<1e-8,"10ms envelope includes every real sample on same time and scale");}
                if(JUST_FEEDBACK_INDEX==4 || JUST_FEEDBACK_INDEX==6){check(w.effectFields&just::analysisReduction,"effect-owned actual reduction tap present");maxReduction=std::max(maxReduction,w.reductionDb);}
                if(JUST_FEEDBACK_INDEX==6)check(w.effectFields&just::analysisGate,"actual Gate state tap present");
                if(JUST_FEEDBACK_INDEX==3){check(w.effectFields&just::analysisModulation,"actual Tremolo gain tap present");for(unsigned c=0;c<2;++c){double lo=1e9,hi=-1e9;std::uint64_t observed=0;for(auto i=h.startSample;i<h.endSample;++i){double x=log[c][base+i];if(std::abs(x)<1e-10)continue;++observed;double gain=log[c+2][base+i]/x;lo=std::min(lo,gain);hi=std::max(hi,gain);}if(observed==h.endSample-h.startSample)check(std::abs(lo-w.modulationMin[c])<1e-5 && std::abs(hi-w.modulationMax[c])<1e-5,"measured Tremolo extrema equal gains actually applied to audio");else if(observed)check(lo>=w.modulationMin[c]-1e-5 && hi<=w.modulationMax[c]+1e-5,"observable Tremolo gains lie inside actual interval when silence crosses a window");}}
                if(JUST_FEEDBACK_INDEX==7)check((w.effectFields&just::analysisDelay) && w.delayMs[0]>=.05 && w.delayMs[0]<=12 && w.delayMs[1]>=.05 && w.delayMs[1]<=12,"actual bounded Flanger modulation delays");
                if(JUST_FEEDBACK_INDEX==8 && (w.correlationValid&2)){check(w.correlation[1]>=-1 && w.correlation[1]<=1,"actual correlation bounds");}
                lastEnd=h.endSample;
            }
            if(holdAnalysis){if(held.size()<128)held.emplace_back(m);return kResultOk;}
            if(dropSamples && item.kind==just::AnalysisKind::samples)return kResultOk;
        }
        return destination->notify(m);
    }
    OBJ_METHODS(Probe,FObject) DEFINE_INTERFACES DEF_INTERFACE(IConnectionPoint) END_DEFINE_INTERFACES(FObject) REFCOUNT_METHODS(FObject)
};
static void point(ParameterChanges& events,IEditController* controller,ParamID id,double physical){int32 index=0,p=0;double n=controller->plainParamToNormalized(id,physical);check(controller->setParamNormalized(id,n)==kResultOk,"configure actual target");check(events.addParameterData(id,index)->addPoint(0,n,p)==kResultOk,"host automation point");}
template<class S> static void run(const VST3::Hosting::Module::Ptr& module,HostApplication& host,int format,bool inplace,const std::string& path){
    auto classes=module->getFactory().classInfos();auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());auto reference=module->getFactory().createInstance<IComponent>(classes[0].ID());auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
    check(component && reference && controller,"actual factory instances");check(component->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize real twins/controller");
    auto handler=owned(new Handler);controller->setComponentHandler(handler.get());FUnknownPtr<IAudioProcessor> processor(component),twin(reference);FUnknownPtr<IConnectionPoint> pConnection(component),cConnection(controller);auto probe=owned(new Probe(cConnection));
    check(pConnection->connect(probe)==kResultOk && cConnection->connect(pConnection)==kResultOk,"connected host runtime proxy");
    MemoryStream saved;check(component->getState(&saved)==kResultOk,"save initial sound");saved.seek(0,IBStream::kIBSeekSet,nullptr);check(reference->setState(&saved)==kResultOk,"matched complete sound/seed");saved.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&saved)==kResultOk,"restore controller targets");
    ProcessSetup setup{kRealtime,format,64,48000};check(processor->setupProcessing(setup)==kResultOk && twin->setupProcessing(setup)==kResultOk,"prepare real sample format");check(component->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"activate real processors");processor->setProcessing(true);twin->setProcessing(true);
    std::array<S,64> left{},right{},tLeft{},tRight{},ol{},orr{},rl{},rr{};S* inputs[]={left.data(),right.data()};S* refInputs[]={tLeft.data(),tRight.data()};S* outputs[]={inplace?left.data():ol.data(),inplace?right.data():orr.data()};S* refOutputs[]={inplace?tLeft.data():rl.data(),inplace?tRight.data():rr.data()};AudioBusBuffers in{},out{},refIn{},refOut{};in.numChannels=out.numChannels=refIn.numChannels=refOut.numChannels=2;
    if constexpr(std::is_same_v<S,double>){in.channelBuffers64=inputs;refIn.channelBuffers64=refInputs;out.channelBuffers64=outputs;refOut.channelBuffers64=refOutputs;}else{in.channelBuffers32=inputs;refIn.channelBuffers32=refInputs;out.channelBuffers32=outputs;refOut.channelBuffers32=refOutputs;}
    ProcessContext context{};context.state=ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kPlaying;context.tempo=137;context.sampleRate=48000;ProcessData data{};data.numSamples=64;data.symbolicSampleSize=format;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;auto refData=data;refData.inputs=&refIn;refData.outputs=&refOut;
    ParameterChanges configuration;
    if(JUST_FEEDBACK_INDEX==4){point(configuration,controller,100,-18);point(configuration,controller,101,4);point(configuration,controller,105,0);}
    if(JUST_FEEDBACK_INDEX==6){point(configuration,controller,100,0);point(configuration,controller,110,-24);}
    if(JUST_FEEDBACK_INDEX==3){point(configuration,controller,0x1100,80);point(configuration,controller,0x1302,90);point(configuration,controller,0x1201,1);}
    #if JUST_FEEDBACK_INDEX==7
    point(configuration,controller,just::flanger::syncID,1);
    #endif
    if(JUST_FEEDBACK_INDEX==9){point(configuration,controller,4096,1);point(configuration,controller,4128,std::getenv("JUST_QA_DRIVE")?std::atof(std::getenv("JUST_QA_DRIVE")):18);}
    std::uint64_t position=0;
    auto render=[&](bool silent=false,IParameterChanges* events=nullptr){for(unsigned i=0;i<64;++i){const auto n=position+i;auto noise=[](std::uint64_t n,unsigned salt){std::uint32_t x=std::uint32_t(n)^salt;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (double(x)/4294967295.-.5)*.8;};bool dual=std::getenv("JUST_QA_SIGNAL") && std::string(std::getenv("JUST_QA_SIGNAL"))=="dual-mono";bool anti=std::getenv("JUST_QA_SIGNAL") && std::string(std::getenv("JUST_QA_SIGNAL"))=="anti-phase";double source=.4*std::sin(2*3.141592653589793*220.*double(n)/48000.);if(JUST_FEEDBACK_INDEX==8)source=noise(n,0x12345678);left[i]=tLeft[i]=silent?0:S(source);right[i]=tRight[i]=silent?0:S(JUST_FEEDBACK_INDEX==8?(anti?-source:dual?source:noise(n,0x87654321)):source);probe->log[0][position+i]=left[i];probe->log[1][position+i]=right[i];}in.silenceFlags=refIn.silenceFlags=silent?3:0;data.inputParameterChanges=refData.inputParameterChanges=events;check(processor->process(data)==kResultOk && twin->process(refData)==kResultOk,"render real twins with analysis enabled");for(unsigned i=0;i<64;++i){check(outputs[0][i]==refOutputs[0][i] && outputs[1][i]==refOutputs[1][i],"analysis and views preserve exact audio");probe->log[2][position+i]=outputs[0][i];probe->log[3][position+i]=outputs[1][i];}position+=64;context.projectTimeSamples+=64;context.projectTimeMusic+=64*137./(60*48000);};
    render(false,&configuration);probe->base=position;
    NSWindow* window=nil;NSAppearance* applicationAppearance=NSApp.appearance;
    if(activated){window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,720,420) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.title=@"JUST module feedback acceptance";}
    NSView* root=window?window.contentView:[[NSView alloc] initWithFrame:NSMakeRect(0,0,720,420)];auto view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"attach actual native module editor");ViewRect size;view->getSize(&size);root.frame=NSMakeRect(0,0,size.getWidth(),size.getHeight());if(window)[window setContentSize:root.bounds.size];
    for(unsigned b=0;b<960;++b){render();if(b%4==0)pump(.001);}
    pump(.03);update(root);check(probe->envelopes>50 && probe->frames>10,"true history and raw frames arrive through loaded module bridge");check([(NSNumber*)[feedback(root) valueForKey:@"availability"] unsignedIntValue]==unsigned(just::AnalysisAvailability::fresh),"actual native main feedback becomes fresh");
    if(!path.empty() && format==kSample64)screenshot(root,path+"/measured-simple.png");
    check(largeControls(root)>=(JUST_FEEDBACK_INDEX==8?2:JUST_FEEDBACK_INDEX==4 || JUST_FEEDBACK_INDEX==7?4:3),"actual Simple large continuous controls present");
    if(activated && format==kSample64){
        for(NSString* appearance in @[NSAppearanceNameDarkAqua,NSAppearanceNameAqua]){
            window.appearance=[NSAppearance appearanceNamed:appearance];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
            for(unsigned i=0;i<80 && (!window.isKeyWindow || !NSApp.isActive);++i)pump(.025);
            check(window.isKeyWindow && NSApp.isActive,"module QA host is genuinely key and active");check([window.effectiveAppearance.name isEqualToString:appearance] && [root.effectiveAppearance.name isEqualToString:appearance] && NSApp.appearance==applicationAppearance,"module leaves host and global appearance unchanged");check([feedback(root).effectiveAppearance.name isEqualToString:NSAppearanceNameAqua],"actual owned module feedback remains Aqua in dark host");
            readable(button(root,@"Bypass"),true);readable(button(root,@"Advanced"),true);
            NSString* name=JUST_FEEDBACK_INDEX==4?@"JUST Compressor":JUST_FEEDBACK_INDEX==6?@"JUST Gate":JUST_FEEDBACK_INDEX==3?@"JUST Tremolo":JUST_FEEDBACK_INDEX==7?@"JUST Flanger":JUST_FEEDBACK_INDEX==8?@"JUST Wider":@"JUST Distortion";readable(textView(root,name),false);
            check(button(root,@"Preset") && !button(root,@"Preset").enabled,"module without adopted catalog truthfully disables preset menu");
            for(unsigned b=0;b<64;++b){render();if(b%4==0)pump(.003);}pump(.02);
            std::string prefix=[appearance isEqualToString:NSAppearanceNameDarkAqua]?"activated-dark":"activated-light";screenshot(root,path+"/"+prefix+"-simple.png");
            {auto* plot=feedback(root);const auto& frame=probe->latest;Ivar measuredIvar=class_getInstanceVariable(plot.class,"measured");check(measuredIvar!=nullptr,"native plot stores real measurement");
            const auto* displayed=reinterpret_cast<const just::stereo::FieldMeasurement*>(reinterpret_cast<const char*>((__bridge void*)plot)+ivar_getOffset(measuredIvar));
            just::stereo::FieldMeasurement expected;check(expected.accept(frame),"actual host sample frame is valid");check(displayed->header.sequence==frame.header.sequence && displayed->count==frame.count,"displayed measurement is latest actual host output");
            auto geometry=just::stereo::FieldGeometry::fit(plot.bounds.size.width,plot.bounds.size.height);check(geometry.radius>=100,"real field has readable equal-axis radius");
            std::ofstream csv(path+"/"+prefix+"-raw-coordinates.csv");csv<<std::setprecision(17)<<"sample,outputL,outputR,side,mid,pixelX,pixelY\n";
            for(unsigned i=0;i<frame.count;++i){const auto a=expected.points[i],b=displayed->points[i];check(a.side==b.side && a.mid==b.mid,"every displayed point derives exactly from real final output");auto point=geometry.pixel(b,displayed->scale);check(std::hypot(point.side-geometry.centerX,point.mid-geometry.baseline)<=geometry.radius+1e-8,"measured output stays within same-scale semicircle");csv<<i<<','<<frame.samples[2][i]<<','<<frame.samples[3][i]<<','<<b.side<<','<<b.mid<<','<<point.side<<','<<point.mid<<'\n';}
            check(displayed->peakLR[0]==expected.peakLR[0] && displayed->peakLR[1]==expected.peakLR[1] && displayed->peakMS[0]==expected.peakMS[0] && displayed->peakMS[1]==expected.peakMS[1],"LR/MS output meters share the actual frame");
            screenshot(plot,path+"/"+prefix+"-isolated-plot.png");}


            [button(root,@"Advanced") performClick:nil];pump(.02);screenshot(root,path+"/"+prefix+"-advanced.png");[button(root,@"Advanced") performClick:nil];
            check(handler->writes==0 && state(component)==state(reference),"appearance and native view changes preserve complete sound/seed");
            std::cout<<"activated module="<<JUST_FEEDBACK_INDEX<<" appearance="<<appearance.UTF8String<<" key="<<bool(window.isKeyWindow)<<" active="<<bool(NSApp.isActive)<<"\n";
        }
    }

    auto before=state(component);for(unsigned b=0;b<20;++b){[button(root,@"Advanced") performClick:nil];check(handler->writes==0 && state(component)==before,"measured views switch without sound writes");render();before=state(component);pump(.003);}check(state(component)==state(reference),"complete state/seed matches after measured view switching");[button(root,@"Advanced") performClick:nil];
    if(!path.empty() && format==kSample64)screenshot(root,path+"/measured-advanced.png");
    context.state&=~ProcessContext::kPlaying;for(unsigned b=0;b<40;++b){render();pump(.001);}pump(.02);check(probe->stopped>0,"actual host-stopped flag reaches measured feedback");
    for(unsigned b=0;b<80;++b){render(true);pump(.001);}pump(.02);check(probe->silent>0,"actual silence distinguished from missing analysis");
    // Overflow produces dropped source windows rather than synthetic continuity.
    for(unsigned b=0;b<1700;++b)render();pump(.08);for(unsigned b=0;b<80;++b){render();pump(.001);}pump(.03);check(probe->dropped>0,"stalled UI producer queue reports lost frames");
    // With raw frames withheld, new real envelopes must not refresh old raw/FFT data.
    probe->dropSamples=true;for(unsigned b=0;b<800;++b){render();if(b%4==0)pump(.001);}pump(.03);update(root);
    bool separateStream=JUST_FEEDBACK_INDEX==7 || JUST_FEEDBACK_INDEX==8 || JUST_FEEDBACK_INDEX==9;
    bool independent=[(NSNumber*)[feedback(root) valueForKey:@"availability"] unsignedIntValue]==unsigned(separateStream?just::AnalysisAvailability::stale:just::AnalysisAvailability::fresh);
    if(!independent){++knownFreshnessFailures;std::cout<<"REPRODUCED missing raw-stream independent freshness for plugin "<<JUST_FEEDBACK_INDEX<<"\n";}if(requireFreshness)check(independent,"new envelopes cannot refresh old raw/FFT data");
    probe->dropSamples=false;for(unsigned b=0;b<64;++b){render();pump(.001);}pump(.03);update(root);
    // Hold genuine already-produced wire messages until after audio has stopped.
    probe->holdAnalysis=true;for(unsigned b=0;b<80;++b){render();pump(.001);}pump(.03);check(!probe->held.empty(),"hold genuine produced messages in host delivery queue");pump(.65);
    probe->holdAnalysis=false;for(auto& held:probe->held)probe->destination->notify(held);probe->held.clear();update(root);
    bool stale=[(NSNumber*)[feedback(root) valueForKey:@"availability"] unsignedIntValue]==unsigned(just::AnalysisAvailability::stale);
    if(!stale){++knownFreshnessFailures;std::cout<<"REPRODUCED old backlog renews native freshness for plugin "<<JUST_FEEDBACK_INDEX<<"\n";}if(requireFreshness)check(stale,"old produced messages cannot revive freshness after stopping");
    for(unsigned b=0;b<64;++b){render();pump(.001);}pump(.03);
    ParameterChanges bypass;point(bypass,controller,0,1);render(false,&bypass);for(unsigned b=0;b<40;++b){render();pump(.001);}pump(.02);check(probe->bypassed>0,"actual bypass flag preserved");
    view->removed();view.reset();if(window)[window close];probe->capture=false;processor->setProcessing(false);twin->setProcessing(false);component->setActive(false);reference->setActive(false);pConnection->disconnect(probe);cConnection->disconnect(pConnection);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();reference->terminate();
    std::cout<<"PASS feedback plugin="<<JUST_FEEDBACK_INDEX<<" format="<<format<<" inplace="<<inplace<<" windows="<<probe->envelopes<<" raw="<<probe->frames<<" dropped="<<probe->dropped<<" maxGR="<<probe->maxReduction<<"\n";
}
int main(int argc,char** argv){@autoreleasepool{check(argc>=2,"usage <bundle> [evidence-dir]");if(argc>6)setenv("JUST_QA_SIGNAL",argv[6],1);[NSApplication sharedApplication];activated=argc>4 && !std::strcmp(argv[4],"--activated");reportPath=argc>5?argv[5]:nullptr;[NSApp setActivationPolicy:activated?NSApplicationActivationPolicyRegular:NSApplicationActivationPolicyProhibited];if(activated)[NSApp finishLaunching];std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());HostApplication host;std::string path=argc>2?argv[2]:"";requireFreshness=argc>3 && !std::strcmp(argv[3],"--require-freshness");run<double>(module,host,kSample64,false,path);run<float>(module,host,kSample32,true,path);std::cout<<"PASS loaded real measured feedback "<<checks<<" checks; independent stream/backlog freshness failures="<<knownFreshnessFailures<<"; strict="<<requireFreshness<<"\n";if(reportPath)std::ofstream(reportPath)<<"PASS\n";}}
