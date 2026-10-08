#import <Cocoa/Cocoa.h>
#include "EditorModel.hpp"
#include "FeedbackHistory.hpp"
#include "PreviewLayout.hpp"
#include <memory>
using namespace just;
using namespace just::delay;
#include "common/ui/Controls.hpp"
#include <vector>
static NSString* uiText(const EditorServices& services,const char* zh,const char* en){return [NSString stringWithUTF8String:localized(*services.view,zh,en)];}
struct DelayRotary {
    ParamID id=0;bool simple=false,custom=false;
    std::unique_ptr<ControlServicesAdapter> adapter;
    std::unique_ptr<RotaryControl> control;
};
// Native button actions retain OS focus/keyboard behavior; only the route
// pill's appearance belongs to the module layout.
@interface JustDelayPingButton : NSButton
@end
@implementation JustDelayPingButton
- (void)drawRect:(NSRect)dirty {
    const BOOL on=self.state==NSControlStateValueOn;
    NSRect rect=NSInsetRect(self.bounds,1,1);
    auto* pill=[NSBezierPath bezierPathWithRoundedRect:rect xRadius:16 yRadius:16];
    [(on?[NSColor colorWithRed:.85 green:.96 blue:.96 alpha:1]:NSColor.whiteColor) setFill];[pill fill];
    [(on?[NSColor colorWithRed:.52 green:.83 blue:.85 alpha:1]:[NSColor colorWithRed:.83 green:.89 blue:.91 alpha:1]) setStroke];[pill stroke];
    NSDictionary* text=@{NSFontAttributeName:[NSFont systemFontOfSize:11 weight:NSFontWeightMedium],NSForegroundColorAttributeName:on?[NSColor colorWithRed:0 green:.49 blue:.54 alpha:1]:[NSColor colorWithRed:.39 green:.51 blue:.56 alpha:1]};
    [@"Ping Pong" drawAtPoint:NSMakePoint(14,9) withAttributes:text];
    auto* track=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(92,7,31,17) xRadius:9 yRadius:9];
    [(on?[NSColor colorWithRed:.1 green:.73 blue:.77 alpha:1]:[NSColor colorWithRed:.84 green:.89 blue:.9 alpha:1]) setFill];[track fill];
    [NSColor.whiteColor setFill];[[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(on?109:95,10,11,11)] fill];
    [(on?@"ON":@"OFF") drawAtPoint:NSMakePoint(136,9) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:11 weight:NSFontWeightBold],NSForegroundColorAttributeName:text[NSForegroundColorAttributeName]}];
    if(self.window.firstResponder==self){[[NSColor colorWithRed:.07 green:.72 blue:.78 alpha:.5] setStroke];pill.lineWidth=2;[pill stroke];}
}
@end
@interface JustDelayDocumentView : NSView
@end
@implementation JustDelayDocumentView
- (BOOL)isFlipped {return YES;}
@end
@interface JustDelayContentView : NSView {
    EditorServices services;
    std::unique_ptr<Gesture> gesture;
    NSMutableDictionary<NSNumber*,NSControl*>* controls;
    NSMutableDictionary<NSNumber*,NSTextField*>* captions;
    std::vector<std::unique_ptr<DelayRotary>> rotaries;
    NSScrollView* scroll;
    JustDelayDocumentView* document;
    NSTextField* note;
    NSButton* pingButton;
    PingPongRoute pingRoute;
    FeedbackHistory analysisHistory;
    std::uint64_t visualResumeGeneration;
    RuntimeTelemetrySnapshot presentationTelemetry;
    TelemetryAvailability presentationTelemetryAvailability;
    BOOL advanced;
}
- (instancetype)initWithServices:(EditorServices)s;
- (void)refresh;
- (void)stop;
@end
@implementation JustDelayContentView
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithServices:(EditorServices)s {
    self=[super initWithFrame:NSMakeRect(0,0,680,310)];if(!self)return nil;
    services=s;gesture=std::make_unique<Gesture>(s);
    controls=[NSMutableDictionary dictionary];captions=[NSMutableDictionary dictionary];
    self.wantsLayer=YES;self.layer.backgroundColor=NSColor.clearColor.CGColor;self.layer.cornerRadius=12;
    note=[NSTextField wrappingLabelWithString:@""];note.font=[NSFont systemFontOfSize:11];note.textColor=NSColor.secondaryLabelColor;[self addSubview:note];
    for(auto id:{timeL,feedback,mix}) {
        auto row=std::make_unique<DelayRotary>();row->id=id;row->simple=true;
        row->adapter=std::make_unique<ControlServicesAdapter>(services,id);
        auto displaySpec=spec(id);if(id==timeL)displaySpec.title="Time";
        row->control=RotaryControl::create((__bridge void*)self,row->adapter->services(),displaySpec,controlPolicy(id,false,true,services.view));
        rotaries.push_back(std::move(row));
    }
    pingButton=[[JustDelayPingButton alloc] initWithFrame:NSZeroRect];pingButton.title=@"Ping Pong  OFF";pingButton.identifier=@"just.delay.ping-pong";pingButton.accessibilityLabel=@"Ping Pong";
    pingButton.buttonType=NSButtonTypeToggle;pingButton.bezelStyle=NSBezelStyleRounded;
    pingButton.target=self;pingButton.action=@selector(togglePing:);[self addSubview:pingButton];
    scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.hasVerticalScroller=YES;scroll.hasHorizontalScroller=YES;scroll.drawsBackground=NO;
    document=[[JustDelayDocumentView alloc] initWithFrame:NSMakeRect(0,0,620,1)];
    scroll.documentView=document;[self addSubview:scroll];
    CGFloat y=18;
    for(std::size_t group=0;group<moduleDefinition().advancedGroupCount;++group) {
        const auto& g=moduleDefinition().advancedGroups[group];
        NSTextField* heading=[NSTextField labelWithString:[NSString stringWithUTF8String:g.name]];heading.font=[NSFont boldSystemFontOfSize:14];
        heading.frame=NSMakeRect(12,y,560,22);[document addSubview:heading];captions[@(10000+group)]=heading;y+=30;
        for(std::size_t j=0;j<g.count;++j) {
            auto id=g.parameters[j];const auto& p=spec(id);
            const CGFloat x=12+(j%2)*300,rowY=y+(j/2)*148;
            if(p.enumLabels) {
                NSTextField* label=[NSTextField labelWithString:[NSString stringWithUTF8String:p.title]];
                label.font=[NSFont systemFontOfSize:12];label.frame=NSMakeRect(x,rowY,280,19);[document addSubview:label];captions[@(id)]=label;
                NSPopUpButton* popup=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(x,rowY+22,270,28) pullsDown:NO];
                for(std::uint32_t k=0;k<=p.stepCount;++k)[popup addItemWithTitle:[NSString stringWithUTF8String:p.enumLabels[k]]];
                popup.tag=id;popup.target=self;popup.action=@selector(changeEnum:);[document addSubview:popup];controls[@(id)]=popup;
            } else {
                auto row=std::make_unique<DelayRotary>();row->id=id;
                row->adapter=std::make_unique<ControlServicesAdapter>(services,id);
                row->control=RotaryControl::create((__bridge void*)document,row->adapter->services(),p,controlPolicy(id,false,false,services.view));
                if(row->control)row->control->resize(int(x),int(rowY),270,136);
                rotaries.push_back(std::move(row));
            }
        }
        y+=((g.count+1)/2)*148+20;
    }
    document.frame=NSMakeRect(0,0,620,y);[self refresh];return self;
}
- (void)changeEnum:(NSPopUpButton*)sender {
    const auto& p=spec(ParamID(sender.tag));
    if(gesture->begin(p.id)){gesture->update(double(sender.indexOfSelectedItem)/p.stepCount);gesture->end();}
    [self refresh];
}
- (void)togglePing:(NSButton*)sender {
    auto state=editorTargets(services);const int next=pingRoute.next(state);
    if(gesture->begin(route)){gesture->update(double(next)/spec(route).stepCount);gesture->end();}
    [self refresh];
}
- (void)refresh {
    BOOL next=services.view->advanced;if(next!=advanced)gesture->end();advanced=next;
    auto state=editorTargets(services);pingRoute.observe(state);
    if(visualResumeGeneration!=services.view->visualResumeGeneration){analysisHistory={};presentationTelemetry={};presentationTelemetryAvailability=TelemetryAvailability::unavailable;visualResumeGeneration=services.view->visualResumeGeneration;}
    if(!services.view->visualsPaused)analysisHistory.poll(services);
    scroll.hidden=!advanced;pingButton.hidden=advanced;
    const bool pingOn=value(state,route)==2;
    pingButton.state=pingOn?NSControlStateValueOn:NSControlStateValueOff;
    pingButton.title=pingOn?@"Ping Pong   ●  ON":@"Ping Pong   ○  OFF";
    pingButton.contentTintColor=pingOn?[NSColor colorWithRed:0 green:.55 blue:.62 alpha:1]:[NSColor colorWithRed:.46 green:.57 blue:.61 alpha:1];
    pingButton.toolTip=[NSString stringWithFormat:@"Edits existing Route parameter. Off returns to %s; other sound targets stay saved.",routes[pingRoute.returnRoute()]];
    for(NSNumber* key in captions){const auto k=key.unsignedIntValue;const char* text=k>=10000?localized(*services.view,chineseGroups[k-10000],moduleDefinition().advancedGroups[k-10000].name):localized(*services.view,chineseLabel(k),spec(k).title);captions[key].stringValue=[NSString stringWithUTF8String:text];}
    for(NSNumber* key in controls) {
        const ParamID id=key.unsignedIntValue;auto* popup=(NSPopUpButton*)controls[key];
        popup.enabled=editable(id,state,advanced);
        [popup selectItemAtIndex:std::size_t(value(state,id))];
        popup.toolTip=id==crossfeed && !popup.enabled?@"Ping-Pong uses 100% cross feedback. Stored value is preserved.":@"Edits the saved host parameter.";
    }
    for(auto& row:rotaries){
        if(row->simple && row->id==timeL){const bool custom=!simpleTimeEditable(state);
            if(custom!=row->custom){
                row->control.reset();auto displaySpec=spec(timeL);displaySpec.title="Time";if(custom)displaySpec.unit="";
                row->control=RotaryControl::create((__bridge void*)self,row->adapter->services(),displaySpec,controlPolicy(timeL,custom,true,services.view));row->custom=custom;
            }
        }
        if(row->control){auto* native=(__bridge NSView*)row->control->nativeHandle();native.hidden=row->simple?advanced:!advanced;
            row->control->refresh(!native.hidden && editable(row->id,state,advanced));
            native.toolTip=row->simple && row->custom?uiText(services,"时间：自定义。请在 Advanced 编辑独立时间或同步。","Time: Custom. Edit independent times or Sync in Advanced."):uiText(services,"向上拖动增大；Shift 精调；双击复位。点击数值输入。","Drag up to increase; Shift refines; double click resets. Click the value to type.");
        }
    }
    if(!services.view->visualsPaused)presentationTelemetryAvailability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,presentationTelemetry):TelemetryAvailability::unavailable;
    const auto& telemetry=presentationTelemetry;const auto availability=presentationTelemetryAvailability;
    NSString* tempo=availability==TelemetryAvailability::fresh && (telemetry.validFields&telemetryTempo)?
        [NSString stringWithFormat:uiText(services,"宿主速度 %.1f BPM","Host tempo %.1f BPM"),telemetry.bpm]:uiText(services,"宿主速度数据不可用","Host tempo unavailable");
    if(advanced)note.stringValue=[NSString stringWithFormat:uiText(services,"Ping-Pong 将立体声输入汇入起始侧。%@。冻结不保存录音内容。","Ping-Pong sums stereo input to the start side. %@. Freeze does not save recorded audio."),tempo];
    else {
        NSString* leftTiming=[NSString stringWithUTF8String:value(state,syncL)?notes[int(value(state,noteL))]:localized(*services.view,"自由 ms","Free ms")];
        NSString* rightTiming=[NSString stringWithUTF8String:value(state,syncR)?notes[int(value(state,noteR))]:localized(*services.view,"自由 ms","Free ms")];
        NSString* timing=[NSString stringWithFormat:@"L: %@ · R: %@",leftTiming,rightTiming];
        note.stringValue=[NSString stringWithFormat:@"%@ · %@%@",timing,tempo,simpleTimeEditable(state)?@"":uiText(services," · 时间：自定义 — Advanced"," · Time: Custom — Advanced")];
    }
    const auto* measured=analysisHistory.latest();
    if(analysisHistory.availability()==AnalysisAvailability::fresh && measured && (measured->effectFields&analysisDelay))
        note.stringValue=[note.stringValue stringByAppendingFormat:uiText(services," · 实测 L/R %.1f / %.1f ms"," · Actual L/R %.1f / %.1f ms"),measured->delayMs[0],measured->delayMs[1]];
    note.toolTip=[NSString stringWithFormat:@"Saved Fallback Tempo target: %.1f BPM. Actual values are shown only when available from processing.",value(state,localTempo)];
    [self setNeedsDisplay:YES];[self setNeedsLayout:YES];
}
- (void)layout {
    [super layout];const int w=int(self.bounds.size.width),h=int(self.bounds.size.height);
    const auto geometry=PreviewLayout::fit(w,h);
    note.hidden=!advanced && geometry.note.height==0;
    note.frame=advanced?NSMakeRect(20,MAX(0,h-50),MAX(1,w-40),40):NSMakeRect(geometry.note.x,geometry.note.y,geometry.note.width,geometry.note.height);
    scroll.frame=NSMakeRect(8,8,MAX(1,w-16),MAX(1,h-66));
    if(!advanced) {
        int i=0;for(auto& row:rotaries)if(row->simple){const auto& cell=geometry.cells[i++];if(row->control)row->control->resize(cell.x,cell.y,cell.width,cell.height);}
        pingButton.frame=NSMakeRect(geometry.ping.x,geometry.ping.y,geometry.ping.width,geometry.ping.height);
    }
}
- (void)drawRect:(NSRect)dirty {
    [super drawRect:dirty];if(advanced || self.bounds.size.height<180)return;
    const auto geometry=PreviewLayout::fit(int(self.bounds.size.width),int(self.bounds.size.height));
    const auto& card=geometry.graph;
    NSRect panel=NSMakeRect(card.x,card.y,card.width,card.height);
    [[NSColor colorWithRed:.965 green:.985 blue:.991 alpha:.72] setFill];[[NSBezierPath bezierPathWithRoundedRect:panel xRadius:12 yRadius:12] fill];
    [[NSColor colorWithRed:.84 green:.9 blue:.925 alpha:1] setStroke];[[NSBezierPath bezierPathWithRoundedRect:panel xRadius:12 yRadius:12] stroke];
    const CGFloat left=NSMinX(panel)+42,right=NSMaxX(panel)-18;
    const CGFloat first=NSMinY(panel)+panel.size.height*.37,second=NSMinY(panel)+panel.size.height*.7;
    NSBezierPath* grid=[NSBezierPath bezierPath];grid.lineWidth=.6;
    for(int i=0;i<=10;++i){CGFloat x=left+(right-left)*i/10;[grid moveToPoint:NSMakePoint(x,NSMinY(panel)+30)];[grid lineToPoint:NSMakePoint(x,NSMaxY(panel)-24)];}
    for(CGFloat y:{first,second}){[grid moveToPoint:NSMakePoint(left,y)];[grid lineToPoint:NSMakePoint(right,y)];}
    [[NSColor colorWithRed:.78 green:.87 blue:.9 alpha:.7] setStroke];[grid stroke];
    NSDictionary* labels=@{NSFontAttributeName:[NSFont systemFontOfSize:10],NSForegroundColorAttributeName:[NSColor colorWithRed:.47 green:.62 blue:.66 alpha:1]};
    [uiText(services,"回声轨迹 · 实测","ECHO TRAIL · measured") drawAtPoint:NSMakePoint(NSMinX(panel)+16,NSMinY(panel)+10) withAttributes:labels];
    [@"L" drawAtPoint:NSMakePoint(NSMinX(panel)+16,first-6) withAttributes:labels];
    [@"R" drawAtPoint:NSMakePoint(NSMinX(panel)+16,second-6) withAttributes:labels];
    [@"−2500 ms" drawAtPoint:NSMakePoint(left,NSMaxY(panel)-18) withAttributes:labels];
    [uiText(services,"现在","now") drawAtPoint:NSMakePoint(right-22,NSMaxY(panel)-18) withAttributes:labels];
    const auto* newest=analysisHistory.latest();NSString* title=uiText(services,"L / R 湿声重复 · 数据不可用","L / R wet repeats · data unavailable");
    if(analysisHistory.availability()==AnalysisAvailability::stale)title=uiText(services,"L / R 湿声重复 · 暂无新音频","L / R wet repeats · no new audio");
    else if(analysisHistory.availability()==AnalysisAvailability::fresh && newest){
        if(!(newest->effectFields&analysisWet))title=uiText(services,"湿声测量不可用","Wet measurement unavailable");
        else if(!(newest->header.flags&analysisInputAligned))title=uiText(services,"等待 PDC 对齐","Waiting for PDC alignment");
        else if(newest->header.flags&analysisBypassed)title=uiText(services,"实测 L / R 湿声重复 · 旁通","Actual L / R wet repeats · bypass");
        else if(newest->header.flags&analysisTransportKnown && !(newest->header.flags&analysisPlaying))title=uiText(services,"实测 L / R 湿声重复 · 宿主停止","Actual L / R wet repeats · host stopped");
        else if(std::max(newest->channels[0].peak,newest->channels[1].peak)==0){
            bool historyHasSignal=false;analysisHistory.each([&](const AnalysisWindow& item){for(unsigned ch:{0u,1u,4u,5u})historyHasSignal=historyHasSignal || item.channels[ch].peak>0;});
            title=std::max(newest->channels[4].peak,newest->channels[5].peak)>0?uiText(services,"输入静音 · 回声仍在继续","Input silent · repeats still active"):historyHasSignal?uiText(services,"实测 L / R 湿声重复 · 历史（当前静音）","Actual L / R wet repeats · history (now silent)"):uiText(services,"实测 L / R 湿声重复 · 静音","Actual L / R wet repeats · silence");
        }
        else title=uiText(services,"实测 L / R 湿声重复 · 输入 / 湿声峰值","Actual L / R wet repeats · input / wet peaks");
    }
    [title drawInRect:NSMakeRect(NSMinX(panel)+180,NSMinY(panel)+10,MAX(1,panel.size.width-196),14) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:9],NSForegroundColorAttributeName:[NSColor colorWithRed:.47 green:.62 blue:.66 alpha:1]}];
    if(newest && analysisHistory.availability()==AnalysisAvailability::fresh) {
        const double end=double(newest->header.endSample),span=newest->header.sampleRate*2.5;
        const CGFloat amplitude=std::max(4.,std::min(33.,panel.size.height*.13));
        for(unsigned lane=0;lane<2;++lane)for(unsigned series=0;series<2;++series){
            auto* path=[NSBezierPath bezierPath];path.lineWidth=series?3:2;path.lineCapStyle=NSLineCapStyleRound;
            analysisHistory.each([&](const AnalysisWindow& item){const auto& header=item.header;
                if(!(header.flags&analysisInputAligned) || (series && !(item.effectFields&analysisWet)) || (lane && header.outputChannels<2))return;
                const double x=right-(end-double(header.endSample))/span*(right-left);if(x<left || x>right)return;
                const unsigned channel=series?4+lane:(header.inputChannels==1?0:lane);
                const double peak=std::clamp(item.channels[channel].peak,0.,1.);if(peak<=1e-8)return;
                const CGFloat y=lane?second:first,a=peak*amplitude;
                [path moveToPoint:NSMakePoint(x,y-a)];[path lineToPoint:NSMakePoint(x,y+a)];
            });
            [(series?[NSColor colorWithRed:.07 green:.72 blue:.78 alpha:.95]:[NSColor colorWithRed:.4 green:.58 blue:.63 alpha:.8]) setStroke];[path stroke];
        }
    }
}
- (void)stop {gesture->end();rotaries.clear();}
@end
namespace just::delay {
class MacContent final:public EditorContent {
    JustDelayContentView* view=nil;
public:
    ~MacContent() override {[view stop];[view removeFromSuperview];view=nil;}
    bool attach(void* parent,const EditorServices& s) override {view=[[JustDelayContentView alloc] initWithServices:s];[(__bridge NSView*)parent addSubview:view];return view!=nil;}
    void resize(int w,int h) override {view.frame=NSMakeRect(0,0,w,h);[view layout];[view setNeedsDisplay:YES];}
    void refresh(const EditorViewState&,const StatusSnapshot&) override {[view refresh];}
};
EditorContent* createEditorContent(){return new MacContent;}
}
