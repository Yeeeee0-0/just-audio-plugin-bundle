#import <Cocoa/Cocoa.h>
#include "Parameters.hpp"
#include "DelayEngine.hpp"
#include "common/ui/NativeEditor.hpp"
#include <fstream>
#include <iostream>
using namespace just;
using namespace just::delay;
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
    DelayEngine engine;check(engine.prepare({48000,480,2,2,0,SampleFormat::float64,false}),"Delay prepares for real graph");engine.applyTargets(state,0);
    std::array<double,480> left{},right{},outL{},outR{};WetPeak tap;ProcessContext context;context.analysis=&tap;
    for(unsigned n=0;n<measured.size();++n){
        const bool impulse=n%200==0 || n==550;left[0]=right[0]=impulse?1:0;tap.peak[0]=tap.peak[1]=0;
        AudioBlock<double> block{{left.data(),right.data()},{outL.data(),outR.data()},2,2,480,impulse?0ull:3ull};
        engine.process(block,context);engine.endBlock();
        auto& window=measured[n];auto& header=window.header;
        header.session=header.epoch=1;header.sequence=n+1;header.startSample=n*480;header.endSample=(n+1)*480;
        header.sampleRate=48000;header.inputChannels=header.outputChannels=2;header.flags=analysisInputAligned|analysisPlaying|analysisTransportKnown;header.sourceNanoseconds=analysisNow();
        window.effectFields=analysisWet;window.channels[0].peak=window.channels[1].peak=impulse?1:0;
        window.channels[4].peak=tap.peak[0];window.channels[5].peak=tap.peak[1];
    }
    check(measured[25].channels[4].peak>.9 && measured[50].channels[5].peak>.05,"real left/right wet repeats reach graph");
}
static NSView* rotary(NSView* root,ParamID id){
    NSString* title=[NSString stringWithUTF8String:id==timeL?"Time":spec(id).title];
    if([root.identifier isEqualToString:@"just.common.rotary"] && [root.accessibilityLabel isEqualToString:title] && !root.hiddenOrHasHiddenAncestor)return root;
    for(NSView* child in root.subviews)if(auto* found=rotary(child,id))return found;
    return nil;
}
static NSTextField* valueField(NSView* root){
    for(NSView* child in root.subviews)if([child isKindOfClass:NSTextField.class] && ((NSTextField*)child).editable)return (NSTextField*)child;
    return nil;
}
static NSTextField* named(NSView* root,NSString* name){
    for(NSView* child in root.subviews){if([child isKindOfClass:NSTextField.class] && [[(NSTextField*)child stringValue] isEqualToString:name] && !child.hiddenOrHasHiddenAncestor)return (NSTextField*)child;if(auto* found=named(child,name))return found;}return nil;
}
static unsigned darkPixels(NSView* view){
    [view layoutSubtreeIfNeeded];[view displayIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];unsigned dark=0;
    for(int y=0;y<image.pixelsHigh;++y)for(int x=0;x<image.pixelsWide;++x){auto* c=[[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];if(c.alphaComponent>.5 && c.redComponent<.45 && c.greenComponent<.45 && c.blueComponent<.45)++dark;}return dark;
}
static void screenshot(NSView* view,NSString* path){
    [view layoutSubtreeIfNeeded];[view displayIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES],"activated screenshot saved");
}
int main(int argc,char** argv){@autoreleasepool {
    if(argc>2)reportPath=argv[2];
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
    auto* appAppearance=NSApp.appearance;SoundState sound=initialState({},registry);
    set(sound,route,2);set(sound,mix,100);set(sound,timeL,250);set(sound,timeR,250);set(sound,feedback,70);
    renderMeasuredInput(sound);EditorViewState state;state.language=UiLanguage::english;
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
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,720,480) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;window.title=@"JUST Delay active appearance QA";
        window.appearance=[NSAppearance appearanceNamed:appearance];auto* parent=window.contentView;auto* parentAppearance=parent.appearance;
        windowsRead=0;state.advanced=false;void* handle=createNativeEditor((__bridge void*)parent,pluginIdentities[2],callbacks);check(handle,"real Delay shell attaches");resizeNativeEditor(handle,720,480);auto* shell=(__bridge NSView*)handle;
        [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        for(unsigned i=0;i<80 && (!window.isKeyWindow || !NSApp.isActive);++i)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.025]];
        check(window.isKeyWindow && NSApp.isActive && window.isVisible,"real Delay window activated");
        check([parent.effectiveAppearance.name isEqualToString:appearance] && [shell.effectiveAppearance.name isEqualToString:NSAppearanceNameAqua],"host appearance preserved and Delay shell Aqua");
        check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"no host or global appearance mutation");
        for(unsigned i=0;i<5;++i)[shell performSelector:@selector(refresh:) withObject:nil];
        check(windowsRead>=measured.size() && validAnalysisHeader(measured.back().header),"actual Delay L/R wet history reaches native editor");
        for(ParamID id:{timeL,feedback,mix}){auto* knob=rotary(shell,id);check(knob && knob.bounds.size.width>=64 && knob.bounds.size.height>=64,"large primary hit area");auto* field=valueField(knob);check(field && field.bounds.size.width>=64,"primary value visible");}
        check(darkPixels(named(shell,@"Time"))>=5 && darkPixels(named(shell,@"Feedback"))>=5,"Delay labels readable in both host appearances");
        if(argc>1){NSString* folder=[NSString stringWithUTF8String:argv[1]];NSString* name=[appearance isEqualToString:NSAppearanceNameDarkAqua]?@"delay-dark-key-720.png":@"delay-light-key-720.png";screenshot(shell,[folder stringByAppendingPathComponent:name]);}
        auto before=sound.targets;state.advanced=true;[shell performSelector:@selector(refresh:) withObject:nil];check(sound.targets==before,"Advanced is pure view");
        state.advanced=false;resizeNativeEditor(handle,640,480);[shell performSelector:@selector(refresh:) withObject:nil];
        [shell layoutSubtreeIfNeeded];
        if(argc>1){NSString* folder=[NSString stringWithUTF8String:argv[1]];NSString* name=[appearance isEqualToString:NSAppearanceNameDarkAqua]?@"delay-dark-key-640.png":@"delay-light-key-640.png";screenshot(shell,[folder stringByAppendingPathComponent:name]);}
        for(ParamID id:{timeL,feedback,mix}){auto* knob=rotary(shell,id);auto* field=valueField(knob);check(knob && knob.bounds.size.width>=64 && knob.bounds.size.height>=64 && NSMaxY(knob.frame)<=knob.superview.bounds.size.height,"compact primary hit area fully visible");check(field && NSMaxY(field.frame)<=field.superview.bounds.size.height,"compact value fully visible");}
        check(sound.targets==before,"resize leaves sound unchanged");destroyNativeEditor(handle);check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"detach preserves host appearance");[window close];
    }
    if(reportPath)std::ofstream(reportPath)<<"PASS\n";std::cout<<"PASS Delay active dark/light windows, readable labels, 720/640 hit areas, pure view and resize\n";
}}
