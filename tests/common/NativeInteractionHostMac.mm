// Interactive QA fixture. All pointer/keyboard input must enter through the OS.
// This test emits no NSEvent or CGEvent and never calls mouse/key delegate methods.
#import <Cocoa/Cocoa.h>
#include "common/ui/Controls.hpp"
#include <fstream>
#include <iomanip>
#include <vector>
struct QAState {
    just::EditorViewState view;double gain=.501234567891234,frequency=.62;
    unsigned begins=0,writes=0,ends=0,depth=0,maxDepth=0,refreshes=0;
    std::ofstream events;std::string path;
    void log(const char* kind,just::ParamID id,double value){events<<std::setprecision(17)<<"{\"event\":\""<<kind<<"\",\"id\":"<<id<<",\"value\":"<<value<<",\"depth\":"<<depth<<"}\n";events.flush();}
};
@interface QADelegate : NSObject <NSApplicationDelegate> {
@public
    QAState state,peer;NSWindow* window;NSTextField* status;NSTimer* timer;
    std::unique_ptr<just::RotaryControl> gain,frequency,compact,hero,vertical,horizontal,independent;
}
@end
@implementation QADelegate
- (void)applicationDidFinishLaunching:(NSNotification*)note {
    window=[[NSWindow alloc] initWithContentRect:NSMakeRect(200,160,940,530) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];window.title=@"JUST common v0.3 — native interaction QA";window.releasedWhenClosed=NO;window.backgroundColor=[NSColor colorWithWhite:.97 alpha:1];window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
    state.events.open(state.path+"/native-input-events.jsonl");peer.events.open(state.path+"/native-peer-events.jsonl");
    just::EditorServices s;s.owner=&state;s.view=&state.view;s.readTarget=[](void* p,just::ParamID id){auto& q=*static_cast<QAState*>(p);return id==1?q.gain:q.frequency;};
    s.beginEdit=[](void* p,just::ParamID id){auto& q=*static_cast<QAState*>(p);++q.begins;++q.depth;q.maxDepth=std::max(q.maxDepth,q.depth);q.log("begin",id,id==1?q.gain:q.frequency);return true;};
    s.performEdit=[](void* p,just::ParamID id,double v){auto& q=*static_cast<QAState*>(p);++q.writes;(id==1?q.gain:q.frequency)=v;q.log("write",id,v);return true;};
    s.endEdit=[](void* p,just::ParamID id){auto& q=*static_cast<QAState*>(p);++q.ends;--q.depth;q.log("end",id,id==1?q.gain:q.frequency);};
    static const just::ParameterSpec g={"qa.gain",1,"Gain","dB",-60,12,0,just::Mapping::linear,0,true,"all",just::Transition::continuous,0};
    static const just::ParameterSpec f={"qa.frequency",2,"Frequency","Hz",20,20000,1000,just::Mapping::logarithmic,0,true,"all",just::Transition::continuous,0};
    gain=just::RotaryControl::create((__bridge void*)window.contentView,s,g);gain->resize(35,180,112,148);
    frequency=just::RotaryControl::create((__bridge void*)window.contentView,s,f);frequency->resize(200,180,112,148);
    just::DisplayPolicy dark;dark.dark=true;compact=just::RotaryControl::create((__bridge void*)window.contentView,s,g,dark);compact->resize(375,200,74,102);
    hero=just::RotaryControl::create((__bridge void*)window.contentView,s,g);hero->resize(495,120,186,218);
    just::DisplayPolicy vert;vert.style=just::ControlStyle::vertical;vertical=just::RotaryControl::create((__bridge void*)window.contentView,s,g,vert);vertical->resize(730,130,100,300);
    just::DisplayPolicy horiz;horiz.style=just::ControlStyle::horizontal;horizontal=just::RotaryControl::create((__bridge void*)window.contentView,s,g,horiz);horizontal->resize(35,110,470,38);
    auto isolated=s;isolated.owner=&peer;isolated.view=nullptr;independent=just::RotaryControl::create((__bridge void*)window.contentView,just::RotaryBinding{&peer,nullptr,[](void* p){return static_cast<QAState*>(p)->gain;},[](void* p){auto& q=*static_cast<QAState*>(p);++q.begins;++q.depth;q.log("begin",1,q.gain);return true;},[](void* p,double v){auto& q=*static_cast<QAState*>(p);q.gain=std::clamp(v,0.,.65);++q.writes;q.log("write",1,q.gain);return true;},[](void* p){auto& q=*static_cast<QAState*>(p);++q.ends;--q.depth;q.log("end",1,q.gain);}},g);independent->resize(550,340,112,148);
    status=[NSTextField wrappingLabelWithString:@""];status.frame=NSMakeRect(25,25,690,70);status.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];[window.contentView addSubview:status];
    NSTextField* help=[NSTextField wrappingLabelWithString:@"Native OS input only · Drag upward / Shift during drag / click exact value\nEnter commits · Escape cancels · unchanged blur emits zero writes · timer runs 30 Hz"];
    help.frame=NSMakeRect(25,455,490,60);[window.contentView addSubview:help];
    timer=[NSTimer scheduledTimerWithTimeInterval:1./30 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
- (void)tick:(id)sender {
    ++state.refreshes;gain->refresh();frequency->refresh();compact->refresh();hero->refresh();vertical->refresh();horizontal->refresh();independent->refresh();
    status.stringValue=[NSString stringWithFormat:@"gain=%.17g  frequency=%.17g\nbegin=%u write=%u end=%u depth=%u maxDepth=%u refresh=%u",state.gain,state.frequency,state.begins,state.writes,state.ends,state.depth,state.maxDepth,state.refreshes];
    std::ofstream snapshot(state.path+"/native-input-state.json");snapshot<<std::setprecision(17)<<"{\"gain\":"<<state.gain<<",\"frequency\":"<<state.frequency<<",\"begins\":"<<state.begins<<",\"writes\":"<<state.writes<<",\"ends\":"<<state.ends<<",\"depth\":"<<state.depth<<",\"maxDepth\":"<<state.maxDepth<<",\"refreshes\":"<<state.refreshes<<"}\n";
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {return YES;}
- (void)applicationWillTerminate:(NSNotification*)notification {[timer invalidate];gain.reset();frequency.reset();compact.reset();hero.reset();vertical.reset();horizontal.reset();independent.reset();}
@end
int main(int argc,char**argv){@autoreleasepool{[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];QADelegate* d=[QADelegate new];d->state.path=argc>1?argv[1]:"/tmp";NSApp.delegate=d;[NSApp run];}}
