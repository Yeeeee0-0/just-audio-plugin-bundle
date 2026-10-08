#import <Cocoa/Cocoa.h>
#include "Editor.hpp"
#include "Parameters.hpp"
#include "AlgorithmPolicy.hpp"
#include "HistoryModel.hpp"
#include "PreviewLayout.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/ObjCNames.hpp"
#define JustLimiterContent JUST_OBJC_CLASS(LimiterContent)
#define JustLimiterDetails JUST_OBJC_CLASS(LimiterDetails)

namespace {
NSColor* ink(){return [NSColor colorWithSRGBRed:.09 green:.22 blue:.26 alpha:1];}
NSColor* cyan(){return [NSColor colorWithSRGBRed:.06 green:.71 blue:.77 alpha:1];}
NSColor* muted(){return [NSColor colorWithSRGBRed:.48 green:.61 blue:.65 alpha:1];}
NSRect rect(just::limiter::PreviewRect r){return NSMakeRect(r.x,r.y,r.width,r.height);}
NSString* level(double v){return v>0?[NSString stringWithFormat:@"%.1f",20*std::log10(v)]:@"−∞";}
void text(NSString* value,NSRect r,NSColor* color,CGFloat size,BOOL bold=NO){
 [value drawInRect:r withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size weight:bold?NSFontWeightSemibold:NSFontWeightRegular],NSForegroundColorAttributeName:color}];
}
}
@interface JustLimiterDetails:NSView @end
@implementation JustLimiterDetails
- (BOOL)isFlipped{return YES;}
@end
@interface JustLimiterContent:NSView {
 just::EditorServices services;
 std::array<std::unique_ptr<just::RotaryControl>,10> controls;
 just::limiter::HistoryModel feedback;
 std::uint64_t visualResumeGeneration;
 just::limiter::PreviewLayout geometry;
 just::BusLayoutSnapshot busLayout;
 NSScrollView* details;JustLimiterDetails* detailsPane;
 NSButton* modeButton;NSButton* outputHold;NSButton* reductionHold;
 NSPopUpButton* algorithmPicker;NSTextField* algorithmLabel;NSTextField* algorithmInfo;NSTextField* lookaheadInfo;
 BOOL advanced,bypassed,chinese;
}
- (instancetype)initWithServices:(just::EditorServices)s;
- (void)refreshWithView:(const just::EditorViewState&)state status:(const just::StatusSnapshot&)status;
@end
@implementation JustLimiterContent
- (BOOL)isFlipped{return YES;}
- (instancetype)initWithServices:(just::EditorServices)s {
 self=[super initWithFrame:NSZeroRect];if(!self)return nil;services=s;self.identifier=@"limiter.content";
 details=[[NSScrollView alloc] initWithFrame:NSZeroRect];details.identifier=@"limiter.details";details.drawsBackground=NO;details.hasVerticalScroller=YES;details.autohidesScrollers=YES;
 detailsPane=[[JustLimiterDetails alloc] initWithFrame:NSZeroRect];details.documentView=detailsPane;[self addSubview:details];
 for(unsigned id:{1u,9u,3u,5u,4u,2u,6u,8u}) {
  auto spec=just::limiter::parameters[id];if(id==just::limiter::input)spec.title="Input Gain";if(id==just::limiter::lookahead)spec.title="Requested Lookahead";
  auto* parent=(id==1||id==9)?self:detailsPane;
  just::DisplayPolicy policy;policy.decimals=1;policy.labelZh=id==just::limiter::lookahead?"前瞻请求":just::limiter::controlLabelsZh[id];if(id==1||id==9)policy.style=just::ControlStyle::vertical;
  controls[id]=just::RotaryControl::create((__bridge void*)parent,services,spec,policy);
  if(controls[id]){auto* view=(__bridge NSView*)controls[id]->nativeHandle();view.identifier=[NSString stringWithFormat:@"limiter.control.%u",id];}
 }
 algorithmLabel=[NSTextField labelWithString:@"Algorithm"];algorithmLabel.font=[NSFont systemFontOfSize:11];[detailsPane addSubview:algorithmLabel];
 algorithmPicker=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];[algorithmPicker addItemsWithTitles:@[@"Legacy compatibility",@"Transparent insurance"]];algorithmPicker.target=self;algorithmPicker.action=@selector(changeAlgorithm:);algorithmPicker.identifier=@"limiter.algorithm";[detailsPane addSubview:algorithmPicker];
 algorithmInfo=[NSTextField wrappingLabelWithString:@""];algorithmInfo.font=[NSFont systemFontOfSize:10];algorithmInfo.textColor=muted();[detailsPane addSubview:algorithmInfo];
 lookaheadInfo=[NSTextField labelWithString:@""];lookaheadInfo.font=[NSFont systemFontOfSize:10];lookaheadInfo.textColor=muted();lookaheadInfo.identifier=@"limiter.lookahead.effective";[detailsPane addSubview:lookaheadInfo];
 modeButton=[NSButton checkboxWithTitle:@"Live Sample Peak" target:self action:@selector(changeMode:)];modeButton.identifier=@"limiter.mode";
 modeButton.toolTip=@"Live preserves below-ceiling samples. Mix TP prototype retains the original conservative filter and guard.";[detailsPane addSubview:modeButton];
 outputHold=[NSButton buttonWithTitle:@"Peak —" target:self action:@selector(resetOutput:)];outputHold.identifier=@"limiter.peak";outputHold.toolTip=@"Clear the measured output peak hold";
 reductionHold=[NSButton buttonWithTitle:@"Max GR —" target:self action:@selector(resetReduction:)];reductionHold.identifier=@"limiter.maxgr";reductionHold.toolTip=@"Clear the measured gain reduction hold";
 for(NSButton* b in @[outputHold,reductionHold]){b.bordered=NO;b.font=[NSFont systemFontOfSize:10];b.contentTintColor=muted();[self addSubview:b];}
 return self;
}
- (void)changeAlgorithm:(NSPopUpButton*)picker {
 if(!just::limiter::algorithmRegistered())return;
 const auto id=just::limiter::algorithmVersion;
 // A single appended parameter changes; all existing sound targets stay intact.
 if(services.beginEdit(services.owner,id)){services.performEdit(services.owner,id,picker.indexOfSelectedItem==1?1:0);services.endEdit(services.owner,id);}
}
- (void)changeMode:(NSButton*)button {
 const auto id=just::limiter::mode;if(services.beginEdit(services.owner,id)){services.performEdit(services.owner,id,button.state==NSControlStateValueOn?0:1);services.endEdit(services.owner,id);}
}
- (void)resetOutput:(id)sender {feedback.resetOutput();outputHold.title=chinese?@"峰值 —":@"Peak —";}
- (void)resetReduction:(id)sender {feedback.resetGR();reductionHold.title=chinese?@"最大衰减 —":@"Max GR —";}
- (void)refreshWithView:(const just::EditorViewState&)state status:(const just::StatusSnapshot&)status {
 if(advanced!=state.advanced && self.window.firstResponder)[self.window makeFirstResponder:self];
 advanced=state.advanced;bypassed=status.bypassProtectionExit;chinese=state.language==just::UiLanguage::chinese;
 const bool modern=just::limiter::algorithmRegistered()&&just::limiter::transparentVersion(services.readTarget(services.owner,just::limiter::algorithmVersion));
 algorithmLabel.stringValue=chinese?@"算法":@"Algorithm";
 [algorithmPicker itemAtIndex:0].title=chinese?@"旧版兼容（保留原声）":@"Legacy compatibility";
 [algorithmPicker itemAtIndex:1].title=chinese?@"透明保险（新版）":@"Transparent insurance";
 [algorithmPicker selectItemAtIndex:modern?1:0];algorithmPicker.enabled=just::limiter::algorithmRegistered();
 algorithmPicker.toolTip=chinese?@"切换算法版本；保留输入、输出、限幅上限及其它声音参数。":@"Change the algorithm version while preserving every other sound parameter.";
 modeButton.title=modern?(chinese?@"采样峰值（关闭＝TP重建保护）":@"Sample peak (off = reconstructed TP)"):(chinese?@"旧Live（关闭＝旧Mix）":@"Legacy Live (off = legacy Mix)");
 modeButton.toolTip=modern?(chinese?@"主音频只经过延迟与公共增益。TP使用真实重建峰值检测，触发时保留0.2 dB验证余量；不是模拟峰值的绝对保证。":@"Audio uses delay and linked gain. TP reconstructs peaks and reserves 0.2 dB after an excursion; it is not an absolute analog-peak guarantee."):(chinese?@"保留旧版声音：旧Mix包含滤波和保守峰值保护。":@"Preserves the old sound: legacy Mix includes its filter and conservative guard.");
 algorithmInfo.stringValue=modern?(chinese?@"透明保险：中性、未触发时保持原波形；Input提响，Output独立后置。":@"Transparent insurance: neutral, untriggered audio stays intact; Input raises level and Output follows limiting."):(chinese?@"旧工程兼容路径。选择“透明保险”可升级算法，保留其他声音参数。":@"Legacy sound is preserved. Select Transparent insurance to upgrade while keeping other sound parameters.");
 details.hidden=!advanced;
 for(unsigned id=1;id<controls.size();++id)if(controls[id])controls[id]->refresh((id==1||id==9||advanced));
 modeButton.state=services.readTarget(services.owner,just::limiter::mode)<.5?NSControlStateValueOn:NSControlStateValueOff;
 if(visualResumeGeneration!=state.visualResumeGeneration){feedback={};visualResumeGeneration=state.visualResumeGeneration;}
 if(!state.visualsPaused)feedback.refresh(services);
 busLayout={};if(services.readBusLayout)services.readBusLayout(services.owner,busLayout);
 const double sampleRate=(busLayout.validFields&just::layoutSampleRate)?busLayout.sampleRate:0;
 const bool tp=services.readTarget(services.owner,just::limiter::mode)>=.5;
 const double request=just::limiter::parameters[just::limiter::lookahead].toPhysical(services.readTarget(services.owner,just::limiter::lookahead));
 if(sampleRate>0){const double actual=just::limiter::effectiveLookaheadMs(request,sampleRate,modern,tp);
  lookaheadInfo.stringValue=modern&&tp?[NSString stringWithFormat:chinese?@"前瞻请求 %.2f ms · 实际 %.2f ms · TP最多 %.2f ms（保留固定延迟）":@"Lookahead requested %.2f ms · effective %.2f ms · TP maximum %.2f ms (fixed latency)",request,actual,just::limiter::maximumTpLookaheadMs(sampleRate)]:[NSString stringWithFormat:chinese?@"前瞻请求 %.2f ms · 实际 %.2f ms":@"Lookahead requested %.2f ms · effective %.2f ms",request,actual];
 }else lookaheadInfo.stringValue=chinese?@"实际前瞻：等待宿主采样率":@"Effective lookahead: awaiting the host sample rate";
 outputHold.enabled=reductionHold.enabled=feedback.current();
 outputHold.title=feedback.outputHeld?[NSString stringWithFormat:chinese?@"峰值 %@ dBFS":@"Peak %@ dBFS",level(feedback.outputHold.outputPeak)]:(chinese?@"峰值 —":@"Peak —");
 reductionHold.title=feedback.grHold.reductionValid?[NSString stringWithFormat:chinese?@"最大衰减 %.1f dB":@"Max GR %.1f dB",feedback.grHold.reductionDb]:(chinese?@"最大衰减 —":@"Max GR —");
 [self layout];self.needsDisplay=YES;
}
- (void)layout {
 [super layout];geometry=just::limiter::PreviewLayout::make(self.bounds.size.width,self.bounds.size.height,advanced);
 for(unsigned id:{1u,9u})if(controls[id]){auto r=id==1?geometry.input:geometry.output;controls[id]->resize(r.x,r.y,r.width,r.height);}
 details.frame=rect(geometry.details);
 const double width=geometry.details.width;const unsigned columns=width<950?4:6;const double cell=width/columns;
 algorithmLabel.frame=NSMakeRect(8,6,48,22);algorithmPicker.frame=NSMakeRect(58,2,std::min(250.,width*.34),28);
 modeButton.frame=NSMakeRect(326,2,std::max(120.,width-334),28);
 algorithmInfo.frame=NSMakeRect(8,34,width-16,28);lookaheadInfo.frame=NSMakeRect(8,64,width-16,24);
 const unsigned ids[]={3,5,4,2,6,8};
 for(unsigned i=0;i<6;++i)if(controls[ids[i]])controls[ids[i]]->resize(i%columns*cell,96+i/columns*146,cell,140);
 const double bottom=96+((6+columns-1)/columns)*146;
 detailsPane.frame=NSMakeRect(0,0,width,MAX(details.contentSize.height,bottom+8));
 const auto r=geometry.readout;outputHold.frame=NSMakeRect(r.x+r.width*.42,r.y+34,r.width*.3,24);reductionHold.frame=NSMakeRect(r.x,r.y+34,r.width*.4,24);
}
- (void)drawRect:(NSRect)dirty {
 const auto card=rect(geometry.card),plot=rect(geometry.plot),readout=rect(geometry.readout);
 auto* panel=[NSBezierPath bezierPathWithRoundedRect:card xRadius:11 yRadius:11];
 [[NSColor colorWithSRGBRed:.96 green:.98 blue:.985 alpha:.88] setFill];[panel fill];
 [[NSColor colorWithSRGBRed:.82 green:.89 blue:.91 alpha:1] setStroke];[panel stroke];
 const bool modern=just::limiter::algorithmRegistered()&&just::limiter::transparentVersion(services.readTarget(services.owner,just::limiter::algorithmVersion));
 const bool tp=services.readTarget(services.owner,just::limiter::mode)>=.5;
 NSString* modeText=modern?(tp?(chinese?@"透明保险 · TP重建保护":@"TRANSPARENT · RECONSTRUCTED TP"):(chinese?@"透明保险 · 采样峰值":@"TRANSPARENT · SAMPLE PEAK")):(tp?(chinese?@"旧版兼容 · Mix":@"LEGACY · MIX"):(chinese?@"旧版兼容 · Live":@"LEGACY · LIVE"));
 text(modeText,NSMakeRect(card.origin.x+20,card.origin.y+18,card.size.width-210,18),muted(),10);
 const double hostRate=(busLayout.validFields&just::layoutSampleRate)?busLayout.sampleRate:0;
 if(hostRate>0){const double request=just::limiter::parameters[just::limiter::lookahead].toPhysical(services.readTarget(services.owner,just::limiter::lookahead));const double actual=just::limiter::effectiveLookaheadMs(request,hostRate,modern,tp);
  NSString* timing=modern&&tp?[NSString stringWithFormat:chinese?@"实际前瞻 %.2f ms · 请求 %.2f · TP最多 %.2f":@"Effective lookahead %.2f ms · requested %.2f · TP max %.2f",actual,request,just::limiter::maximumTpLookaheadMs(hostRate)]:[NSString stringWithFormat:chinese?@"实际前瞻 %.2f ms · 请求 %.2f":@"Effective lookahead %.2f ms · requested %.2f",actual,request];
  text(timing,NSMakeRect(card.origin.x+20,card.origin.y+42,card.size.width-40,16),muted(),9);
 }
 const double ceiling=just::limiter::parameters[2].toPhysical(services.readTarget(services.owner,2));
 text([NSString stringWithFormat:chinese?@"限幅上限 %.1f dB":@"CEILING %.1f dB",ceiling],NSMakeRect(NSMaxX(card)-162,card.origin.y+16,152,21),cyan(),13,YES);
 auto y=[&](double db){return NSMaxY(plot)-(std::clamp(db,-60.,12.)+60)/72.*plot.size.height;};
 [[NSColor colorWithSRGBRed:.83 green:.90 blue:.93 alpha:.65] setStroke];
 auto* grid=[NSBezierPath bezierPath];grid.lineWidth=1;
 for(unsigned i=0;i<=6;++i){double x=plot.origin.x+i*plot.size.width/6;[grid moveToPoint:NSMakePoint(x,plot.origin.y)];[grid lineToPoint:NSMakePoint(x,NSMaxY(plot))];}
 for(double db:{-60.,-48.,-36.,-24.,-12.,0.,12.}){[grid moveToPoint:NSMakePoint(plot.origin.x,y(db))];[grid lineToPoint:NSMakePoint(NSMaxX(plot),y(db))];}[grid stroke];
 auto* limit=[NSBezierPath bezierPath];CGFloat dash[]={5,5};[limit setLineDash:dash count:2 phase:0];limit.lineWidth=1.3;
 [limit moveToPoint:NSMakePoint(plot.origin.x,y(ceiling))];[limit lineToPoint:NSMakePoint(NSMaxX(plot),y(ceiling))];[cyan() setStroke];[limit stroke];
 text([NSString stringWithFormat:@"%.1f dB",ceiling],NSMakeRect(NSMaxX(plot)-58,y(ceiling)-18,58,15),muted(),10);
 if(feedback.current()){
  const auto& latest=feedback.latest();const double end=latest.header.endSample,span=latest.header.sampleRate*6;
  [NSGraphicsContext saveGraphicsState];NSRectClip(plot);
  for(unsigned tap=0;tap<2;++tap){auto* path=[NSBezierPath bezierPath];BOOL started=NO;std::uint64_t next=0;
   for(std::size_t i=0;i<feedback.count;++i){const auto& w=feedback.at(i);const auto& h=w.header;
    if(h.flags&just::analysisInvalid || !(h.flags&just::analysisInputAligned)){started=NO;continue;}
    const double x=NSMaxX(plot)-(end-double(h.endSample))/span*plot.size.width;
    if(x<NSMinX(plot)){started=NO;continue;}
    const double peak=std::max(w.channels[tap*2].peak,w.channels[tap*2+1].peak);
    auto p=NSMakePoint(x,y(20*std::log10(std::max(1e-8,peak))));
    if(!started||next!=h.startSample||h.flags&just::analysisGap)[path moveToPoint:p];else[path lineToPoint:p];started=YES;next=h.endSample;
   }
   [(tap?cyan():[NSColor colorWithSRGBRed:.62 green:.74 blue:.79 alpha:1]) setStroke];path.lineWidth=tap?2.4:1.3;[path stroke];
  }[NSGraphicsContext restoreGraphicsState];
  text((latest.effectFields&just::analysisReduction)?[NSString stringWithFormat:@"GR  %.1f dB",latest.reductionDb]:@"GR  —",NSMakeRect(readout.origin.x,readout.origin.y,readout.size.width*.4,24),cyan(),17,YES);
  text([NSString stringWithFormat:@"IN %@ dBFS",level(std::max(latest.channels[0].peak,latest.channels[1].peak))],NSMakeRect(readout.origin.x+readout.size.width*.43,readout.origin.y+3,readout.size.width*.29,20),muted(),11);
  text([NSString stringWithFormat:@"OUT %@ dBFS",level(std::max(latest.channels[2].peak,latest.channels[3].peak))],NSMakeRect(readout.origin.x+readout.size.width*.73,readout.origin.y+3,readout.size.width*.27,20),muted(),11);
 }else{text(@"GR  —",NSMakeRect(readout.origin.x,readout.origin.y,readout.size.width*.4,24),cyan(),17,YES);
  text(feedback.availability==just::AnalysisAvailability::stale?(chinese?@"没有新的音频":@"No new audio"):(chinese?@"等待音频":@"Waiting for audio"),NSMakeRect(plot.origin.x+12,NSMidY(plot)-10,plot.size.width-24,24),muted(),12);}
 NSString* activity=bypassed?(chinese?@"旁路 · 原始输入":@"Bypass · raw input"):(chinese?@"输入 / 输出 · 6 秒":@"Input / output · 6 s");
 if(feedback.current()&&(feedback.latest().header.flags&just::analysisTransportKnown)&&!(feedback.latest().header.flags&just::analysisPlaying))activity=[activity stringByAppendingString:chinese?@" · 宿主已停止":@" · Host stopped"];
 const double rate=(busLayout.validFields&just::layoutSampleRate)?busLayout.sampleRate:0;
 NSString* pdc=rate>0?[NSString stringWithFormat:chinese?@"延迟 %.2f ms · %.0f 样本":@"PDC %.2f ms · %.0f samples",1000*(std::ceil(rate*.005)+48)/rate,std::ceil(rate*.005)+48]:(chinese?@"延迟等待宿主采样率":@"PDC awaiting host rate");
 const auto footer=rect(geometry.status);
 text(activity,NSMakeRect(footer.origin.x,footer.origin.y,footer.size.width*.48,20),muted(),10);
 text(chinese?@"输出增益在限幅之后":@"OUTPUT GAIN FOLLOWS THE LIMIT",NSMakeRect(footer.origin.x+footer.size.width*.34,footer.origin.y,footer.size.width*.38,20),muted(),9);
 text(pdc,NSMakeRect(footer.origin.x+footer.size.width*.76,footer.origin.y,footer.size.width*.24,20),muted(),10);
}
@end
namespace just::limiter {
class MacEditor final:public EditorContent {
 JustLimiterContent* view=nil;
public:
 ~MacEditor() override {[view removeFromSuperview];view=nil;}
 bool attach(void* parent,const EditorServices& services) override {
  if(!parent||!services.readTarget||!services.beginEdit||!services.performEdit||!services.endEdit)return false;
  view=[[JustLimiterContent alloc] initWithServices:services];view.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];[(__bridge NSView*)parent addSubview:view];return bool(view);
 }
 void resize(int width,int height) override {view.frame=NSMakeRect(0,0,width,height);[view layout];}
 void refresh(const EditorViewState& state,const StatusSnapshot& status) override {[view refreshWithView:state status:status];}
};
EditorContent* createEditor(){return new MacEditor;}
}
