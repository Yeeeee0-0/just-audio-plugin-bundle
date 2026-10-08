#import <Cocoa/Cocoa.h>
#include "common/ui/NativeEditor.hpp"
#include <iostream>
#include <fstream>
static unsigned checks=0;
static const char* reportPath=nullptr;
static void check(bool ok,const char* message){++checks;if(!ok){if(reportPath)std::ofstream(reportPath)<<"FAIL "<<message<<"\n";std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
static NSView* named(NSView* parent,NSString* name){
    for(NSView* v in parent.subviews){
        if([v isKindOfClass:NSTextField.class] && [[(NSTextField*)v stringValue] isEqualToString:name])return v;
        if([v isKindOfClass:NSButton.class] && [[(NSButton*)v title] isEqualToString:name])return v;
        if(auto* found=named(v,name))return found;
    }return nil;
}
static NSBitmapImageRep* pixels(NSView* view){
    [view layoutSubtreeIfNeeded];[view displayIfNeeded];auto* result=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:result];check(result!=nil,"actual live view pixels");return result;
}
static void readable(NSView* view,bool skipCheckbox){
    check(view!=nil,"visible header control exists");auto* image=pixels(view);const double scale=double(image.pixelsWide)/view.bounds.size.width;
    unsigned dark=0;for(int y=0;y<image.pixelsHigh;++y)for(int x=skipCheckbox?int(22*scale):0;x<image.pixelsWide;++x){
        auto* c=[[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
        if(c.alphaComponent>.5 && c.redComponent<.45 && c.greenComponent<.45 && c.blueComponent<.45)++dark;
    }check(dark>=20,"header text actually paints dark against fixed light background");
}
int main(int argc,char** argv){@autoreleasepool {
    if(argc>2)reportPath=argv[2];
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
    NSAppearance* appAppearance=NSApp.appearance;
    just::EditorViewState state;double targets[]={0,.6};
    just::EditorCallbacks c;c.owner=targets;c.view=&state;c.getBypass=[](void* p){return static_cast<double*>(p)[0]>=.5;};c.setBypass=[](void* p,bool v){static_cast<double*>(p)[0]=v;};
    c.readStatus=[](void*){just::StatusSnapshot s;s.mode="Appearance acceptance fixture";return s;};
    c.services.owner=targets;c.services.view=&state;c.services.readTarget=[](void* p,just::ParamID id){return static_cast<double*>(p)[id];};
    c.services.beginEdit=[](void*,just::ParamID){return true;};c.services.performEdit=[](void* p,just::ParamID id,double v){static_cast<double*>(p)[id]=v;return true;};c.services.endEdit=[](void*,just::ParamID){};
    for(NSString* name in @[NSAppearanceNameDarkAqua,NSAppearanceNameAqua]){
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,720,420) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;
        window.title=@"JUST common appearance acceptance";window.appearance=[NSAppearance appearanceNamed:name];
        NSView* parent=window.contentView;NSAppearance* parentAppearance=parent.appearance;
        void* handle=just::createNativeEditor((__bridge void*)parent,just::pluginIdentities[0],c);check(handle!=nullptr,"own shell attaches to host view");just::resizeNativeEditor(handle,720,420);
        NSView* shell=(__bridge NSView*)handle;[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        for(unsigned attempt=0;attempt<80 && (!window.isKeyWindow || !NSApp.isActive);++attempt)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.025]];
        std::cout<<"appearance "<<name.UTF8String<<" key="<<bool(window.isKeyWindow)<<" active="<<bool(NSApp.isActive)<<" visible="<<bool(window.isVisible)<<"\n";
        check(window.isKeyWindow && NSApp.isActive,"QA window is genuinely activated and key");
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
        check([window.effectiveAppearance.name isEqualToString:name] && [parent.effectiveAppearance.name isEqualToString:name],"host retains selected light or dark appearance");
        check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"host parent and global appearance unchanged");
        check([shell.effectiveAppearance.name isEqualToString:NSAppearanceNameAqua],"owned JUST subtree uses Aqua");
        readable(named(shell,@"JUST EQ"),false);readable(named(shell,@"Bypass"),true);readable(named(shell,@"Advanced"),true);readable(named(shell,@"Preset"),false);
        if(argc>1){auto* image=pixels(shell);NSString* path=[[NSString stringWithUTF8String:argv[1]] stringByAppendingPathComponent:[name isEqualToString:NSAppearanceNameDarkAqua]?@"common-dark-host-key.png":@"common-light-host-key.png"];
            check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES],"activated appearance screenshot saved");}
        just::destroyNativeEditor(handle);check(parent.appearance==parentAppearance && NSApp.appearance==appAppearance,"destroy leaves host appearance unchanged");[window close];
    }
    std::cout<<"PASS "<<checks<<" activated light/dark host appearance and header pixel checks\n";
    if(reportPath)std::ofstream(reportPath)<<"PASS\n";
}}
