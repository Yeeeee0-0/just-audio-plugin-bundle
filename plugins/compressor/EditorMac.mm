#import <Cocoa/Cocoa.h>
#include "Engine.hpp"
#include "EnvelopeDisplay.hpp"
#include "common/ui/Controls.hpp"
namespace {
NSColor* muted(){return [NSColor colorWithRed:.46 green:.58 blue:.62 alpha:1];}
NSColor* cyan(){return [NSColor colorWithRed:.07 green:.72 blue:.78 alpha:1];}
void text(NSString* s,NSRect r,CGFloat size,NSColor* color,NSTextAlignment align=NSTextAlignmentLeft) {
    NSMutableParagraphStyle* p=[[NSMutableParagraphStyle alloc] init];p.alignment=align;
    [s drawInRect:r withAttributes:@{NSFontAttributeName:[NSFont monospacedDigitSystemFontOfSize:size weight:NSFontWeightRegular],NSForegroundColorAttributeName:color,NSParagraphStyleAttributeName:p}];
}
void card(NSRect r) {
    NSBezierPath* p=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r,.5,.5) xRadius:12 yRadius:12];
    [[NSColor colorWithRed:.96 green:.985 blue:.99 alpha:.86] setFill];[p fill];
    [[NSColor colorWithRed:.83 green:.90 blue:.93 alpha:1] setStroke];p.lineWidth=1;[p stroke];
}
just::DisplayPolicy policyFor(std::size_t i) {
    // All unit formatting, precision and edit events belong to common.
    static constexpr const char* labels[]={"阈值","压缩比","启动","释放","风格","检测器","峰值/RMS 混合","拐点","衰减范围","输入增益","补偿增益","混合","立体声联动","侧链来源","侧链增益","侧链高通开关","侧链高通","侧链低通开关","侧链低通","前瞻（待支持）"};
    just::DisplayPolicy p;p.labelZh=labels[i];return p;
}
NSString* translated(const just::EditorServices& s,const char* zh,const char* en) {
    return [NSString stringWithUTF8String:s.view?just::localized(*s.view,zh,en):en];
}
}
@interface JCCompressorEnvelope : NSView {
@public
    just::EditorServices services;
    just::compressor::EnvelopeDisplay display;
    std::uint64_t visualResumeGeneration;
    double threshold;
    CGFloat lastY;
    BOOL dragging;
}
- (void)updateAnalysis;
- (void)finishGesture;
- (NSUInteger)availability;
- (NSRect)plotRect;
- (NSRect)thresholdLabel;
@end
@implementation JCCompressorEnvelope
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (NSUInteger)availability{return static_cast<NSUInteger>(display.availability);}
- (NSRect)plotRect{return NSMakeRect(26,36,MAX(1,self.bounds.size.width-52),MAX(1,self.bounds.size.height-66));}
- (NSRect)thresholdLabel {
    NSRect r=[self plotRect];const double y=just::compressor::levelY(just::module::parameters[1].toPhysical(threshold),r.origin.y,r.size.height);
    return NSMakeRect(NSMinX(r),std::clamp(y-10,NSMinY(r),NSMaxY(r)-20),98,20);
}
- (void)updateAnalysis {
    if(!dragging)threshold=services.readTarget(services.owner,100);
    const auto* view=services.view;
    if(view && visualResumeGeneration!=view->visualResumeGeneration){display={};visualResumeGeneration=view->visualResumeGeneration;}
    if(!view || !view->visualsPaused)display.refresh(services);
    self.needsDisplay=YES;
}
- (void)finishGesture {if(dragging){dragging=NO;services.endEdit(services.owner,100);}}
- (void)mouseDown:(NSEvent*)e {
    NSPoint p=[self convertPoint:e.locationInWindow fromView:nil];NSRect r=[self plotRect];
    const double y=just::compressor::levelY(just::module::parameters[1].toPhysical(threshold),r.origin.y,r.size.height);
    if(p.x<NSMinX(r) || p.x>NSMaxX(r) || (std::abs(p.y-y)>12 && !NSPointInRect(p,[self thresholdLabel])))return;
    [self finishGesture];[self.window makeFirstResponder:self];threshold=services.readTarget(services.owner,100);lastY=p.y;dragging=services.beginEdit(services.owner,100);
}
- (void)mouseDragged:(NSEvent*)e {
    if(!dragging)return;NSPoint p=[self convertPoint:e.locationInWindow fromView:nil];NSRect r=[self plotRect];
    const double next=std::clamp(threshold+(lastY-p.y)*((e.modifierFlags&NSEventModifierFlagShift)?.1:1)/r.size.height,0.,1.);lastY=p.y;
    if(next!=threshold && services.performEdit(services.owner,100,next))threshold=services.readTarget(services.owner,100);
    self.needsDisplay=YES;
}
- (void)mouseUp:(NSEvent*)e {[self finishGesture];}
- (void)keyDown:(NSEvent*)e {if(e.keyCode==53){[self finishGesture];return;}[super keyDown:e];}
- (BOOL)resignFirstResponder {[self finishGesture];return YES;}
- (void)windowLostFocus:(NSNotification*)n {[self finishGesture];}
- (void)viewWillMoveToWindow:(NSWindow*)w {
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidResignKeyNotification object:nil];[self finishGesture];[super viewWillMoveToWindow:w];
}
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];if(self.window)[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowLostFocus:) name:NSWindowDidResignKeyNotification object:self.window];
}
- (void)drawRect:(NSRect)dirty {
    card(self.bounds);NSRect r=[self plotRect];text(translated(services,"输入 / 输出包络","INPUT / OUTPUT ENVELOPE"),NSMakeRect(18,12,self.bounds.size.width-36,18),11,muted());
    [[NSColor colorWithRed:.82 green:.90 blue:.93 alpha:.55] setStroke];
    for(int i=0;i<=4;++i){NSBezierPath* p=[NSBezierPath bezierPath];CGFloat y=NSMinY(r)+r.size.height*i/4;[p moveToPoint:NSMakePoint(NSMinX(r),y)];[p lineToPoint:NSMakePoint(NSMaxX(r),y)];[p stroke];}
    for(int i=0;i<=8;++i){NSBezierPath* p=[NSBezierPath bezierPath];CGFloat x=NSMinX(r)+r.size.width*i/8;[p moveToPoint:NSMakePoint(x,NSMinY(r))];[p lineToPoint:NSMakePoint(x,NSMaxY(r))];[p stroke];}
    if(display.availability==just::AnalysisAvailability::fresh && display.count) {
        const auto& newest=display.latest();const double span=newest.header.sampleRate*6.;
        for(unsigned tap=0;tap<2;++tap) {
            NSBezierPath* p=[NSBezierPath bezierPath];BOOL started=NO;std::uint64_t next=0;
            for(std::size_t i=0;i<display.count;++i) {
                const auto& w=display.at(i);const auto& h=w.header;
                if(!(h.flags&just::analysisInputAligned) || h.flags&just::analysisInvalid){started=NO;continue;}
                const double x=NSMaxX(r)-(newest.header.endSample-h.endSample)/span*r.size.width;if(x<NSMinX(r)){started=NO;continue;}
                const double peak=std::max(w.channels[tap*2].peak,w.channels[tap*2+1].peak);
                NSPoint point=NSMakePoint(x,just::compressor::levelY(20*std::log10(std::max(peak,1e-8)),NSMinY(r),r.size.height));
                if(!started || next!=h.startSample || h.flags&just::analysisGap)[p moveToPoint:point];else [p lineToPoint:point];started=YES;next=h.endSample;
            }
            [(tap?cyan():[NSColor colorWithRed:.67 green:.79 blue:.84 alpha:1]) setStroke];p.lineWidth=tap?2.5:1.5;[p stroke];
        }
    }else text(display.availability==just::AnalysisAvailability::stale?translated(services,"反馈已过期 · 暂无新音频","Feedback stale · no new audio"):translated(services,"反馈不可用","Feedback unavailable"),NSMakeRect(NSMinX(r),NSMidY(r)-10,r.size.width,20),12,muted(),NSTextAlignmentCenter);
    const double db=just::module::parameters[1].toPhysical(threshold);const CGFloat y=just::compressor::levelY(db,NSMinY(r),r.size.height);
    NSBezierPath* line=[NSBezierPath bezierPath];[line moveToPoint:NSMakePoint(NSMinX(r),y)];[line lineToPoint:NSMakePoint(NSMaxX(r),y)];CGFloat dash[]={5,5};[line setLineDash:dash count:2 phase:0];[[NSColor colorWithRed:.08 green:.59 blue:.67 alpha:1] setStroke];[line stroke];
    NSRect label=[self thresholdLabel];NSBezierPath* pill=[NSBezierPath bezierPathWithRoundedRect:label xRadius:5 yRadius:5];[[NSColor colorWithRed:.88 green:.97 blue:.98 alpha:1] setFill];[pill fill];[pill stroke];
    text([NSString stringWithFormat:@"%.1f dBFS",db],NSInsetRect(label,2,2),10,[NSColor colorWithRed:0 green:.54 blue:.62 alpha:1],NSTextAlignmentCenter);
    NSString* note=translated(services,"灰色输入 / 青色输出 · −60…0 dBFS · 6 s","Gray IN / cyan OUT · −60…0 dBFS · 6 s");
    if(display.availability==just::AnalysisAvailability::fresh && display.count){const auto f=display.latest().header.flags;if(f&just::analysisBypassed)note=[note stringByAppendingString:translated(services," · 旁路"," · Bypass")];else if(f&just::analysisTransportKnown && !(f&just::analysisPlaying))note=[note stringByAppendingString:translated(services," · 停止"," · Stopped")];}
    text(note,NSMakeRect(18,self.bounds.size.height-22,self.bounds.size.width-36,16),9,muted());
}
@end
@interface JCCompressorReduction : NSView {
@public JCCompressorEnvelope* envelope;
}
@end
@implementation JCCompressorReduction
- (BOOL)isFlipped{return YES;}
- (void)drawRect:(NSRect)dirty {
    card(self.bounds);CGFloat w=self.bounds.size.width,h=self.bounds.size.height;const auto& services=envelope->services;text(translated(services,"增益衰减","GAIN REDUCTION"),NSMakeRect(18,12,w-36,18),11,muted());
    const auto& d=envelope->display;const auto* latest=d.availability==just::AnalysisAvailability::fresh && d.count?&d.latest():nullptr;
    BOOL valid=latest && latest->effectFields&just::analysisReduction && !(latest->header.flags&just::analysisInvalid);
    const CGFloat size=h<160?25:40,numberY=h<160?33:h*.30;
    text(valid?[NSString stringWithFormat:@"%.1f",latest->reductionDb]:@"—",NSMakeRect(8,numberY,w-16,size+8),size,[NSColor colorWithRed:.07 green:.62 blue:.69 alpha:1],NSTextAlignmentCenter);
    text(valid?translated(services,"dB · 压缩级","dB · compression stage"):d.availability==just::AnalysisAvailability::stale?translated(services,"已过期","Stale"):translated(services,"不可用","Unavailable"),NSMakeRect(8,numberY+size+6,w-16,14),9,muted(),NSTextAlignmentCenter);
    {CGFloat railY=h>=180?h*.70:h-38,railHeight=h>=180?18:8,railWidth=w-36;for(unsigned i=0;i<30;++i){NSRect r=NSMakeRect(18+i*railWidth/30,railY,MAX(2,railWidth/30-3),railHeight);[(valid && i<latest->reductionDb?cyan():[NSColor colorWithRed:.84 green:.91 blue:.94 alpha:1]) setFill];[[NSBezierPath bezierPathWithRoundedRect:r xRadius:2 yRadius:2] fill];}if(h>=180){text(@"0",NSMakeRect(18,railY+23,30,14),9,muted());text(@"30+",NSMakeRect(w-50,railY+23,32,14),9,muted(),NSTextAlignmentRight);}}
    NSString* sc=translated(services,"侧链不可用","SC unavailable");if(latest && latest->effectFields&just::analysisSidechain){auto f=latest->effectFlags;sc=!(f&1)?translated(services,"内部侧链","SC Internal"):!(f&2)?translated(services,"外部侧链 · 未连接","SC External · missing"):!(f&4)?translated(services,"外部侧链 · 静音","SC External · silent"):translated(services,"外部侧链 · 活动","SC External · active");}
    text(sc,NSMakeRect(8,h-19,w-16,14),9,muted(),NSTextAlignmentCenter);
}
@end
@interface JCCompressorDocument : NSView
@end
@implementation JCCompressorDocument
- (BOOL)isFlipped{return YES;}
@end
@interface JustCompressorBody : NSView {
    just::EditorServices services;
    JCCompressorEnvelope* envelope;
    JCCompressorReduction* reduction;
    NSScrollView* scroll;
    JCCompressorDocument* document;
    NSTextField* additional;
    std::array<std::unique_ptr<just::RotaryControl>,20> controls;
    BOOL advanced;
}
- (instancetype)initWithServices:(just::EditorServices)s;
- (void)refreshWithView:(const just::EditorViewState&)view;
- (void)stop;
@end
@implementation JustCompressorBody
- (BOOL)isFlipped{return YES;}
- (instancetype)initWithServices:(just::EditorServices)s {
    self=[super initWithFrame:NSZeroRect];if(!self)return nil;services=s;
    scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.hasVerticalScroller=YES;scroll.autohidesScrollers=YES;scroll.drawsBackground=NO;
    document=[[JCCompressorDocument alloc] initWithFrame:NSZeroRect];scroll.documentView=document;[self addSubview:scroll];
    envelope=[[JCCompressorEnvelope alloc] initWithFrame:NSZeroRect];envelope->services=s;envelope->threshold=s.readTarget(s.owner,100);envelope.identifier=@"just.compressor.envelope";envelope.accessibilityLabel=@"Input/output envelope; drag threshold line";
    reduction=[[JCCompressorReduction alloc] initWithFrame:NSZeroRect];reduction->envelope=envelope;reduction.identifier=@"just.compressor.reduction";
    [document addSubview:envelope];[document addSubview:reduction];
    additional=[NSTextField labelWithString:@"ADDITIONAL CONTROLS · Actual PDC 0 samples · SC Listen unavailable"];additional.font=[NSFont systemFontOfSize:10];additional.textColor=muted();[document addSubview:additional];
    for(std::size_t i=0;i<controls.size();++i){controls[i]=just::RotaryControl::create((__bridge void*)document,s,just::module::parameters[i+1],policyFor(i));if(!controls[i])return nil;NSView* native=(__bridge NSView*)controls[i]->nativeHandle();native.accessibilityIdentifier=[NSString stringWithFormat:@"just.compressor.parameter.%u",just::module::parameters[i+1].id];if(i==19)native.toolTip=@"Saved Lookahead request retained. Actual lookahead and PDC are 0 samples; activation unavailable.";}
    [self refreshWithView:*s.view];return self;
}
- (void)stop {[envelope finishGesture];[[NSNotificationCenter defaultCenter] removeObserver:envelope];for(auto& c:controls)c.reset();}
- (void)refreshWithView:(const just::EditorViewState&)view {
    if(advanced!=view.advanced){[envelope finishGesture];for(auto& c:controls)c->refresh(false);[self.window makeFirstResponder:nil];[document scrollPoint:NSZeroPoint];}advanced=view.advanced;additional.hidden=!advanced;
    additional.stringValue=translated(services,"进阶控制 · 实际 PDC 0 样本 · 侧链监听不可用","ADDITIONAL CONTROLS · Actual PDC 0 samples · SC Listen unavailable");
    [envelope updateAnalysis];reduction.needsDisplay=YES;
    for(std::size_t i=0;i<controls.size();++i){[(__bridge NSView*)controls[i]->nativeHandle() setHidden:i>=4 && !advanced];controls[i]->refresh(i!=19 && (i<4 || advanced));}
    self.needsLayout=YES;
}
- (void)layout {
    [super layout];scroll.frame=self.bounds;CGFloat w=self.bounds.size.width-16,h=self.bounds.size.height;
    const CGFloat margin=w>=900?34:16,gap=w>=900?22:14,side=std::clamp(w*.29,196.,305.),chartHeight=std::clamp(h-172,120.,270.);
    envelope.frame=NSMakeRect(margin,16,MAX(1,w-2*margin-gap-side),chartHeight);reduction.frame=NSMakeRect(w-margin-side,16,side,chartHeight);
    CGFloat y=chartHeight+28,cell=MIN(148.,(w-32)/4),start=(w-cell*4)/2;for(unsigned i=0;i<4;++i)controls[i]->resize(start+cell*i,y,cell,132);
    y+=146;additional.frame=NSMakeRect(18,y,w-36,16);y+=24;
    const unsigned columns=MAX(2,unsigned((w-36)/132));const CGFloat advancedCell=(w-36)/columns;
    for(std::size_t i=4;i<controls.size();++i){auto n=i-4;controls[i]->resize(18+(n%columns)*advancedCell,y+(n/columns)*146,advancedCell,136);}
    document.frame=NSMakeRect(0,0,w,MAX(h,advanced?y+((16+columns-1)/columns)*146+12:chartHeight+172));
}
@end
namespace just::compressor {
namespace {
class MacEditor final:public EditorContent {
    __strong JustCompressorBody* body=nil;
public:
    ~MacEditor() override {[body stop];[body removeFromSuperview];body=nil;}
    bool attach(void* parent,const EditorServices& s) override {if(!parent || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;body=[[JustCompressorBody alloc] initWithServices:s];if(!body)return false;[(__bridge NSView*)parent addSubview:body];return true;}
    void resize(int w,int h) override {body.frame=NSMakeRect(0,0,w,h);[body layoutSubtreeIfNeeded];}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {[body refreshWithView:view];}
};
}
EditorContent* createCompressorEditor(){return new MacEditor;}
}
