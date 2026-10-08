#import <Cocoa/Cocoa.h>
#include "Editor.hpp"
#include "Presets.hpp"
#include "ReverbEngine.hpp"
#include "SimpleLayout.hpp"
#include <iostream>
using namespace just;using namespace just::reverb;
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct Mock {SoundState state=presetState(0);unsigned begins=0,writes=0,ends=0;std::array<AnalysisWindow,600> windows{};};
struct WetProbe final:AnalysisTap {
    double peak[2]{};
    void pushSample(std::uint32_t,const EffectAnalysisSample& sample) noexcept override {
        if(sample.validFields&analysisWet)for(int ch=0;ch<2;++ch)peak[ch]=std::max(peak[ch],std::abs(sample.wet[ch]));
    }
};
static void fillMeasuredTail(Mock& mock) {
    ReverbEngine engine;check(engine.prepare({48000,480,2,2,0,SampleFormat::float64,false}),"feedback engine prepares");engine.applyTargets(mock.state,0);
    std::array<double,480> input{},right{},outputL{},outputR{};WetProbe tap;ProcessContext context;context.analysis=&tap;
    for(unsigned n=0;n<mock.windows.size();++n) {
        input[0]=n%200==0?.5:0;right[0]=n%200==0?.3:0;tap.peak[0]=tap.peak[1]=0;
        engine.process(AudioBlock<double>{{input.data(),right.data()},{outputL.data(),outputR.data()},2,2,480,input[0]?0ull:3ull},context);engine.endBlock();
        auto& window=mock.windows[n];auto& header=window.header;header.session=header.epoch=1;header.sequence=n+1;
        header.startSample=n*480;header.endSample=(n+1)*480;header.sampleRate=48000;header.inputChannels=header.outputChannels=2;header.sourceNanoseconds=analysisNow();
        header.flags=analysisInputAligned|analysisPlaying|analysisTransportKnown;window.effectFields=analysisWet;
        window.channels[0].peak=input[0];window.channels[1].peak=right[0];window.channels[4].peak=tap.peak[0];window.channels[5].peak=tap.peak[1];
    }
    check(mock.windows.back().channels[4].peak>0,"real reverb tail fills UI fixture");
}
static NSView* control(NSView* view,NSString* name) {
    for(NSView* child in view.subviews) {
        if([child.identifier isEqualToString:@"just.common.rotary"] && [child.accessibilityLabel isEqualToString:name] && !child.hiddenOrHasHiddenAncestor)return child;
        if(auto* found=control(child,name))return found;
    }return nil;
}
static void screenshot(NSView* view,const std::string& path) {
    [view layoutSubtreeIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"screenshot written");
}
int main(int argc,char** argv) {@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    Mock mock;fillMeasuredTail(mock);EditorViewState view;view.language=UiLanguage::english;EditorServices services;services.owner=&mock;services.view=&view;
    services.readTarget=[](void* p,ParamID id){return static_cast<Mock*>(p)->state.targets[registry.index(id)];};
    services.beginEdit=[](void* p,ParamID){++static_cast<Mock*>(p)->begins;return true;};
    services.performEdit=[](void* p,ParamID id,double v){auto& m=*static_cast<Mock*>(p);++m.writes;m.state.targets[registry.index(id)]=v;return true;};
    services.endEdit=[](void* p,ParamID){++static_cast<Mock*>(p)->ends;};
    services.readAnalysis=[](void* p,AnalysisCursor& cursor,AnalysisBatch& batch){auto& m=*static_cast<Mock*>(p);batch.count=0;
        for(unsigned i=0;i<batch.windows.size() && cursor.sequence<m.windows.size();++i){batch.windows[i]=m.windows[cursor.sequence++];++batch.count;}return AnalysisAvailability::fresh;};
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1120,460)];parent.wantsLayer=YES;parent.layer.backgroundColor=NSColor.whiteColor.CGColor;
    std::unique_ptr<EditorContent> editor(createEditor());check(editor->attach((__bridge void*)parent,services),"shared controls attach");editor->resize(1120,460);editor->refresh(view,{});
    const auto before=mock.state.targets;
    NSArray<NSString*>* names=@[@"Brightness",@"Character",@"Distance",@"Space",@"Decay Rate",@"Stereo Width",@"Mix"];
    for(NSString* name:names)check(control(parent,name)!=nil,"approved seven simple controls exist");
    check(control(parent,@"High Cut")==nil && control(parent,@"Pre-delay")==nil,"raw High Cut and Pre-delay are Advanced only");
    check(control(parent,@"Space").frame.size.width>control(parent,@"Mix").frame.size.width*1.5,"Space is center hero");
    if(argc>1)screenshot(parent,std::string(argv[1])+"/content-simple-v03.png");
    for(int i=0;i<12;++i){view.advanced=!view.advanced;editor->refresh(view,{});check(mock.state.targets==before && mock.writes==0,"view switching never edits sound");}
    view.advanced=true;editor->refresh(view,{});check(control(parent,@"Freeze") && control(parent,@"High Cut"),"all original sound targets remain reachable");
    if(argc>1)screenshot(parent,std::string(argv[1])+"/content-advanced-v03.png");
    view.advanced=false;parent.frame=NSMakeRect(0,0,880,362);editor->resize(880,362);editor->refresh(view,{});
    for(NSString* name:names){auto* c=control(parent,name);check(c && NSMaxX(c.frame)<=880 && NSMaxY(c.frame)<=362,"compact seven controls fit");}
    if(argc>1)screenshot(parent,std::string(argv[1])+"/content-compact-v03.png");
    set(mock.state,Sync,1);const auto synced=mock.state.targets;view.advanced=true;editor->refresh(view,{});check(control(parent,@"Pre-delay").alphaValue<1,"synced free pre-delay read only");
    editor.reset();check(parent.subviews.count==0,"shared controls and module parent released");editor.reset(createEditor());check(editor->attach((__bridge void*)parent,services),"reopen");
    parent.frame=NSMakeRect(0,0,1120,460);editor->resize(1120,460);editor->refresh(view,{});check(mock.state.targets==synced && mock.writes==0 && mock.begins==0 && mock.ends==0,"reopen and layout preserve all targets without gestures");
    std::cout<<"PASS nonactivated Cocoa layout: seven shared controls, centered Space, Advanced targets, 12 view toggles, real wet fixture, compact bounds, Sync read-only and reopen; OS input QA pending\n";
}}
