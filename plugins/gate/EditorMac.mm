#import <Cocoa/Cocoa.h>
#include "EnvelopeModel.hpp"
#include "common/ui/Controls.hpp"
#include <vector>

namespace {
NSColor* ink(){return [NSColor colorWithRed:.09 green:.20 blue:.24 alpha:1];}
NSColor* muted(){return [NSColor colorWithRed:.43 green:.56 blue:.60 alpha:1];}
NSColor* cyan(){return [NSColor colorWithRed:.07 green:.72 blue:.78 alpha:1];}
void text(NSString* value,NSPoint point,CGFloat size,NSColor* color){
    [value drawAtPoint:point withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],NSForegroundColorAttributeName:color}];
}
NSString* actualState(const just::AnalysisWindow& w,const just::EditorServices& services){
    auto tr=[&](const char* zh,const char* en){return [NSString stringWithUTF8String:just::gate::uiText(services,zh,en)];};
    if(!(w.effectFields&just::analysisGate))return tr("门态不可用","Gate state unavailable");
    static const char* phases[]={"Closed","Opening","Open","Holding","Closing"};
    const unsigned mode=w.gateState>>8,phase=w.gateState&255;
    static const char* phasesZh[]={"关闭","打开中","已打开","保持","关闭中"};
    NSString* state=mode==1?tr("扩展","Expand"):[NSString stringWithFormat:@"%@ · %@",mode==2?tr("闪避","Duck"):tr("门控","Gate"),phase<5?tr(phasesZh[phase],phases[phase]):tr("未知","Unknown")];
    if(w.effectFlags&8)state=[state stringByAppendingFormat:@" · %@",tr("模式过渡","transitioning")];
    if(w.effectFlags&1)state=[state stringByAppendingFormat:@" · %@",(w.effectFlags&2)?((w.effectFlags&4)?tr("侧链已连接","SC connected"):tr("侧链静音","SC silent")):tr("侧链缺失","SC missing")];
    if(w.header.flags&just::analysisBypassed)state=[state stringByAppendingFormat:@" · %@",tr("旁通","Bypassed")];
    else if((w.header.flags&just::analysisTransportKnown) && !(w.header.flags&just::analysisPlaying))state=[state stringByAppendingFormat:@" · %@",tr("已停止","Stopped")];
    return state;
}
}

@interface JustGateEnvelopeView : NSView {
@public
    just::EditorServices services;
    just::gate::EnvelopeHistory history;
    std::uint64_t visualResumeGeneration;
}
- (void)updateAnalysis;
- (NSUInteger)availability;
@end
@implementation JustGateEnvelopeView
- (BOOL)isFlipped{return YES;}
- (NSUInteger)availability{return NSUInteger(history.availability);}
- (void)updateAnalysis {
    const auto* view=services.view;
    if(view && visualResumeGeneration!=view->visualResumeGeneration){history={};visualResumeGeneration=view->visualResumeGeneration;}
    if(!view || !view->visualsPaused)history.refresh(services);
    self.needsDisplay=YES;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;auto tr=[&](const char* zh,const char* en){return [NSString stringWithUTF8String:just::gate::uiText(services,zh,en)];};const CGFloat w=self.bounds.size.width,h=self.bounds.size.height;
    NSBezierPath* card=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,.5,.5) xRadius:12 yRadius:12];
    [[NSColor colorWithRed:.96 green:.985 blue:.989 alpha:.94] setFill];[card fill];
    [[NSColor colorWithRed:.84 green:.90 blue:.92 alpha:1] setStroke];[card stroke];
    text(tr("门限包络","GATE ENVELOPE"),NSMakePoint(20,10),11,muted());
    NSString* status=history.count?actualState(history.latest(),services):tr("等待真实音频","Waiting for measured audio");
    if(history.availability!=just::AnalysisAvailability::fresh)status=history.availability==just::AnalysisAvailability::stale?tr("数据已过期 · 保留最后音频","Measurements stale — last audio held"):tr("测量不可用","Measurements unavailable");
    text(status,NSMakePoint(20,h-20),10,muted());
    const CGFloat left=26,right=w-26,top=34,bottom=MAX(top+18,h*.54),gainTop=bottom+16,gainBottom=MAX(gainTop+6,h-28);
    NSBezierPath* grid=[NSBezierPath bezierPath];grid.lineWidth=1;
    for(unsigned i=0;i<=12;++i){CGFloat x=left+(right-left)*i/12;[grid moveToPoint:NSMakePoint(x,top)];[grid lineToPoint:NSMakePoint(x,gainBottom)];}
    for(unsigned i=0;i<=3;++i){CGFloat y=top+(bottom-top)*i/3;[grid moveToPoint:NSMakePoint(left,y)];[grid lineToPoint:NSMakePoint(right,y)];}
    [[NSColor colorWithRed:.80 green:.87 blue:.90 alpha:.5] setStroke];[grid stroke];
    const auto& p=just::gate::spec(just::gate::threshold);double threshold=p.toPhysical(just::gate::read(services,p.id));
    auto levelY=[&](double db){return bottom-std::clamp((db+96)/102.,0.,1.)*(bottom-top);};
    CGFloat ty=levelY(threshold);NSBezierPath* line=[NSBezierPath bezierPath];CGFloat dash[]={5,5};[line setLineDash:dash count:2 phase:0];
    [line moveToPoint:NSMakePoint(left,ty)];[line lineToPoint:NSMakePoint(right,ty)];[[NSColor colorWithRed:.48 green:.61 blue:.66 alpha:1] setStroke];[line stroke];
    text([NSString stringWithFormat:tr("阈值设置 %.1f dBFS","Threshold setting %.1f dBFS"),threshold],NSMakePoint(MAX(left,right-194),MAX(top,ty-14)),10,muted());
    text(tr("输入 / 输出峰值","Input / output peaks"),NSMakePoint(MAX(20,w-150),10),10,muted());
    if(!history.count)return;
    const auto& last=history.latest();const double span=last.header.sampleRate*2.;
    auto xAt=[&](const just::AnalysisWindow& item){return right-(double(last.header.endSample)-double(item.header.endSample))/span*(right-left);};
    [NSGraphicsContext saveGraphicsState];[[NSBezierPath bezierPathWithRect:NSMakeRect(left,top,right-left,gainBottom-top)] addClip];
    auto trace=[&](unsigned kind,NSColor* color,CGFloat thickness){
        NSBezierPath* path=[NSBezierPath bezierPath];path.lineWidth=thickness;path.lineJoinStyle=NSLineJoinStyleRound;BOOL continuous=NO;
        for(std::size_t i=0;i<history.count;++i){const auto& item=history.at(i);const double x=xAt(item);if(x<left){continuous=NO;continue;}
            bool valid=!(item.header.flags&just::analysisInvalid) && (item.header.flags&just::analysisInputAligned);
            if(kind==2)valid=valid && (item.effectFields&just::analysisReduction);
            if(!valid){continuous=NO;continue;}
            double y=kind==2?gainTop+std::clamp(item.reductionDb/90.,0.,1.)*(gainBottom-gainTop):levelY(just::gate::EnvelopeHistory::peakDb(kind==0?just::gate::EnvelopeHistory::inputPeak(item):just::gate::EnvelopeHistory::outputPeak(item)));
            NSPoint point=NSMakePoint(x,y);if(continuous && i && just::gate::EnvelopeHistory::joins(history.at(i-1),item))[path lineToPoint:point];else [path moveToPoint:point];continuous=YES;
        }
        [color setStroke];[path stroke];
    };
    const CGFloat alpha=history.availability==just::AnalysisAvailability::fresh?1:.45;
    trace(0,[NSColor colorWithRed:.69 green:.79 blue:.83 alpha:alpha],1.5);trace(1,[cyan() colorWithAlphaComponent:alpha],2.5);trace(2,[cyan() colorWithAlphaComponent:alpha*.8],2);
    [NSGraphicsContext restoreGraphicsState];
    NSString* gain=(last.effectFields&just::analysisReduction)?[NSString stringWithFormat:tr("实际增益 −%.1f dB · 2 秒测量","Applied gain −%.1f dB · measured 2 s"),last.reductionDb]:tr("实际增益不可用","Applied gain unavailable");
    text(gain,NSMakePoint(left,gainTop-14),10,muted());
}
@end

@interface JustGateFlippedView : NSView
@end
@implementation JustGateFlippedView
- (BOOL)isFlipped{return YES;}
@end
@interface JustGateContentView : NSView {
    just::EditorServices services;
    JustGateEnvelopeView* envelope;
    NSScrollView* scroll;
    NSView* controls;
    NSView* additional;
    NSMutableDictionary<NSNumber*,NSPopUpButton*>* menus;
    NSMutableDictionary<NSNumber*,NSTextField*>* menuLabels;
    std::vector<std::unique_ptr<just::RotaryControl>> primaryRotaries;
    std::vector<std::pair<just::ParamID,std::unique_ptr<just::RotaryControl>>> extraRotaries;
    NSTextField* notice;
    BOOL wasAdvanced;
}
- (instancetype)initWithServices:(just::EditorServices)s;
- (void)refresh;
- (void)stop;
@end
@implementation JustGateContentView
- (BOOL)isFlipped{return YES;}
- (instancetype)initWithServices:(just::EditorServices)s {
    self=[super initWithFrame:NSMakeRect(0,0,680,310)];if(!self)return nil;services=s;
    self.identifier=@"JUST.gate.content";self.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
    envelope=[[JustGateEnvelopeView alloc] initWithFrame:NSZeroRect];envelope->services=s;envelope.identifier=[NSString stringWithUTF8String:just::gate::envelopeViewIdentifier];
    envelope.accessibilityLabel=@"Measured Gate input, output and applied gain history";
    envelope.toolTip=@"Two seconds of real raw input/output peaks and applied gain. Dashed line is the current threshold setting. Raw IO is not the filtered/RMS/External SC detector signal. Gaps are not interpolated.";
    [self addSubview:envelope];
    scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.drawsBackground=NO;scroll.autohidesScrollers=YES;
    controls=[[JustGateFlippedView alloc] initWithFrame:NSZeroRect];scroll.documentView=controls;[self addSubview:scroll];
    for(auto id:just::gate::simpleIDs){
        auto rotary=just::RotaryControl::create((__bridge void*)controls,s,just::gate::spec(id),just::gate::displayPolicy(id));
        if(!rotary)return nil;NSView* native=(__bridge NSView*)rotary->nativeHandle();native.accessibilityIdentifier=[NSString stringWithFormat:@"gate.param.%u",id];primaryRotaries.push_back(std::move(rotary));
    }
    additional=[[JustGateFlippedView alloc] initWithFrame:NSZeroRect];[controls addSubview:additional];menus=[NSMutableDictionary dictionary];menuLabels=[NSMutableDictionary dictionary];
    for(auto id:just::gate::additionalIDs){const auto& p=just::gate::spec(id);
        if(p.enumLabels){
            auto* label=[NSTextField labelWithString:[NSString stringWithUTF8String:p.title]];label.alignment=NSTextAlignmentCenter;[additional addSubview:label];menuLabels[@(id)]=label;
            auto* menu=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(unsigned i=0;i<=p.stepCount;++i)[menu addItemWithTitle:[NSString stringWithUTF8String:p.enumLabels[i]]];
            menu.tag=id;menu.target=self;menu.action=@selector(changeMenu:);menu.accessibilityLabel=[NSString stringWithUTF8String:p.title];[additional addSubview:menu];menus[@(id)]=menu;
        }else{auto rotary=just::RotaryControl::create((__bridge void*)additional,s,p,just::gate::displayPolicy(id));if(!rotary)return nil;[(__bridge NSView*)rotary->nativeHandle() setAccessibilityIdentifier:[NSString stringWithFormat:@"gate.param.%u",id]];extraRotaries.emplace_back(id,std::move(rotary));}
    }
    notice=[NSTextField wrappingLabelWithString:@"RMS integration: 5 ms · Stereo Link: 100%\nLookahead: saved target only; actual maximum / effective / PDC = 0 ms.\nSC Listen unavailable. Range limits relative attenuation. Expand Ratio 1 gives unity."];
    notice.textColor=muted();notice.font=[NSFont systemFontOfSize:11];[additional addSubview:notice];
    [self refresh];[self layout];return self;
}
- (void)changeMenu:(NSPopUpButton*)sender {
    const auto& p=just::gate::spec(just::ParamID(sender.tag));just::gate::writeOne(services,p.id,double(sender.indexOfSelectedItem)/p.stepCount);[self refresh];
}
- (void)refresh {
    const bool show=services.view && services.view->advanced;
    if(show!=wasAdvanced){wasAdvanced=show;additional.hidden=!show;scroll.hasVerticalScroller=show;[scroll.contentView scrollToPoint:NSZeroPoint];[self setNeedsLayout:YES];[self layoutSubtreeIfNeeded];}
    additional.hidden=!show;
    const auto mode=unsigned(just::gate::spec(just::gate::mode).toPhysical(just::gate::read(services,just::gate::mode)));
    for(unsigned i=0;i<primaryRotaries.size();++i){auto id=just::gate::simpleIDs[i];primaryRotaries[i]->refresh(just::gate::availability(id,services).enabled);[(__bridge NSView*)primaryRotaries[i]->nativeHandle() setToolTip:[NSString stringWithUTF8String:just::gate::help(id,mode)]];}
    for(auto& entry:extraRotaries){auto a=just::gate::availability(entry.first,services);entry.second->refresh(a.enabled);[(__bridge NSView*)entry.second->nativeHandle() setToolTip:[NSString stringWithUTF8String:a.enabled?just::gate::help(entry.first,mode):a.reason]];}
    for(NSNumber* key in menus){const auto id=just::ParamID(key.unsignedIntValue);const auto& p=just::gate::spec(id);auto a=just::gate::availability(id,services);menuLabels[key].stringValue=[NSString stringWithUTF8String:just::gate::uiText(services,just::gate::chineseLabel(id),p.title)];menus[key].enabled=a.enabled;[menus[key] selectItemAtIndex:NSInteger(std::round(just::gate::read(services,id)*p.stepCount))];menus[key].toolTip=[NSString stringWithUTF8String:a.enabled?just::gate::help(id,mode):a.reason];}
    [envelope updateAnalysis];
}
- (void)layout {
    [super layout];const int w=int(self.bounds.size.width),h=int(self.bounds.size.height);auto layout=just::gate::PreviewLayout::fit(w,h);
    envelope.frame=NSMakeRect(layout.margin,layout.chartY,w-2*layout.margin,layout.chartHeight);
    scroll.frame=NSMakeRect(0,layout.controlY,w,MAX(148,h-layout.controlY));
    const int docWidth=w-(wasAdvanced?16:0);const unsigned columns=MAX(4,docWidth/144);const int cell=docWidth/columns;
    const int rows=(std::size(just::gate::additionalIDs)+columns-1)/columns;
    controls.frame=NSMakeRect(0,0,docWidth,wasAdvanced?154+rows*154+90:154);
    for(unsigned i=0;i<primaryRotaries.size();++i){int primaryCell=std::min(177,docWidth/4);primaryRotaries[i]->resize((docWidth-4*primaryCell)/2+primaryCell*i+(primaryCell-layout.controlWidth)/2,0,layout.controlWidth,148);}
    additional.frame=NSMakeRect(0,154,docWidth,rows*154+90);
    unsigned index=0,rotaryIndex=0;
    for(auto id:just::gate::additionalIDs){int x=(index%columns)*cell,y=(index/columns)*154;
        if(menus[@(id)]){menuLabels[@(id)].frame=NSMakeRect(x,y,cell,22);menus[@(id)].frame=NSMakeRect(x+6,y+45,cell-12,32);}
        else extraRotaries[rotaryIndex++].second->resize(x+(cell-112)/2,y,112,148);++index;
    }
    notice.frame=NSMakeRect(16,rows*154+8,docWidth-32,80);
}
- (void)stop {for(auto& control:primaryRotaries)control->refresh(false);for(auto& entry:extraRotaries)entry.second->refresh(false);}
@end
namespace just::gate {
namespace {
class MacContent final:public EditorContent {
    __strong JustGateContentView* view=nil;
public:
    ~MacContent() override {if(view){[view stop];[view removeFromSuperview];view=nil;}}
    bool attach(void* parent,const EditorServices& s) override {
        if(view || !parent || !s.readTarget)return false;view=[[JustGateContentView alloc] initWithServices:s];if(view)[(__bridge NSView*)parent addSubview:view];return view!=nil;
    }
    void resize(int w,int h) override {if(view){view.frame=NSMakeRect(0,0,w,h);view.needsLayout=YES;[view layoutSubtreeIfNeeded];}}
    void refresh(const EditorViewState&,const StatusSnapshot&) override {[view refresh];}
};
}
EditorContent* createEditorContent(){return new MacContent;}
}
