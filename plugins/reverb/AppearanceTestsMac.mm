#import <Cocoa/Cocoa.h>
#include "Parameters.hpp"
#include "ReverbEngine.hpp"
#include "common/ui/NativeEditor.hpp"
#include <fstream>
#include <iostream>
using namespace just;
using namespace just::reverb;
static const char* reportPath=nullptr;
static std::array<AnalysisWindow,600> measured{};
static unsigned windowsRead=0;
static void check(bool good,const char* label){if(!good){if(reportPath)std::ofstream(reportPath)<<"FAIL "<<label<<'\n';std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct WetPeak final:AnalysisTap {
    double peak[2]{};
    void pushSample(std::uint32_t,const EffectAnalysisSample& sample) noexcept override {
        if(sample.validFields&analysisWet)for(unsigned ch=0;ch<2;++ch)peak[ch]=std::max(peak[ch],std::abs(sample.wet[ch]));
    }
};
static void renderMeasuredInput(const SoundState& state){
    ReverbEngine engine;check(engine.prepare({48000,480,2,2,0,SampleFormat::float64,false}),"Reverb prepares for real graph");engine.applyTargets(state,0);
    std::array<double,480> left{},right{},outL{},outR{};WetPeak tap;ProcessContext context;context.analysis=&tap;
    for(unsigned n=0;n<measured.size();++n){
        const bool impulse=n%200==0 || n==550;left[0]=right[0]=impulse?.5:0;tap.peak[0]=tap.peak[1]=0;
        AudioBlock<double> block{{left.data(),right.data()},{outL.data(),outR.data()},2,2,480,impulse?0ull:3ull};
        engine.process(block,context);engine.endBlock();
        auto& window=measured[n];auto& header=window.header;
        header.session=header.epoch=1;header.sequence=n+1;header.startSample=n*480;header.endSample=(n+1)*480;
        header.sampleRate=48000;header.inputChannels=header.outputChannels=2;header.flags=analysisInputAligned|analysisPlaying|analysisTransportKnown;header.sourceNanoseconds=analysisNow();
        window.effectFields=analysisWet;window.channels[0].peak=window.channels[1].peak=impulse?.5:0;
        window.channels[4].peak=tap.peak[0];window.channels[5].peak=tap.peak[1];
    }
    check(measured[599].channels[4].peak>0,"late real wet tail reaches graph");
}
static NSView* rotary(NSView* root,NSString* name){
    for(NSView* child in root.subviews){if([child.identifier isEqualToString:@"just.common.rotary"] && [child.accessibilityLabel isEqualToString:name] && !child.hiddenOrHasHiddenAncestor)return child;if(auto* found=rotary(child,name))return found;}return nil;
}
static NSTextField* named(NSView* root,NSString* name){
    for(NSView* child in root.subviews){if([child isKindOfClass:NSTextField.class] && [[(NSTextField*)child stringValue] isEqualToString:name] && !child.hiddenOrHasHiddenAncestor)return (NSTextField*)child;if(auto* found=named(child,name))return found;}return nil;
}
static unsigned darkPixels(NSView* view){
    [view layoutSubtreeIfNeeded];[view displayIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];unsigned dark=0;
    for(int y=0;y<image.pixelsHigh;++y)for(int x=0;x<image.pixelsWide;++x){auto* c=[[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];if(c.alphaComponent>.5 && c.redComponent<.85 && c.greenComponent<.85 && c.blueComponent<.85)++dark;}return dark;
}
static void screenshot(NSView* view,NSString* path){
    [view layoutSubtreeIfNeeded];[view displayIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES],"activated screenshot saved");
}
int main(int argc,char** argv){@autoreleasepool {
    if(argc>2)reportPath=argv[2];
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
    auto* appAppearance=NSApp.appearance;SoundState sound=initialState({},registry);renderMeasuredInput(sound);EditorViewState state;state.language=UiLanguage::english;
    EditorCallbacks callbacks;callbacks.owner=&sound;callbacks.view=&state;
    callbacks.getBypass=[](void* owner){return static_cast<SoundState*>(owner)->targets[registry.index(bypassParamID)]>=.5;};
    callbacks.setBypass=[](void* owner,bool bypassed){static_cast<SoundState*>(owner)->targets[registry.index(bypassParamID)]=bypassed?1:0;};
    callbacks.readStatus=[](void* owner){return moduleStatus(moduleDefinition(),*static_cast<SoundState*>(owner));};
    callbacks.services.owner=&sound;callbacks.services.view=&state;
    callbacks.services.readTarget=[](void* owner,ParamID id){return static_cast<SoundState*>(owner)->targets[registry.index(id)];};
    callbacks.services.beginEdit=[](void*,ParamID){return true;};
    callbacks.services.performEdit=[](void* owner,ParamID id,double normalized){static_cast<SoundState*>(owner)->targets[registry.index(id)]=normalized;return true;};
    callbacks.services.endEdit=[](void*,ParamID){};
    callbacks.services.readAnalysis=[](void*,AnalysisCursor& cursor,AnalysisBatch& batch){
        batch.count=0;for(unsigned i=0;i<batch.windows.size() && cursor.sequence<measured.size();++i){batch.windows[i]=measured[cursor.sequence++];++batch.count;}
        windowsRead+=batch.count;
        return AnalysisAvailability::fresh;
    };
    for(NSString* appearance in @[NSAppearanceNameDarkAqua,NSAppearanceNameAqua]){
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,1120,584) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.title=@"JUST Reverb v0.3 serial appearance QA";
        window.appearance=[NSAppearance appearanceNamed:appearance];auto* parent=window.contentView;auto* parentAppearance=parent.appearance;
        windowsRead=0;state.advanced=false;void* handle=createNativeEditor((__bridge void*)parent,pluginIdentities[1],callbacks);check(handle,"real Reverb shell attaches");resizeNativeEditor(handle,1120,584);auto* shell=(__bridge NSView*)handle;
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        for(unsigned i=0;i<80 && (!window.isKeyWindow || !NSApp.isActive);++i)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.025]];
        check(window.isKeyWindow && NSApp.isActive && window.isVisible,"real Reverb window activated");
        check([parent.effectiveAppearance.name isEqualToString:appearance] && [shell.effectiveAppearance.name isEqualToString:NSAppearanceNameAqua],"host appearance preserved and Reverb shell Aqua");
        check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"no host or global appearance mutation");
        for(unsigned i=0;i<5;++i)[shell performSelector:@selector(refresh:) withObject:nil];
        check(windowsRead>=measured.size() && validAnalysisHeader(measured.back().header),"actual Reverb wet history reaches native editor");
        for(NSString* name in @[@"Brightness",@"Character",@"Distance",@"Space",@"Decay Rate",@"Stereo Width",@"Mix"]){auto* knob=rotary(shell,name);check(knob && knob.bounds.size.width>=64 && knob.bounds.size.height>=64,"seven shared primary hit areas");}
        check(darkPixels(named(shell,@"Mix"))>=5 && darkPixels(named(shell,@"Space"))>=5,"Reverb labels readable in both host appearances");
        if(argc>1){NSString* folder=[NSString stringWithUTF8String:argv[1]];NSString* name=[appearance isEqualToString:NSAppearanceNameDarkAqua]?@"reverb-dark-key-1120.png":@"reverb-light-key-1120.png";screenshot(shell,[folder stringByAppendingPathComponent:name]);}
        auto before=sound.targets;state.advanced=true;[shell performSelector:@selector(refresh:) withObject:nil];check(sound.targets==before,"Advanced is pure view");
        state.advanced=false;resizeNativeEditor(handle,880,485);[shell performSelector:@selector(refresh:) withObject:nil];
        for(NSString* name in @[@"Brightness",@"Character",@"Distance",@"Space",@"Decay Rate",@"Stereo Width",@"Mix"]){auto* knob=rotary(shell,name);check(knob && knob.bounds.size.width>=64 && knob.bounds.size.height>=64 && NSMaxY(knob.frame)<=knob.superview.bounds.size.height,"compact seven-control hit areas fully visible");}
        if(argc>1){NSString* folder=[NSString stringWithUTF8String:argv[1]];NSString* name=[appearance isEqualToString:NSAppearanceNameDarkAqua]?@"reverb-dark-key-880.png":@"reverb-light-key-880.png";screenshot(shell,[folder stringByAppendingPathComponent:name]);}
        check(sound.targets==before,"resize leaves sound unchanged");destroyNativeEditor(handle);check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"detach preserves host appearance");[window close];
    }
    if(reportPath)std::ofstream(reportPath)<<"PASS\n";std::cout<<"PASS Reverb active dark/light visual fixture, seven shared hit areas, pure view and resize; this fixture does not certify physical input\n";
}}
