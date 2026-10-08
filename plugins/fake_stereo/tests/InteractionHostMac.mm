#import <Cocoa/Cocoa.h>
#include "plugins/fake_stereo/EditorModel.hpp"
#include "common/vst3/Module.hpp"
#include <fstream>
#include <string>
#include <iomanip>
// Manual OS-input fixture. This feeds known signals through the real module DSP,
// with no audio device, DAW, installed plug-in or simulated mouse/keyboard events.
@interface JWInteractionHost : NSObject <NSWindowDelegate> {
@public
    just::EditorViewState view;
    just::SoundState sound;
    just::SampleFrame samples;
    std::unique_ptr<just::Engine> engine;
    std::unique_ptr<just::EditorContent> content;
    NSWindow* window;NSView* parent;NSTimer* timer;
    NSButton *advanced,*finish;
    just::ParamID active;unsigned starts,writes,ends;
    std::uint64_t position;std::ofstream events;std::string directory;
}
- (void)tick:(NSTimer*)sender;
- (void)save;
@end
@implementation JWInteractionHost
- (instancetype)init {
    self=[super init];if(!self)return nil;active=~just::ParamID(0);view.width=1120;view.height=740;view.language=just::UiLanguage::english;
    sound=just::initialState({},just::stereo::registry);engine.reset(just::moduleDefinition().createEngine());engine->prepare({48000,512,2,2});return self;
}
- (void)save {
    std::ofstream state(directory+"/wider-input-state.json");state<<"{\"starts\":"<<starts<<",\"writes\":"<<writes<<",\"ends\":"<<ends<<",\"active\":"<<(active==~just::ParamID(0)?-1:int(active))<<",\"physical\":{";
    bool comma=false;for(const auto& p:just::stereo::parameters){if(comma)state<<',';comma=true;state<<'"'<<p.id<<"\":"<<std::setprecision(17)<<p.toPhysical(sound.targets[just::stereo::registry.index(p.id)]);}state<<"}}\n";
}
- (void)tick:(NSTimer*)sender {
    (void)sender;engine->applyTargets(sound,0);std::array<double,512> l{},r{},ol{},orr{};
    for(unsigned i=0;i<512;++i){l[i]=.28*std::sin((position+i)*2*M_PI*220/48000);r[i]=.19*std::sin((position+i)*2*M_PI*330/48000);}
    just::AudioBlock<double> b;b.samples=512;b.inputChannels=b.outputChannels=2;b.inputs={l.data(),r.data()};b.outputs={ol.data(),orr.data()};engine->process(b,{});
    samples.count=512;samples.header={1,1,position/512+1,position,position+512,0,48000,2,2,0,just::analysisPlaying|just::analysisTransportKnown,just::analysisNow()};
    for(unsigned i=0;i<512;++i){samples.samples[0][i]=l[i];samples.samples[1][i]=r[i];samples.samples[2][i]=ol[i];samples.samples[3][i]=orr[i];}position+=512;
    content->refresh(view,{});[self save];
}
- (void)toggleAdvanced:(NSButton*)b {view.advanced=b.state==NSControlStateValueOn;content->refresh(view,{});}
- (void)finish:(id)sender {(void)sender;[window close];}
- (void)windowWillClose:(NSNotification*)n {
    (void)n;[timer invalidate];timer=nil;content.reset();[self save];events.flush();[NSApp terminate:nil];
}
@end
int main(int argc,char** argv){@autoreleasepool {
    if(argc!=2){std::fprintf(stderr,"usage: JustWiderInteraction evidence-directory\n");return 2;}
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    JWInteractionHost* host=[JWInteractionHost new];host->directory=argv[1];host->events.open(host->directory+"/wider-input-events.jsonl");
    just::EditorServices s;s.owner=(__bridge void*)host;s.view=&host->view;
    s.readTarget=[](void* p,just::ParamID id){auto* h=(__bridge JWInteractionHost*)p;return h->sound.targets[just::stereo::registry.index(id)];};
    s.beginEdit=[](void* p,just::ParamID id){auto* h=(__bridge JWInteractionHost*)p;if(h->active!=~just::ParamID(0))return false;h->active=id;++h->starts;h->events<<"{\"event\":\"begin\",\"id\":"<<id<<"}\n";h->events.flush();return true;};
    s.performEdit=[](void* p,just::ParamID id,double n){auto* h=(__bridge JWInteractionHost*)p;if(h->active!=id)return false;h->sound.targets[just::stereo::registry.index(id)]=n;++h->writes;h->events<<"{\"event\":\"write\",\"id\":"<<id<<",\"normalized\":"<<std::setprecision(17)<<n<<"}\n";h->events.flush();return true;};
    s.endEdit=[](void* p,just::ParamID id){auto* h=(__bridge JWInteractionHost*)p;++h->ends;h->events<<"{\"event\":\"end\",\"id\":"<<id<<"}\n";h->active=~just::ParamID(0);h->events.flush();};
    s.readBusLayout=[](void*,just::BusLayoutSnapshot& bus){bus.validFields=just::layoutBuses|just::layoutSampleRate;bus.inputChannels=bus.outputChannels=2;bus.sampleRate=48000;bus.active=true;return true;};
    s.readSamples=[](void* p,just::SampleFrame& frame){auto* h=(__bridge JWInteractionHost*)p;frame=h->samples;return frame.count?just::AnalysisAvailability::fresh:just::AnalysisAvailability::unavailable;};
    host->window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,1120,740) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];host->window.releasedWhenClosed=NO;host->window.delegate=host;host->window.title=@"JUST Wider — native input QA / real DSP test signal";
    host->parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1120,696)];[host->window.contentView addSubview:host->parent];
    host->advanced=[NSButton checkboxWithTitle:@"Advanced" target:host action:@selector(toggleAdvanced:)];host->advanced.frame=NSMakeRect(30,705,160,26);[host->window.contentView addSubview:host->advanced];
    host->finish=[NSButton buttonWithTitle:@"Finish QA" target:host action:@selector(finish:)];host->finish.frame=NSMakeRect(930,705,150,26);[host->window.contentView addSubview:host->finish];
    host->content.reset(just::stereo::createEditorContent());if(!host->content->attach((__bridge void*)host->parent,s))return 3;host->content->resize(1120,696);
    host->timer=[NSTimer scheduledTimerWithTimeInterval:1./30 target:host selector:@selector(tick:) userInfo:nil repeats:YES];[host tick:nil];[host->window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp run];
}}
