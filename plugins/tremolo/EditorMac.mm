#import <Cocoa/Cocoa.h>
#include "Editor.hpp"
#include "EditorModel.hpp"
#include "AmountDisplay.hpp"
#include "GainViewMac.hpp"
#include "common/ui/Controls.hpp"
#include <vector>

namespace t=just::tremolo;
#define JTTremoloDocument JUST_OBJC_CLASS(TremoloDocument)
#define JTTremoloContentView JUST_OBJC_CLASS(TremoloContentView)
@interface JTTremoloDocument : NSView
@end
@implementation JTTremoloDocument
- (BOOL)isFlipped{return YES;}
@end
struct TremoloRotary {
    just::ParamID id;int primarySlot;NSUInteger advancedSlot;
    std::unique_ptr<just::RotaryControl> control;
};
static const char* tremoloTranslation(just::ParamID id) {
    switch(id){case t::rateHz:return "自由速率";case t::depth:return "深度";case t::mix:return "混合";
        case t::phase:return "相位";case t::stereoPhase:return "立体声相位";case t::duty:return "占空比";
        case t::edgeMs:return "边缘";case t::inputGain:return "输入增益";case t::outputGain:return "输出增益";
        case t::shape:return "波形";case t::sync:return "同步";case t::division:return "拍分";case t::timeMode:return "时间模式";default:return "";}
}
static bool tremoloRateParse(const char* text,double& normalized) {
    if(t::spec(t::rateHz).parse(text,normalized))return true;
    char* end=nullptr;double ms=std::strtod(text,&end);while(end && (*end==' ' || *end=='\t'))++end;
    if(end==text || !end || std::strcmp(end,"ms") || !std::isfinite(ms) || ms<=0)return false;
    double hz=1000/ms;if(hz<t::spec(t::rateHz).minimum || hz>t::spec(t::rateHz).maximum)return false;
    normalized=t::spec(t::rateHz).toNormalized(hz);return true;
}
static void tremoloPhaseFormat(double n,just::DisplayContext context,char* text,std::size_t size) {
    const double physical=t::spec(t::phase).toPhysical(n);
    if(context==just::DisplayContext::editing)std::snprintf(text,size,"%.17g",physical);
    else std::snprintf(text,size,"%.1f",t::fraction(physical/360)*360);
}
@interface JTTremoloContentView : NSView {
    t::EditorModel* model;
    std::unique_ptr<t::AmountDisplay> amountDisplay;
    std::vector<TremoloRotary> controls;
    NSScrollView* scroll;
    JTTremoloDocument* document;
    NSMutableArray<NSView*>* advancedSlots;
    NSMutableArray<NSPopUpButton*>* choices;
    NSMutableArray<NSTextField*>* choiceLabels;
    NSTextField* advancedTitle;
    JTTremoloGainView* appliedGain;
    NSSegmentedControl* timingMode;
    NSTextField* timingReadout;
    BOOL showAdvanced;
}
- (instancetype)initWithModel:(t::EditorModel*)m;
- (void)refreshAdvanced:(BOOL)next;
@end
@implementation JTTremoloContentView
- (BOOL)isFlipped{return YES;}
- (NSString*)textZh:(const char*)zh en:(const char*)en {
    const auto* view=model->editorServices().view;
    return [NSString stringWithUTF8String:view?just::localized(*view,zh,en):en];
}
- (void)addControl:(just::ParamID)id title:(const char*)title primary:(int)slot {
    auto display=t::spec(id);if(title)display.title=title;
    if(id==t::phase || id==t::stereoPhase)display.unit="°";
    just::DisplayPolicy policy;policy.labelZh=slot==0?(id==t::division?"节拍":"频率"):slot==2?"立体声分离":tremoloTranslation(id);
    if(id==t::rateHz)policy.parse=tremoloRateParse;
    if(id==t::phase)policy.format=tremoloPhaseFormat;
    auto control=just::RotaryControl::create((__bridge void*)document,model->editorServices(),display,policy);
    if(!control)return;
    NSView* native=(__bridge NSView*)control->nativeHandle();native.accessibilityIdentifier=[NSString stringWithFormat:@"just.tremolo.%s.%u",slot>=0?"primary":"advanced",unsigned(id)];
    native.accessibilityLabel=[NSString stringWithUTF8String:display.title];
    NSUInteger index=advancedSlots.count;if(slot<0)[advancedSlots addObject:native];
    controls.push_back({id,slot,index,std::move(control)});
}
- (void)addAmount {
    amountDisplay=std::make_unique<t::AmountDisplay>(model->editorServices());
    just::DisplayPolicy policy;policy.labelZh="幅度";policy.context=amountDisplay.get();
    policy.formatWithContext=[](void* p,double n,just::DisplayContext c,char* out,std::size_t size){static_cast<t::AmountDisplay*>(p)->format(n,c,out,size);};
    policy.parseWithContext=[](void* p,const char* text,double& n){return static_cast<t::AmountDisplay*>(p)->parse(text,n);};
    auto control=just::RotaryControl::create((__bridge void*)document,amountDisplay->controlServices(),amountDisplay->controlSpec(),policy);
    if(!control)return;
    NSView* native=(__bridge NSView*)control->nativeHandle();native.accessibilityIdentifier=@"just.tremolo.primary.4352";native.accessibilityLabel=@"Amount";
    controls.push_back({t::depth,1,0,std::move(control)});
}
- (void)addChoice:(just::ParamID)id {
    auto* slot=[[JTTremoloDocument alloc] initWithFrame:NSZeroRect];
    auto* label=[NSTextField labelWithString:@""];label.alignment=NSTextAlignmentCenter;
    label.font=[NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];label.textColor=[NSColor colorWithRed:.09 green:.20 blue:.24 alpha:1];
    auto* choice=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];choice.tag=id;
    const auto& parameter=t::spec(id);for(unsigned i=0;i<=parameter.stepCount;++i)[choice addItemWithTitle:[NSString stringWithUTF8String:parameter.enumLabels[i]]];
    choice.target=self;choice.action=@selector(choiceChanged:);choice.accessibilityLabel=[NSString stringWithUTF8String:parameter.title];
    [slot addSubview:label];[slot addSubview:choice];[document addSubview:slot];
    [advancedSlots addObject:slot];[choiceLabels addObject:label];[choices addObject:choice];
}
- (instancetype)initWithModel:(t::EditorModel*)m {
    self=[super initWithFrame:NSZeroRect];if(!self)return nil;model=m;self.identifier=@"just.tremolo.content";
    scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.drawsBackground=NO;scroll.hasHorizontalScroller=NO;
    document=[[JTTremoloDocument alloc] initWithFrame:NSZeroRect];scroll.documentView=document;[self addSubview:scroll];
    advancedSlots=[NSMutableArray array];choices=[NSMutableArray array];choiceLabels=[NSMutableArray array];
    appliedGain=[[JTTremoloGainView alloc] initWithFrame:NSZeroRect];appliedGain->services=m->editorServices();
    appliedGain.identifier=[NSString stringWithUTF8String:just::analysisViewIdentifier];
    appliedGain.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];[document addSubview:appliedGain];
    timingMode=[NSSegmentedControl segmentedControlWithLabels:@[@"Free",@"Sync"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(timingChanged:)];
    timingMode.accessibilityLabel=@"Free / Sync";[document addSubview:timingMode];
    timingReadout=[NSTextField labelWithString:@""];timingReadout.font=[NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
    timingReadout.alignment=NSTextAlignmentRight;timingReadout.textColor=[NSColor colorWithRed:.43 green:.59 blue:.64 alpha:1];[document addSubview:timingReadout];
    [self addControl:t::rateHz title:"Frequency" primary:0];
    [self addControl:t::division title:"Rate" primary:0];
    [self addAmount];
    [self addControl:t::stereoPhase title:"Stereo Separation" primary:2];
    advancedTitle=[NSTextField labelWithString:@""];advancedTitle.font=[NSFont systemFontOfSize:10 weight:NSFontWeightMedium];advancedTitle.textColor=timingReadout.textColor;[document addSubview:advancedTitle];
    [self addChoice:t::shape];[self addControl:t::phase title:nullptr primary:-1];
    [self addControl:t::stereoPhase title:nullptr primary:-1];[self addControl:t::duty title:nullptr primary:-1];
    [self addControl:t::edgeMs title:nullptr primary:-1];[self addControl:t::rateHz title:"Free Rate" primary:-1];
    [self addChoice:t::sync];[self addChoice:t::division];[self addChoice:t::timeMode];
    [self addControl:t::depth title:nullptr primary:-1];[self addControl:t::mix title:nullptr primary:-1];
    [self addControl:t::inputGain title:nullptr primary:-1];[self addControl:t::outputGain title:nullptr primary:-1];
    [self refreshAdvanced:NO];return self;
}
- (BOOL)editable:(just::ParamID)id {
    if(id==t::rateHz)return model->frequencyEditable();
    if(id==t::division)return !model->frequencyEditable();
    if(id==t::stereoPhase)return model->separationEditable();
    if(id==t::duty)return model->value(t::shape)==2;
    if(id==t::edgeMs)return model->value(t::shape)>=2;
    return YES;
}
- (void)timingChanged:(NSSegmentedControl*)sender {
    model->writePhysical(t::sync,sender.selectedSegment);model->cancel();[self refreshAdvanced:showAdvanced];
}
- (void)choiceChanged:(NSPopUpButton*)sender {
    model->writePhysical(just::ParamID(sender.tag),sender.indexOfSelectedItem);model->cancel();[self refreshAdvanced:showAdvanced];
}
- (void)refreshAdvanced:(BOOL)next {
    if(showAdvanced!=next){model->cancel();showAdvanced=next;self.needsLayout=YES;}
    [appliedGain updateAnalysis];const bool sync=!model->frequencyEditable();timingMode.selectedSegment=sync?1:0;
    const auto* latest=[appliedGain latest];
    if(sync) {
        if(appliedGain->availability==just::AnalysisAvailability::fresh && latest && (latest->effectFields&just::analysisModulation) && latest->effectiveHz>0) {
            NSString* fallback=(latest->effectFlags&1)?[self textZh:" · 速度回退" en:" · Tempo fallback"]:(latest->effectFlags&2)?[self textZh:" · 位置不可用" en:" · Position unavailable"]:@"";
            const double hz=latest->effectiveHz;const int decimals=hz<.1?std::min(8,int(std::ceil(-std::log10(hz)))+1):hz<1?2:1;
            timingReadout.stringValue=[NSString stringWithFormat:@"%.*f Hz%@",decimals,hz,fallback];
        }else timingReadout.stringValue=[self textZh:"同步 · 等待音频" en:"Sync · waiting for audio"];
    }else timingReadout.stringValue=model->value(t::timeMode)==1?[self textZh:"Transport 需要同步" en:"Transport needs Sync"]:@"";
    for(auto& item:controls) {
        NSView* native=(__bridge NSView*)item.control->nativeHandle();
        const bool visible=item.primarySlot>=0?!(item.primarySlot==0 && ((item.id==t::division)!=sync)):showAdvanced;
        const bool enabled=visible && (item.primarySlot==1?model->amountEditable():[self editable:item.id]);
        item.control->refresh(enabled);native.hidden=!visible;
        if(item.primarySlot==1 && !enabled)native.toolTip=[self textZh:"Mix 为 0%；已保存 Depth 保留" en:"Mix is 0%; saved Depth is retained"];
        else if(item.id==t::stereoPhase && !model->separationEditable())native.toolTip=[self textZh:"单声道或声道未知；已保存相位保留" en:"Mono or unknown bus; saved phase is retained"];
        else native.toolTip=nil;
    }
    for(NSUInteger i=0;i<choices.count;++i) {
        NSPopUpButton* choice=choices[i];const auto id=just::ParamID(choice.tag);
        choice.superview.hidden=!showAdvanced;choice.enabled=showAdvanced && [self editable:id];
        for(unsigned index=0;index<=t::spec(id).stepCount;++index) {
            const char* zh=t::spec(id).enumLabels[index];
            static constexpr const char* waveformZh[]={"正弦","三角","方波","上行锯齿","下行锯齿"};
            static constexpr const char* timeZh[]={"自由","跟随播放位置","播放时重触发"};
            if(id==t::shape)zh=waveformZh[index];else if(id==t::sync)zh=index?"开启":"关闭";else if(id==t::timeMode)zh=timeZh[index];
            [choice itemAtIndex:index].title=[self textZh:zh en:t::spec(id).enumLabels[index]];
        }
        [choice selectItemAtIndex:NSInteger(model->value(id))];
        choiceLabels[i].stringValue=[self textZh:tremoloTranslation(id) en:t::spec(id).title];
    }
    advancedTitle.stringValue=[self textZh:"进阶控制" en:"ADDITIONAL CONTROLS"];advancedTitle.hidden=!showAdvanced;
    if(scroll.hasVerticalScroller!=showAdvanced){scroll.hasVerticalScroller=showAdvanced;self.needsLayout=YES;}
    [self layoutSubtreeIfNeeded];self.needsDisplay=YES;
}
- (void)layout {
    [super layout];const CGFloat w=self.bounds.size.width,h=self.bounds.size.height;
    scroll.frame=self.bounds;const CGFloat dw=MAX(1,w-(showAdvanced?16:0));
    const CGFloat margin=h>=400?24:12,graphHeight=MAX(96,MIN(244,h-184));
    const CGFloat primaryTop=margin+graphHeight+16,primaryHeight=144;
    const unsigned columns=MAX(2,unsigned((dw-48)/166));const CGFloat cell=(dw-48)/columns;
    const CGFloat advancedTop=primaryTop+primaryHeight+44;
    const CGFloat documentHeight=showAdvanced?MAX(h,advancedTop+((advancedSlots.count+columns-1)/columns)*160+16):h;
    document.frame=NSMakeRect(0,0,dw,documentHeight);
    appliedGain.frame=NSMakeRect(24,margin,dw-48,graphHeight);
    timingMode.frame=NSMakeRect(dw-184,margin+12,146,26);
    const CGFloat readoutWidth=MAX(80,MIN(210,dw-470));timingReadout.frame=NSMakeRect(dw-194-readoutWidth,margin+16,readoutWidth,18);
    const CGFloat primaryWidth=156,gap=MIN(100,MAX(24,(dw-3*primaryWidth)/4)),rowWidth=3*primaryWidth+2*gap,rowLeft=(dw-rowWidth)/2;
    for(auto& item:controls) {
        if(item.primarySlot>=0)item.control->resize(rowLeft+item.primarySlot*(primaryWidth+gap),primaryTop,primaryWidth,primaryHeight);
        else item.control->resize(24+(item.advancedSlot%columns)*cell,advancedTop+(item.advancedSlot/columns)*160,cell-8,148);
    }
    advancedTitle.frame=NSMakeRect(28,advancedTop-26,dw-56,20);
    for(NSUInteger i=0;i<advancedSlots.count;++i) {
        NSView* slot=advancedSlots[i];if([slot.identifier isEqualToString:[NSString stringWithUTF8String:just::rotaryViewIdentifier]])continue;
        slot.frame=NSMakeRect(24+(i%columns)*cell,advancedTop+(i/columns)*160,cell-8,148);
        for(NSView* child in slot.subviews)child.frame=[child isKindOfClass:NSPopUpButton.class]?NSMakeRect(4,54,cell-16,28):NSMakeRect(0,0,cell-8,20);
    }
}
@end
namespace just::tremolo {
class MacEditor final:public EditorContent {
    std::unique_ptr<EditorModel> model;JTTremoloContentView* view=nil;
public:
    ~MacEditor()override{if(model)model->cancel();[view removeFromSuperview];view=nil;}
    bool attach(void* parent,const EditorServices& services)override{
        if(!parent || view)return false;model=std::make_unique<EditorModel>(services);
        view=[[JTTremoloContentView alloc] initWithModel:model.get()];[(__bridge NSView*)parent addSubview:view];return view!=nil;
    }
    void resize(int w,int h)override{view.frame=NSMakeRect(0,0,w,h);view.needsLayout=YES;[view layoutSubtreeIfNeeded];}
    void refresh(const EditorViewState& state,const StatusSnapshot&)override{[view refreshAdvanced:state.advanced];}
};
EditorContent* createEditorContent(){return new MacEditor;}
}
