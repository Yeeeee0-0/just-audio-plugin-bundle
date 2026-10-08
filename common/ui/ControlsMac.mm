#import <Cocoa/Cocoa.h>
#include "Controls.hpp"
#include "ObjCNames.hpp"
#define JustRotaryView JUST_OBJC_CLASS(RotaryView)
#define JustValueField JUST_OBJC_CLASS(ValueField)
@class JustRotaryView;
@interface JustValueField : NSTextField
@property(nonatomic,weak) JustRotaryView* rotary;
@end
@interface JustRotaryView : NSView <NSTextFieldDelegate> {
@public
    just::EditorServices services;just::ParameterSpec spec;just::DisplayPolicy policy;
    just::TextEditSession edit;just::RotaryDrag drag;
    NSTextField *label,*unit;JustValueField* value;
    double normalized;BOOL enabled,dragging,typing,cancelling;
}
- (void)prepareText;
- (void)refreshValue;
- (void)finishGesture;
@end
@implementation JustValueField
- (BOOL)becomeFirstResponder {[self.rotary prepareText];BOOL ok=[super becomeFirstResponder];if(!ok && !self.currentEditor){self.rotary->typing=NO;[self.rotary refreshValue];}return ok;}
- (void)mouseDown:(NSEvent*)event {[self.rotary prepareText];[super mouseDown:event];}
@end
static NSColor* rotaryColor(unsigned rgb){return [NSColor colorWithRed:((rgb>>16)&255)/255. green:((rgb>>8)&255)/255. blue:(rgb&255)/255. alpha:1];}
@implementation JustRotaryView
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return enabled;}
- (void)layout {
    [super layout];CGFloat w=self.bounds.size.width;auto g=just::ControlGeometry::layout(w,self.bounds.size.height,policy);
    label.frame=NSMakeRect(0,0,w,just::ControlGeometry::labelFieldHeight(policy));CGFloat unitWidth=unit.stringValue.length?30:0;
    CGFloat valueY=policy.style==just::ControlStyle::rotary?g.valueTop:self.bounds.size.height-24;
    value.frame=NSMakeRect(0,valueY,w-unitWidth,g.valueHeight);unit.frame=NSMakeRect(w-unitWidth+3,valueY+g.valueHeight-18,unitWidth,18);
    if(policy.style==just::ControlStyle::horizontal){
        label.frame=NSMakeRect(0,(self.bounds.size.height-20)/2,76,20);
        value.frame=NSMakeRect(w-105,(self.bounds.size.height-22)/2,75,22);
        unit.frame=NSMakeRect(w-27,(self.bounds.size.height-16)/2,27,16);
    }
    value.font=[NSFont monospacedDigitSystemFontOfSize:just::ControlGeometry::valuePointSize(g.diameter,policy) weight:NSFontWeightSemibold];
}
- (void)drawRect:(NSRect)dirty {
    if(policy.style!=just::ControlStyle::rotary){
        BOOL horizontal=policy.style==just::ControlStyle::horizontal;CGFloat w=self.bounds.size.width,h=self.bounds.size.height;
        NSPoint a=horizontal?NSMakePoint(88,h/2):NSMakePoint(w/2,h-36),b=horizontal?NSMakePoint(w-120,h/2):NSMakePoint(w/2,30);
        NSPoint thumb=NSMakePoint(a.x+(b.x-a.x)*normalized,a.y+(b.y-a.y)*normalized);
        NSBezierPath* p=[NSBezierPath bezierPath];[p moveToPoint:a];[p lineToPoint:b];p.lineWidth=6;p.lineCapStyle=NSLineCapStyleRound;[rotaryColor(0xccdee4) setStroke];[p stroke];
        p=[NSBezierPath bezierPath];[p moveToPoint:a];[p lineToPoint:thumb];p.lineWidth=6;p.lineCapStyle=NSLineCapStyleRound;[rotaryColor(0x3ec5d0) setStroke];[p stroke];
        p=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(thumb.x-(horizontal?10:18),thumb.y-(horizontal?16:12),horizontal?20:36,horizontal?32:24) xRadius:5 yRadius:5];[rotaryColor(0xf6fbfc) setFill];[rotaryColor(0xabcbd3) setStroke];[p fill];[p stroke];
        p=[NSBezierPath bezierPath];[p moveToPoint:NSMakePoint(thumb.x-7,thumb.y)];[p lineToPoint:NSMakePoint(thumb.x+7,thumb.y)];p.lineWidth=2;[rotaryColor(0x18b6c4) setStroke];[p stroke];return;
    }
    auto g=just::ControlGeometry::layout(self.bounds.size.width,self.bounds.size.height,policy);double d=g.diameter,cx=self.bounds.size.width/2,cy=g.top+d/2;
    auto path=[NSBezierPath bezierPath];
    for(int i=0;i<=100;++i){double a=just::rotaryAngle(i/100.)*M_PI/180.;NSPoint p=NSMakePoint(cx+std::sin(a)*d*.43,cy-std::cos(a)*d*.43);if(i)[path lineToPoint:p];else[path moveToPoint:p];}
    path.lineWidth=3;path.lineCapStyle=NSLineCapStyleRound;[rotaryColor(policy.dark?0x438392:0xd3e6eb) setStroke];[path stroke];
    path=[NSBezierPath bezierPath];for(int i=0;i<=100;++i){double a=just::rotaryAngle(normalized*i/100.)*M_PI/180.;NSPoint p=NSMakePoint(cx+std::sin(a)*d*.43,cy-std::cos(a)*d*.43);if(i)[path lineToPoint:p];else[path moveToPoint:p];}
    path.lineWidth=3;path.lineCapStyle=NSLineCapStyleRound;[rotaryColor(enabled?(policy.dark?0x68e5ed:0x18b5c3):0x99adb4) setStroke];[path stroke];
    NSRect disc=NSMakeRect(cx-d*.345,cy-d*.345,d*.69,d*.69);[rotaryColor(policy.dark?0x245566:0xf6fbfc) setFill];[rotaryColor(policy.dark?0x438392:0xc1dbe3) setStroke];path=[NSBezierPath bezierPathWithOvalInRect:disc];[path fill];[path stroke];
    path=[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(disc,d*.05,d*.05)];[rotaryColor(policy.dark?0x245566:0xffffff) setFill];[path fill];
    double a=just::rotaryAngle(normalized)*M_PI/180.;path=[NSBezierPath bezierPath];[path moveToPoint:NSMakePoint(cx+std::sin(a)*d*.16,cy-std::cos(a)*d*.16)];[path lineToPoint:NSMakePoint(cx+std::sin(a)*d*.27,cy-std::cos(a)*d*.27)];path.lineWidth=3;path.lineCapStyle=NSLineCapStyleRound;[rotaryColor(policy.dark?0xadf6fc:0x008b9e) setStroke];[path stroke];
}
- (void)refreshValue {
    if(!dragging && services.readTarget){double n=services.readTarget(services.owner,spec.id);if(std::isfinite(n))normalized=std::clamp(n,0.,1.);}
    if(!typing && !value.currentEditor){char text[128];just::formatControlDisplay(spec,normalized,services.view && services.view->advanced?just::DisplayContext::advanced:just::DisplayContext::simple,policy,text,sizeof(text));value.stringValue=[NSString stringWithUTF8String:text];}
    unit.stringValue=[NSString stringWithUTF8String:just::controlDisplayUnit(spec,normalized,typing,policy)];
    label.stringValue=[NSString stringWithUTF8String:services.view && services.view->language==just::UiLanguage::chinese && policy.labelZh?policy.labelZh:spec.title];
    value.enabled=enabled;self.alphaValue=enabled?1:.45;[self layout];self.needsDisplay=YES;
}
- (void)finishGesture {if(dragging){dragging=NO;if(services.endEdit)services.endEdit(services.owner,spec.id);}}
- (void)apply:(double)n {
    n=std::clamp(n,0.,1.);if(spec.stepCount)n=std::round(n*spec.stepCount)/spec.stepCount;
    if(n!=normalized && services.performEdit && services.performEdit(services.owner,spec.id,n)){double actual=services.readTarget(services.owner,spec.id);normalized=std::isfinite(actual)?std::clamp(actual,0.,1.):n;
        if(std::abs(normalized-n)>1e-12)drag.accumulator=normalized;[self refreshValue];}
}
- (void)oneEdit:(double)n {
    if(dragging){if(enabled)[self apply:n];return;}
    if(!enabled || n==normalized || !services.beginEdit || !services.beginEdit(services.owner,spec.id))return;
    dragging=YES;[self apply:n];[self finishGesture];
}
- (void)mouseDown:(NSEvent*)event {
    if(!enabled)return;NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];auto g=just::ControlGeometry::layout(self.bounds.size.width,self.bounds.size.height,policy);
    if(policy.style==just::ControlStyle::horizontal){if(p.x<80 || p.x>self.bounds.size.width-112)return;}
    else if(p.y<20 || p.y>=(policy.style==just::ControlStyle::rotary?g.valueTop:self.bounds.size.height-24))return;
    [self finishGesture];[self.window makeFirstResponder:self];[self refreshValue];
    if(event.clickCount==2){[self oneEdit:spec.toNormalized(spec.initial)];return;}
    drag.begin(normalized,policy.style==just::ControlStyle::horizontal?-p.x:p.y);dragging=services.beginEdit && services.beginEdit(services.owner,spec.id);
}
- (void)mouseDragged:(NSEvent*)event {
    if(!dragging)return;NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];[self apply:drag.move(policy.style==just::ControlStyle::horizontal?-p.x:p.y,(event.modifierFlags&NSEventModifierFlagShift)!=0)];
}
- (void)mouseUp:(NSEvent*)event {[self finishGesture];}
- (void)rightMouseDown:(NSEvent*)event {[self oneEdit:spec.toNormalized(spec.initial)];}
- (void)scrollWheel:(NSEvent*)event {
    if(self.window.firstResponder!=self)return;
    double step=spec.stepCount?1./spec.stepCount:((event.modifierFlags&NSEventModifierFlagShift)?.001:.01);
    [self oneEdit:std::clamp(normalized+event.scrollingDeltaY*step,0.,1.)];
}
- (void)keyDown:(NSEvent*)event {
    if(event.keyCode==53){[self finishGesture];return;}
    if(event.keyCode==123 || event.keyCode==124 || event.keyCode==125 || event.keyCode==126){
        double step=spec.stepCount?1./spec.stepCount:((event.modifierFlags&NSEventModifierFlagShift)?.001:.01);
        [self oneEdit:std::clamp(normalized+((event.keyCode==124 || event.keyCode==126)?step:-step),0.,1.)];return;
    }[super keyDown:event];
}
- (BOOL)resignFirstResponder {[self finishGesture];return YES;}
- (void)windowLostFocus:(NSNotification*)notification {[self finishGesture];}
- (void)viewWillMoveToWindow:(NSWindow*)window {[[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidResignKeyNotification object:nil];if(!window)[self finishGesture];[super viewWillMoveToWindow:window];}
- (void)viewDidMoveToWindow {[super viewDidMoveToWindow];if(self.window)[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowLostFocus:) name:NSWindowDidResignKeyNotification object:self.window];}
- (void)prepareText {
    if(typing || !enabled)return;[self finishGesture];[self refreshValue];typing=YES;cancelling=NO;
    auto precise=edit.begin(spec,policy,normalized);value.stringValue=[NSString stringWithUTF8String:precise.c_str()];[self refreshValue];
}
- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor {[self prepareText];return enabled;}
- (void)controlTextDidBeginEditing:(NSNotification*)n {[self prepareText];}
- (void)controlTextDidEndEditing:(NSNotification*)n {
    if(!typing)return;double next=normalized;BOOL changed=!cancelling && edit.changed(spec,policy,value.stringValue.UTF8String,next);
    typing=NO;if(changed)[self oneEdit:next];[self refreshValue];
}
- (BOOL)control:(NSControl*)control textView:(NSTextView*)view doCommandBySelector:(SEL)command {
    if(command==@selector(cancelOperation:)){cancelling=YES;[value abortEditing];typing=NO;[self.window makeFirstResponder:self];[self refreshValue];return YES;}
    if(command==@selector(insertNewline:)){[self.window makeFirstResponder:self];return YES;}return NO;
}
@end
namespace just {
class MacRotary final:public RotaryControl {
    JustRotaryView* view;
public:
    MacRotary(void* parent,const EditorServices& services,const ParameterSpec& spec,DisplayPolicy policy) {
        view=[[JustRotaryView alloc] initWithFrame:NSMakeRect(0,0,112,148)];view->services=services;view->spec=spec;view->policy=policy;view->enabled=YES;
        view.identifier=[NSString stringWithUTF8String:rotaryViewIdentifier];view.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
        view->label=[NSTextField labelWithString:@""];view->label.alignment=NSTextAlignmentCenter;view->label.font=[NSFont systemFontOfSize:just::ControlGeometry::labelPointSize(policy) weight:NSFontWeightSemibold];
        view->value=[[JustValueField alloc] initWithFrame:NSZeroRect];view->value.rotary=view;view->value.alignment=NSTextAlignmentRight;view->value.delegate=view;view->value.bordered=NO;view->value.drawsBackground=NO;view->value.focusRingType=NSFocusRingTypeNone;
        // Keep the complete editing string on one horizontally scrollable line.
        NSCell* valueCell=view->value.cell;valueCell.usesSingleLineMode=YES;valueCell.wraps=NO;valueCell.scrollable=YES;
        view->unit=[NSTextField labelWithString:@""];view->unit.font=[NSFont systemFontOfSize:10];view->unit.textColor=rotaryColor(policy.dark?0x9ec2cd:0x7a939d);
        view->label.textColor=rotaryColor(policy.dark?0xb9dce4:0x17333c);view->value.textColor=rotaryColor(policy.dark?0xeafcff:0x17333c);
        view->value.toolTip=[NSString stringWithUTF8String:spec.unit];view->value.accessibilityLabel=[NSString stringWithFormat:@"%s value",spec.title];view.accessibilityLabel=[NSString stringWithUTF8String:spec.title];view.accessibilityRole=NSAccessibilitySliderRole;
        [view addSubview:view->label];[view addSubview:view->value];[view addSubview:view->unit];[(__bridge NSView*)parent addSubview:view];[view refreshValue];
    }
    ~MacRotary() override{[view finishGesture];[[NSNotificationCenter defaultCenter] removeObserver:view];view->value.delegate=nil;view->value.rotary=nil;[view removeFromSuperview];view=nil;}
    void resize(int x,int y,int width,int height) override{
        const auto& p=view->policy;int minimumHeight=p.style==ControlStyle::horizontal?32:80;
        if(ControlGeometry::customTypography(p))minimumHeight=std::max(minimumHeight,int(ControlGeometry::labelFieldHeight(p)+28+ControlGeometry::readoutHeight(p)));
        view.frame=NSMakeRect(x,y,std::max(width,p.style==ControlStyle::horizontal?240:54),std::max(height,minimumHeight));[view layout];
    }
    void refresh(bool enabled) override{view->enabled=enabled;if(!enabled){[view finishGesture];view->cancelling=YES;[view->value abortEditing];view->typing=NO;}[view refreshValue];}
    void* nativeHandle() const noexcept override{return (__bridge void*)view;}
};
std::unique_ptr<RotaryControl> RotaryControl::create(void* parent,const EditorServices& services,const ParameterSpec& spec,DisplayPolicy policy) {
    if(!parent || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return {};
    return std::make_unique<MacRotary>(parent,services,spec,policy);
}
}
