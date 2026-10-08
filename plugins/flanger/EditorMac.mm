#import <Cocoa/Cocoa.h>
#include "EditorContent.hpp"
#include "UiModel.hpp"
#include "common/ui/AnalysisView.hpp"
#include "common/ui/Controls.hpp"

static NSColor* flangerColor(unsigned rgb) {
    return [NSColor colorWithRed:((rgb>>16)&255)/255. green:((rgb>>8)&255)/255. blue:(rgb&255)/255. alpha:1];
}
static NSString* flangerText(const just::EditorServices& s,const char* zh,const char* en) {
    return [NSString stringWithUTF8String:s.view?just::localized(*s.view,zh,en):en];
}
static NSString* flangerEnumText(const just::EditorServices& s,just::ParamID id,unsigned item,const char* english) {
    if(!s.view || s.view->language!=just::UiLanguage::chinese)return [NSString stringWithUTF8String:english];
    if(id==just::flanger::syncID)return item?@"开启":@"关闭";
    if(id==just::flanger::modeID)return item?@"手动":@"LFO";
    if(id==just::flanger::shapeID)return item?@"三角":@"正弦";
    if(id==just::flanger::wetPolarityID)return item?@"反相":@"正常";
    if(id==just::flanger::timeModeID)return item==0?@"自由":item==1?@"跟随传输":@"播放时重置";
    NSString* text=[NSString stringWithUTF8String:english];
    for(NSArray<NSString*>* pair in @[@[@"bars",@"小节"],@[@"bar",@"小节"],@[@"dotted",@"附点"],@[@"triplet",@"三连音"]])text=[text stringByReplacingOccurrencesOfString:pair[0] withString:pair[1]];
    return text;
}
static NSString* actualAudioState(const just::EditorServices& s,const just::AnalysisWindow& w) {
    NSString* state=@"";
    if(w.header.flags&just::analysisBypassed)state=flangerText(s," · 旁通"," · Bypass");
    else if((w.header.flags&just::analysisTransportKnown) && !(w.header.flags&just::analysisPlaying))state=flangerText(s," · 宿主停止"," · Host stopped");
    if(std::max(w.channels[0].peak,w.channels[1].peak)==0)
        state=[state stringByAppendingString:std::max(w.channels[2].peak,w.channels[3].peak)>0?flangerText(s," · 尾音/输出持续"," · Tail/output active"):flangerText(s," · 静音"," · Silence")];
    if(w.header.flags&just::analysisInvalid)state=[state stringByAppendingString:flangerText(s," · 输入无效"," · Invalid input")];
    return state;
}
@interface FlangerAdvancedCanvas : NSView
@end
@implementation FlangerAdvancedCanvas
- (BOOL)isFlipped{return YES;}
@end
@interface FlangerContentView : NSView {
    just::EditorServices services;
    std::array<std::unique_ptr<just::RotaryControl>,16> rotaries;
    NSMutableArray<NSTextField*>* names;
    NSMutableArray<NSPopUpButton*>* menus;
    NSScrollView* scroll;
    NSView* advancedCanvas;
    std::unique_ptr<just::AnalysisView> graph;
    just::AnalysisCursor analysisCursor;
    std::uint64_t visualResumeGeneration;
    just::RuntimeTelemetrySnapshot presentationTelemetry;
    just::TelemetryAvailability presentationTelemetryAvailability;
    NSTextField *warning,*timing;
    NSSegmentedControl* clock;
    BOOL advancedVisible;
}
- (instancetype)initWithServices:(just::EditorServices)s;
- (void)refreshAdvanced:(BOOL)advanced;
- (void)cancelGestures;
@end
@implementation FlangerContentView
- (BOOL)isFlipped{return YES;}
- (instancetype)initWithServices:(just::EditorServices)s {
    self=[super initWithFrame:NSMakeRect(0,0,720,340)];if(!self)return nil;
    services=s;names=[NSMutableArray array];menus=[NSMutableArray array];
    graph=just::AnalysisView::create((__bridge void*)self,s,just::AnalysisViewMode::spectrum);
    warning=[NSTextField labelWithString:@""];warning.font=[NSFont systemFontOfSize:10];warning.textColor=flangerColor(0x6d8e98);warning.identifier=@"flanger.measured-delay";[self addSubview:warning];
    timing=[NSTextField labelWithString:@""];timing.font=[NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];timing.textColor=flangerColor(0x73949f);timing.alignment=NSTextAlignmentRight;timing.identifier=@"flanger.timing-source";[self addSubview:timing];
    clock=[[NSSegmentedControl alloc]init];clock.segmentCount=2;[clock setLabel:@"Free" forSegment:0];[clock setLabel:@"Sync" forSegment:1];clock.target=self;clock.action=@selector(clockChanged:);clock.identifier=@"flanger.clock-mode";clock.toolTip=@"Use saved Free Rate or the host tempo and saved Division. Sync may fall back when host timing is unavailable.";[self addSubview:clock];
    scroll=[[NSScrollView alloc]init];scroll.hasVerticalScroller=YES;scroll.drawsBackground=NO;scroll.autohidesScrollers=YES;
    advancedCanvas=[[FlangerAdvancedCanvas alloc]initWithFrame:NSZeroRect];scroll.documentView=advancedCanvas;[self addSubview:scroll];
    for(unsigned position=0;position<just::flanger::ui::order.size();++position) {
        const auto id=just::flanger::ui::order[position];const auto& spec=just::flanger::parameters[just::flanger::registry.index(id)];
        NSView* parent=position<4?self:advancedCanvas;
        NSTextField* label=[NSTextField labelWithString:[NSString stringWithUTF8String:spec.title]];
        label.font=[NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];label.textColor=flangerColor(0x17333c);label.alignment=NSTextAlignmentCenter;
        [names addObject:label];[parent addSubview:label];label.hidden=!spec.enumLabels;
        NSPopUpButton* menu=[[NSPopUpButton alloc]initWithFrame:NSZeroRect pullsDown:NO];menu.tag=id;menu.target=self;menu.action=@selector(menuChanged:);
        if(spec.enumLabels)for(unsigned i=0;i<=spec.stepCount;++i)[menu addItemWithTitle:[NSString stringWithUTF8String:spec.enumLabels[i]]];
        [menus addObject:menu];[parent addSubview:menu];menu.hidden=!spec.enumLabels;
        if(!spec.enumLabels) {
            just::DisplayPolicy policy;policy.labelZh=just::flanger::ui::labelsZh[position];
            rotaries[position]=just::RotaryControl::create((__bridge void*)parent,s,spec,policy);
            NSView* native=(__bridge NSView*)rotaries[position]->nativeHandle();native.accessibilityIdentifier=[NSString stringWithUTF8String:spec.stableKey];
        }
    }
    [self refreshAdvanced:NO];return self;
}
- (void)cancelGestures {for(auto& control:rotaries)if(control)control->refresh(false);}
- (void)dealloc {for(auto& control:rotaries)control.reset();graph.reset();}
- (void)write:(just::ParamID)id normalized:(double)value {
    if(value==services.readTarget(services.owner,id))return;
    if(services.beginEdit(services.owner,id)){services.performEdit(services.owner,id,value);services.endEdit(services.owner,id);}
    [self refreshAdvanced:advancedVisible];
}
- (void)clockChanged:(NSSegmentedControl*)sender {[self write:just::flanger::syncID normalized:sender.selectedSegment==1?1:0];}
- (void)menuChanged:(NSPopUpButton*)sender {
    const auto id=just::ParamID(sender.tag);const auto& spec=just::flanger::parameters[just::flanger::registry.index(id)];
    [self write:id normalized:double(sender.indexOfSelectedItem)/spec.stepCount];
}
- (void)refreshAdvanced:(BOOL)advanced {
    if(advancedVisible!=advanced)[self cancelGestures];advancedVisible=advanced;scroll.hidden=!advanced;graph->refresh();
    auto read=[&](just::ParamID id){const auto& spec=just::flanger::parameters[just::flanger::registry.index(id)];return spec.toPhysical(services.readTarget(services.owner,id));};
    const bool manual=read(just::flanger::modeID)==1,sync=read(just::flanger::syncID)==1;
    just::BusLayoutSnapshot buses;
    const bool stereo=services.readBusLayout && services.readBusLayout(services.owner,buses) && (buses.validFields&just::layoutBuses) && buses.outputChannels==2;
    [clock setLabel:flangerText(services,"自由","Free") forSegment:0];[clock setLabel:flangerText(services,"同步","Sync") forSegment:1];
    clock.toolTip=flangerText(services,"使用保存的自由速率，或宿主速度与保存的节拍。缺少宿主时序时，同步会回退。","Use saved Free Rate or host tempo and saved Division. Sync may fall back when host timing is unavailable.");
    clock.selectedSegment=sync?1:0;clock.enabled=!manual;
    for(unsigned i=0;i<just::flanger::ui::order.size();++i) {
        const auto id=just::flanger::ui::order[i];const auto& spec=just::flanger::parameters[just::flanger::registry.index(id)];
        const bool divisionInSimple=i==10 && sync && !manual && !advanced;
        NSView* parent=(advanced || i>=4) && !divisionInSimple?advancedCanvas:self;
        for(NSView* child in @[names[i],menus[i]])if(child.superview!=parent){[child removeFromSuperview];[parent addSubview:child];}
        if(rotaries[i]) {
            NSView* native=(__bridge NSView*)rotaries[i]->nativeHandle();
            if(native.superview!=parent){[native removeFromSuperview];[parent addSubview:native];}
            native.hidden=!advanced && i==0 && sync && !manual;
            rotaries[i]->refresh(just::flanger::ui::enabled(id,manual,sync,stereo));
            native.toolTip=id==just::flanger::stereoPhaseID?(stereo?@"Stereo bus; applies even when L/R samples are equal.":@"Mono or unknown bus; saved Stereo Phase is retained."):[NSString stringWithFormat:@"%s · %.6g–%.6g %s",spec.title,spec.minimum,spec.maximum,spec.unit];
        } else {
            menus[i].enabled=just::flanger::ui::enabled(id,manual,sync,stereo);
            for(unsigned item=0;item<=spec.stepCount;++item)[menus[i] itemAtIndex:item].title=flangerEnumText(services,id,item,spec.enumLabels[item]);
            [menus[i] selectItemAtIndex:std::lround(spec.toPhysical(services.readTarget(services.owner,id)))];
            names[i].stringValue=divisionInSimple?flangerText(services,"速率 / 节拍","Rate / Division"):flangerText(services,just::flanger::ui::labelsZh[i],spec.title);
        }
    }
    const auto* view=services.view;
    if(view && visualResumeGeneration!=view->visualResumeGeneration){analysisCursor={};presentationTelemetry={};presentationTelemetryAvailability=just::TelemetryAvailability::unavailable;warning.stringValue=@"";visualResumeGeneration=view->visualResumeGeneration;}
    if(!view || !view->visualsPaused)presentationTelemetryAvailability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,presentationTelemetry):just::TelemetryAvailability::unavailable;
    const auto& runtime=presentationTelemetry;const auto availability=presentationTelemetryAvailability;
    const auto actual=just::flanger::ui::timingReadout(availability,runtime);
    if(manual)timing.stringValue=flangerText(services,"手动 · LFO 停用","Manual · LFO inactive");
    else if(sync && actual.rateAvailable) {
        NSString* source=actual.syncFallback?flangerText(services,"回退 · 宿主时序不可用","Fallback · host timing unavailable"):actual.syncKnown && actual.tempoAvailable?[NSString stringWithFormat:flangerText(services,"宿主 %.1f BPM","Host %.1f BPM"),actual.bpm]:flangerText(services,"同步来源不可用","Sync source unavailable");
        timing.stringValue=[NSString stringWithFormat:@"%.2f Hz · %@",actual.rateHz,source];
    } else if(sync)timing.stringValue=availability==just::TelemetryAvailability::stale?flangerText(services,"同步 · 实测速率已过期","Sync · measured rate stale"):flangerText(services,"同步 · 实测速率不可用","Sync · measured rate unavailable");
    else timing.stringValue=flangerText(services,"自由 · 保存的速率","Free · saved Rate");
    timing.toolTip=timing.stringValue;
    if(!view || !view->visualsPaused){
    just::AnalysisBatch batch;just::AnalysisWindow measured{};bool got=false;
    auto available=just::AnalysisAvailability::unavailable;
    for(unsigned i=0;i<16 && services.readAnalysis;++i){available=services.readAnalysis(services.owner,analysisCursor,batch);if(available!=just::AnalysisAvailability::fresh || !batch.count)break;measured=batch.windows[batch.count-1];got=true;}
    if(got && (measured.effectFields&just::analysisDelay)) {
        warning.stringValue=[NSString stringWithFormat:flangerText(services,"实际延时 L/R %.2f / %.2f ms%@%@%@","Actual delay L/R %.2f / %.2f ms%@%@%@"),measured.delayMs[0],measured.delayMs[1],std::abs(read(just::flanger::feedbackID))>=80?flangerText(services," · 高反馈"," · high feedback"):@"",std::max(measured.channels[2].peak,measured.channels[3].peak)>1?flangerText(services," · 输出 > 0 dBFS"," · Output > 0 dBFS"):@"",actualAudioState(services,measured)];
        if(measured.effectFlags&1)warning.stringValue=[warning.stringValue stringByAppendingString:flangerText(services," · 同步回退"," · Sync fallback")];
    } else if(available!=just::AnalysisAvailability::fresh)warning.stringValue=available==just::AnalysisAvailability::stale?flangerText(services,"实测延时已过期","Measured delay stale"):flangerText(services,"实际延时不可用","Actual delay unavailable");
    }
    warning.toolTip=warning.stringValue;self.needsLayout=YES;[self layoutSubtreeIfNeeded];
}
- (void)layout {
    [super layout];const int w=self.bounds.size.width,h=self.bounds.size.height;const auto g=just::flanger::ui::layout(w,h,advancedVisible);
    graph->resize(g.margin,g.graphTop,w-2*g.margin,g.graphHeight);
    clock.frame=NSMakeRect(w-g.margin-126,0,126,28);timing.frame=NSMakeRect(g.margin,4,MAX(1,w-2*g.margin-136),20);
    warning.frame=NSMakeRect(g.margin,h-24,w-2*g.margin,20);
    const int top=g.controlsTop;
    scroll.frame=NSMakeRect(g.margin,top,w-2*g.margin,MAX(1,h-top-24));
    advancedCanvas.frame=NSMakeRect(0,0,w-2*g.margin,g.canvasHeight);
    const int rowWidth=4*g.controlWidth+3*g.gap,start=(w-rowWidth)/2;
    for(unsigned i=0;i<just::flanger::ui::order.size();++i) {
        const bool simple=(!advancedVisible && i<4) || (!advancedVisible && i==10 && menus[i].superview==self);
        const int x=simple?start+(i==10?0:i)*(g.controlWidth+g.gap):(i%g.columns)*(g.controlWidth+g.gap);
        const int y=simple?top:(i/g.columns)*160;
        if(rotaries[i])rotaries[i]->resize(x,y,g.controlWidth,g.controlHeight);
        names[i].frame=NSMakeRect(x,y,g.controlWidth,22);menus[i].frame=NSMakeRect(x,y+55,g.controlWidth,28);
    }
}
@end
namespace just::flanger {
class MacContent final:public EditorContent {
    FlangerContentView* __strong content=nil;
public:
    ~MacContent() override {if(content){[content cancelGestures];[content removeFromSuperview];content=nil;}}
    bool attach(void* parent,const EditorServices& services) override {
        if(!parent || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return false;
        content=[[FlangerContentView alloc]initWithServices:services];[(__bridge NSView*)parent addSubview:content];return content!=nil;
    }
    void resize(int width,int height) override {content.frame=NSMakeRect(0,0,width,height);content.needsLayout=YES;[content layoutSubtreeIfNeeded];}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {[content refreshAdvanced:state.advanced];}
};
EditorContent* createEditorContent(){return new MacContent;}
}
