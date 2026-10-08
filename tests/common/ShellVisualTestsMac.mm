// Offscreen layout/resource checks only. Not native input acceptance.
#import <Cocoa/Cocoa.h>
#include "common/ui/NativeEditor.hpp"
#include "common/ui/VisualAssets.hpp"
#include <fstream>
#include <iostream>
@protocol JustShellPresentation
- (void)showAbout:(id)sender;
- (void)showSettings:(id)sender;
@end
static void check(bool b,const char* what){if(!b){std::cerr<<"FAIL "<<what<<"\n";std::exit(1);}}
static void collect(NSView* view,NSMutableArray* images){for(NSView* child in view.subviews){if([child isKindOfClass:NSImageView.class])[images addObject:child];collect(child,images);}}
static void save(NSView* shell,NSString* path){[shell layoutSubtreeIfNeeded];NSBitmapImageRep* pixels=[shell bitmapImageRepForCachingDisplayInRect:shell.bounds];[shell cacheDisplayInRect:shell.bounds toBitmapImageRep:pixels];check([[pixels representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES],"save offscreen layout");}
int main(int argc,char**argv){@autoreleasepool{
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    just::EditorViewState v;v.width=1120;v.height=584;double targets[]={0,.6};just::EditorCallbacks c;c.owner=targets;c.view=&v;
    c.getBypass=[](void* p){return static_cast<double*>(p)[0]>=.5;};c.setBypass=[](void* p,bool b){static_cast<double*>(p)[0]=b;};c.readStatus=[](void*){just::StatusSnapshot s;s.mode="OFFSCREEN TEST: no audio connected";return s;};
    c.services.owner=targets;c.services.view=&v;c.services.readTarget=[](void* p,just::ParamID i){return static_cast<double*>(p)[i];};c.services.beginEdit=[](void*,just::ParamID){return true;};c.services.performEdit=[](void* p,just::ParamID i,double x){static_cast<double*>(p)[i]=x;return true;};c.services.endEdit=[](void*,just::ParamID){};
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1120,584)];void* h=just::createNativeEditor((__bridge void*)parent,just::pluginIdentities[0],c);auto shell=(__bridge NSView*)h;just::resizeNativeEditor(h,1120,584);
    NSMutableArray* icons=[NSMutableArray array];collect(shell,icons);check(icons.count==1 && [(NSImageView*)icons[0] image]!=nil,"approved asset decoded for header");auto first=[(NSImageView*)icons[0] image];
    NSString* folder=argc>1?[NSString stringWithUTF8String:argv[1]]:@"/tmp";save(shell,[folder stringByAppendingPathComponent:@"shell-offscreen.png"]);
    // Invoke presentation of a modal to inspect layout; never count as OS input.
    [(id<JustShellPresentation>)shell showAbout:nil];[icons removeAllObjects];collect(shell,icons);check(icons.count==2 && [(NSImageView*)icons[1] image]==first,"header/About share the exact same decoded icon");save(shell,[folder stringByAppendingPathComponent:@"about-offscreen.png"]);
    [(id<JustShellPresentation>)shell showSettings:nil];save(shell,[folder stringByAppendingPathComponent:@"settings-offscreen.png"]);
    just::destroyNativeEditor(h);std::cout<<"PASS offscreen shell geometry, embedded approved image, same header/About source, settings render; not native input acceptance\n";
}}
