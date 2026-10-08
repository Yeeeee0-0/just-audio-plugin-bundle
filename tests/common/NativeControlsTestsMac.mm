#import <Cocoa/Cocoa.h>
#include "common/ui/Controls.hpp"
#include <iostream>
static unsigned checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}}
struct Sink {
    just::EditorViewState view;double value=.412345678901;
    unsigned starts=0,ends=0,writes=0,depth=0,maxDepth=0;
    just::EditorServices services(){just::EditorServices s;s.owner=this;s.view=&view;
        s.readTarget=[](void* p,just::ParamID){return static_cast<Sink*>(p)->value;};
        s.beginEdit=[](void* p,just::ParamID){auto& s=*static_cast<Sink*>(p);++s.starts;++s.depth;s.maxDepth=std::max(s.maxDepth,s.depth);return true;};
        s.performEdit=[](void* p,just::ParamID,double v){auto& s=*static_cast<Sink*>(p);check(s.depth==1,"each perform belongs to exactly one gesture");s.value=v;++s.writes;return true;};
        s.endEdit=[](void* p,just::ParamID){auto& s=*static_cast<Sink*>(p);check(s.depth==1,"balanced end");--s.depth;++s.ends;};return s;
    }
};
static NSEvent* mouse(NSView* view,NSEventType type,double x,double y,NSEventModifierFlags flags=0){return [NSEvent mouseEventWithType:type location:[view convertPoint:NSMakePoint(x,y) toView:nil] modifierFlags:flags timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];}
static NSEvent* key(NSView* view,unsigned short code,NSEventModifierFlags flags=0){return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags timestamp:0 windowNumber:view.window.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:code];}
@interface TestWheelEvent:NSEvent
@end
@implementation TestWheelEvent
- (CGFloat)scrollingDeltaY{return 1;}
- (NSEventModifierFlags)modifierFlags{return 0;}
@end
int main(int argc,char** argv){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,360,200) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;
    Sink sink;const just::ParameterSpec spec{"fixture.gain",1,"Gain","dB",-60,12,0,just::Mapping::linear,0,true,"all",just::Transition::continuous,0};
    auto control=just::RotaryControl::create((__bridge void*)window.contentView,sink.services(),spec);check(bool(control),"create native common rotary");control->resize(12,12);NSView* view=(__bridge NSView*)control->nativeHandle();
    check(![view isKindOfClass:NSSlider.class],"dial is custom renderer, not enlarged NSSlider frame");check(view.frame.size.width==112 && view.frame.size.height==136,"logical geometry");
    NSTextField* text=nil;for(NSView* child in view.subviews)if([child isKindOfClass:NSTextField.class] && [(NSTextField*)child isEditable])text=(NSTextField*)child;check(text!=nil,"precise numeric input exists");
    const double before=sink.value;for(int i=0;i<20;++i){sink.view.advanced=i%2;control->refresh();}check(sink.value==before && !sink.starts && !sink.writes,"refresh/view formatting never quantizes target");
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,56,70)];[view mouseDragged:mouse(view,NSEventTypeLeftMouseDragged,56,60)];check(sink.depth==1,"drag opens one gesture");
    [view keyDown:key(view,126)];[view scrollWheel:[TestWheelEvent new]];check(sink.starts==1 && sink.ends==0 && sink.depth==1 && sink.maxDepth==1,"drag plus key plus wheel reuses gesture");[view mouseUp:mouse(view,NSEventTypeLeftMouseUp,56,60)];check(sink.starts==sink.ends && !sink.depth,"mouse-up ends original mixed-input gesture");
    auto previous=sink.value;[view mouseDown:mouse(view,NSEventTypeLeftMouseDown,56,70)];[view mouseDragged:mouse(view,NSEventTypeLeftMouseDragged,56,60,NSEventModifierFlagShift)];[view mouseUp:mouse(view,NSEventTypeLeftMouseUp,56,60)];check(std::abs(sink.value-previous-.005)<1e-12,"Shift fine drag");
    [window makeFirstResponder:nil];previous=sink.value;[view scrollWheel:[TestWheelEvent new]];check(sink.value==previous,"unfocused wheel cannot edit");
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,56,70)];[[NSNotificationCenter defaultCenter] postNotificationName:NSWindowDidResignKeyNotification object:window];check(!sink.depth && sink.starts==sink.ends,"window loses focus ends gesture");
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,56,70)];control->refresh(false);check(!sink.depth && sink.starts==sink.ends,"disabling ends gesture");control->refresh(true);
    [view rightMouseDown:mouse(view,NSEventTypeRightMouseDown,56,70)];check(sink.value==spec.toNormalized(spec.initial),"right click restores exact default");
    // Drive the native delegate's edit lifecycle, including precise Return and Escape.
    id<NSTextFieldDelegate> delegate=text.delegate;NSNotification* n=[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:text];
    unsigned writes=sink.writes;[delegate controlTextDidBeginEditing:n];NSString* precise=[text.stringValue copy];[delegate controlTextDidEndEditing:n];check(sink.writes==writes,"unchanged precise Return emits no edit");
    [delegate controlTextDidBeginEditing:n];text.stringValue=@"-10.123456789";[delegate controlTextDidEndEditing:n];check(std::abs(spec.toPhysical(sink.value)+10.123456789)<1e-12,"typed precise value remains full precision");
    previous=sink.value;[delegate controlTextDidBeginEditing:n];text.stringValue=@"-33.5";[(id)delegate control:text textView:[NSTextView new] doCommandBySelector:@selector(cancelOperation:)];[delegate controlTextDidEndEditing:n];check(sink.value==previous,"Escape cancels text without write");
    control->refresh();[view layoutSubtreeIfNeeded];NSBitmapImageRep* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];check(image!=nil,"actual rotary pixels rendered");
    if(argc>1){NSData* png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([png writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES],"rotary screenshot saved");}
    double scale=double(image.pixelsWide)/view.bounds.size.width;int y=int(65*scale),first=-1,last=-1;for(int x=0;x<image.pixelsWide;++x){NSColor* color=[[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];double r=color.redComponent,g=color.greenComponent,b=color.blueComponent;if(color.alphaComponent>.9 && r<.95 && std::abs(r-g)<.05 && std::abs(g-b)<.05){if(first<0)first=x;last=x;}}
    double diameter=(last-first+1)/scale;std::cout<<"paint measurement "<<diameter<<" pxWide "<<image.pixelsWide<<" scale "<<scale<<" first "<<first<<" last "<<last<<"\n";check(diameter>=80 && diameter<=86,"visible painted dial is 84 logical units");
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,56,70)];control.reset();check(!sink.depth && sink.starts==sink.ends && sink.maxDepth==1,"close during drag balances gesture");[window close];
    std::cout<<"PASS "<<checks<<" native rotary rendering/edit/lifecycle checks; measured dial "<<diameter<<" logical units\n";
}}
