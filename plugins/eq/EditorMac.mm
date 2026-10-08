#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "EditorModel.hpp"
#include "EditorLayout.hpp"
#include "PanelPlacement.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/PresentationClock.hpp"
#include <chrono>
using namespace just::eq;
static double monotonicMilliseconds() {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static just::DisplayPolicy panelDisplayPolicy() {just::DisplayPolicy p;p.dark=true;p.valueFontSize=20;p.labelFontSize=13;p.valueFieldHeight=28;return p;}
static void panelFrequencyFormat(double normalized,just::DisplayContext context,char* text,std::size_t size) {
    const double hz=parameters[index(0,frequency)].toPhysical(normalized);
    std::snprintf(text,size,context==just::DisplayContext::editing?"%.17g":"%.0f",hz);
}
@class JustEqCanvas;
struct PanelEditBinding {
    just::EditorServices services{};
    just::ParamID parameter=0;
    __weak JustEqCanvas* canvas=nil;
    bool panelControl=false;
};
static NSColor* fieldColor(int f) {auto c=fieldStyles[f].rgb;return [NSColor colorWithSRGBRed:((c>>16)&255)/255.0 green:((c>>8)&255)/255.0 blue:(c&255)/255.0 alpha:1];}
static NSTextField* eqLabel(NSString* text) {auto* l=[NSTextField labelWithString:text];l.font=[NSFont systemFontOfSize:10];return l;}
@interface JustEqSoloButton:NSButton
@end
@implementation JustEqSoloButton
- (void)mouseDown:(NSEvent*)event {if(self.enabled)[self.target performSelector:@selector(startSolo:) withObject:event];}
- (void)mouseDragged:(NSEvent*)event {[self.target performSelector:@selector(sweepSolo:) withObject:event];}
- (void)mouseUp:(NSEvent*)event {[self.target performSelector:@selector(stopSolo:) withObject:self];}
- (void)resetCursorRects {[self addCursorRect:self.bounds cursor:NSCursor.resizeLeftRightCursor];}
@end
@interface JustEqSelectedPanel:NSView {
    NSTrackingArea* tracking;
    double opacity;
    BOOL visualsPaused;
}
@property BOOL hovered;
@property BOOL held;
@property BOOL nodeCaptured;
- (void)refreshOpacity;
- (void)setVisualsPaused:(BOOL)paused;
@end
@implementation JustEqSelectedPanel
- (BOOL)isFlipped {return YES;}
- (NSView*)hitTest:(NSPoint)point {return self.hidden || self.nodeCaptured?nil:[super hitTest:point];}
- (instancetype)initWithFrame:(NSRect)frame {self=[super initWithFrame:frame];if(self){opacity=.72;self.wantsLayer=YES;self.layer.cornerRadius=12;self.layer.borderWidth=1;self.layer.borderColor=[NSColor colorWithSRGBRed:.21 green:.45 blue:.51 alpha:1].CGColor;}return self;}
- (void)updateTrackingAreas {[super updateTrackingAreas];if(tracking)[self removeTrackingArea:tracking];tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];[self addTrackingArea:tracking];}
- (void)mouseEntered:(NSEvent*)event {self.hovered=YES;[self refreshOpacity];}
- (void)mouseExited:(NSEvent*)event {self.hovered=NO;[self refreshOpacity];}
- (void)setVisualsPaused:(BOOL)paused {
    if(visualsPaused==paused)return;visualsPaused=paused;
    // Preserve the in-flight hover transition without changing the control
    // event tree. Layer time is presentation-only, never the Solo lease clock.
    [CATransaction begin];[CATransaction setDisableActions:YES];
    if(paused){const auto stopped=[self.layer convertTime:CACurrentMediaTime() fromLayer:nil];self.layer.speed=0;self.layer.timeOffset=stopped;}
    else {const auto stopped=self.layer.timeOffset;self.layer.speed=1;self.layer.timeOffset=0;self.layer.beginTime=0;self.layer.beginTime=[self.layer convertTime:CACurrentMediaTime() fromLayer:nil]-stopped;}
    [CATransaction commit];if(!paused)[self refreshOpacity];
}
- (void)refreshOpacity {
    if(visualsPaused)return;
    NSResponder* responder=self.window.firstResponder;BOOL focus=NO;
    if([responder isKindOfClass:NSView.class])focus=[(NSView*)responder isDescendantOf:self];
    if([responder isKindOfClass:NSTextView.class] && [(NSTextView*)responder isFieldEditor]) {NSObject* delegate=[(NSTextView*)responder delegate];if([delegate isKindOfClass:NSView.class])focus|=[(NSView*)delegate isDescendantOf:self];}
    if(self.window.isKeyWindow)self.hovered=NSPointInRect([self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil],self.bounds);
    else self.hovered=NO;
    double next=(self.hovered || (focus && self.window.isKeyWindow) || self.held)?1:.72;
    if(self.layer.backgroundColor && next==opacity)return;
    auto previous=self.layer.presentationLayer.backgroundColor?:self.layer.backgroundColor;
    auto nextColor=[NSColor colorWithSRGBRed:15./255 green:60./255 blue:73./255 alpha:next].CGColor;
    if(previous){auto* fade=[CABasicAnimation animationWithKeyPath:@"backgroundColor"];fade.fromValue=(__bridge NSObject*)previous;fade.toValue=(__bridge NSObject*)nextColor;fade.duration=.16;fade.timingFunction=[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];[self.layer addAnimation:fade forKey:@"EQ.panel.opacity"];}
    self.layer.backgroundColor=nextColor;opacity=next;
}
- (void)scrollWheel:(NSEvent*)event {[self.superview scrollWheel:event];}
@end
@interface JustEqControlCell:NSView
@property BOOL protectsReadout;
@end
@implementation JustEqControlCell
- (BOOL)isFlipped{return YES;}
- (void)drawRect:(NSRect)dirty {if(self.protectsReadout){auto g=just::ControlGeometry::layout(self.bounds.size.width,self.bounds.size.height,panelDisplayPolicy());[[NSColor colorWithSRGBRed:15./255 green:60./255 blue:73./255 alpha:.94] setFill];[[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0,g.valueTop,self.bounds.size.width,g.valueHeight) xRadius:4 yRadius:4] fill];}}
@end
@interface JustEqCanvas:NSView {
@public
    EditorModel model;
    JustEqSelectedPanel* selectedPanel;
    NSTextField *selectedTitle,*compactSummary,*wheelHint,*slopeTitle,*slopeUnit;
    NSButton *enabledButton,*soloButton,*closeButton,*deleteButton,*formButton;
    NSPopUpButton *field,*shape,*cutSlope,*analyzer,*displayRange;
    NSMutableArray<NSView*>* details;
    NSMutableArray<NSPopUpButton*>* enums;
    NSMutableArray<JustEqControlCell*>* cells;
    // Rotary controls retain pointers into this storage; destroy controls first.
    std::vector<std::unique_ptr<PanelEditBinding>> controlBindings;
    std::vector<std::unique_ptr<just::RotaryControl>> rotary;
    PanelPresentation presentation;
    just::PresentationClock presentationClock;
    PanelPlacement placement;
    NSUInteger activeControlGestures;
    NSMutableSet<NSMenu*>* trackingMenus;
    NSMenu* contextMenu;
    std::size_t controlsBand;
    NSTimer* soloTimer;
    NSObject* wheelMonitor;
    BOOL soloHeld,dragging;
    NSPoint lastDrag;
    double dragHz,dragDb,soloLastX;
    NSRect graph;
    double rangeDb;
    int viewportHeight;
}
- (void)sync;
- (void)cancel;
- (void)showSelection;
- (void)updatePresentation;
- (double)panelNow;
- (void)setVisualsPaused:(BOOL)paused;
- (BOOL)canBeginPanelControl;
- (void)beginPanelControl;
- (void)endPanelControl;
- (NSPoint)selectedNode;
- (NSRect)selectedBadge;
- (NSRect)modeBadge:(NSPoint)p field:(int)field;
@end
@implementation JustEqCanvas
- (BOOL)isFlipped {return YES;}
- (BOOL)acceptsFirstResponder {return YES;}
- (instancetype)init {
    self=[super initWithFrame:NSMakeRect(0,0,680,460)];if(!self)return nil;
    controlsBand=bandCount;rangeDb=18;viewportHeight=460;
    details=[NSMutableArray array];enums=[NSMutableArray array];cells=[NSMutableArray array];
    trackingMenus=[NSMutableSet set];
    selectedPanel=[[JustEqSelectedPanel alloc] initWithFrame:NSZeroRect];selectedPanel.identifier=@"JustEQ.SelectedPanel";selectedPanel.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];[self addSubview:selectedPanel];
    selectedTitle=eqLabel(@"BAND 01");selectedTitle.font=[NSFont systemFontOfSize:10 weight:NSFontWeightBold];selectedTitle.textColor=[NSColor colorWithSRGBRed:.51 green:.89 blue:.92 alpha:1];[selectedPanel addSubview:selectedTitle];
    compactSummary=eqLabel(@"");compactSummary.font=[NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightMedium];compactSummary.textColor=NSColor.whiteColor;compactSummary.drawsBackground=YES;compactSummary.backgroundColor=[NSColor colorWithSRGBRed:15./255 green:60./255 blue:73./255 alpha:.94];compactSummary.identifier=@"JustEQ.CompactSummary";[selectedPanel addSubview:compactSummary];
    formButton=[NSButton buttonWithTitle:@"⌄" target:self action:@selector(togglePanelForm:)];formButton.bordered=NO;formButton.identifier=@"JustEQ.TogglePanelForm";[selectedPanel addSubview:formButton];
    shape=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    for(auto s:{"Bell","Low Shelf","High Shelf","Low Cut","High Cut","Notch"})[shape addItemWithTitle:[NSString stringWithUTF8String:s]];
    shape.target=self;shape.action=@selector(changeShape:);shape.identifier=@"JustEQ.Shape";shape.font=[NSFont systemFontOfSize:10];[selectedPanel addSubview:shape];
    field=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(auto s:targetLabels)[field addItemWithTitle:[NSString stringWithUTF8String:s]];
    field.target=self;field.action=@selector(changeField:);field.identifier=@"JustEQ.SelectedBandField";field.font=[NSFont systemFontOfSize:10];field.toolTip=@"Selected band only. Side is inactive on mono input.";[selectedPanel addSubview:field];
    closeButton=[NSButton buttonWithTitle:@"×" target:self action:@selector(closePanel:)];closeButton.bordered=NO;closeButton.identifier=@"JustEQ.ClosePanel";[selectedPanel addSubview:closeButton];
    soloButton=[JustEqSoloButton buttonWithTitle:@"Hold SOLO" target:self action:nil];soloButton.identifier=@"JustEQ.Solo";soloButton.font=[NSFont systemFontOfSize:10];soloButton.toolTip=@"Hold and drag horizontally to sweep the incoming frequency region. Cuts audition the removed side. Release, focus loss, close or lease timeout restores EQ.";[selectedPanel addSubview:soloButton];
    wheelHint=eqLabel(@"Wheel: Q");wheelHint.alignment=NSTextAlignmentCenter;[selectedPanel addSubview:wheelHint];
    deleteButton=[NSButton buttonWithTitle:@"Delete band" target:self action:@selector(deleteBand:)];deleteButton.font=[NSFont systemFontOfSize:9];deleteButton.bordered=NO;deleteButton.identifier=@"JustEQ.DeleteBand";[selectedPanel addSubview:deleteButton];
    cutSlope=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(auto s:slopeChoiceLabels)[cutSlope addItemWithTitle:[NSString stringWithUTF8String:s]];cutSlope.target=self;cutSlope.action=@selector(changeSlope:);cutSlope.identifier=@"JustEQ.Slope";cutSlope.toolTip=@"6, 12, 18, 24, 36, 48, 72, 96 dB/oct. Existing project slopes retain their original automation IDs.";[selectedPanel addSubview:cutSlope];
    slopeTitle=eqLabel(@"Slope");slopeUnit=eqLabel(@"dB/oct");slopeTitle.alignment=slopeUnit.alignment=NSTextAlignmentCenter;[selectedPanel addSubview:slopeTitle];[selectedPanel addSubview:slopeUnit];
    analyzer=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];[analyzer addItemWithTitle:@"Analyzer"];analyzer.identifier=@"JustEQ.Analyzer";analyzer.font=[NSFont systemFontOfSize:11];analyzer.bordered=NO;analyzer.menu.autoenablesItems=NO;
    NSArray<NSString*>* analyzerGroups=@[@"Source",@"Range",@"Tilt",@"Resolution",@"Response"];
    NSArray<NSArray<NSString*>*>* analyzerChoices=@[@[@"Pre",@"Post",@"Pre + Post"],@[@"60 dB",@"90 dB",@"120 dB"],@[@"0 dB/oct · measurement",@"4.5 dB/oct · 1 kHz pivot"],@[@"4096 · default",@"8192 · fine"],@[@"Fast",@"Medium",@"Slow"]];
    for(unsigned group=0;group<5;++group){auto* root=[[NSMenuItem alloc] initWithTitle:analyzerGroups[group] action:nil keyEquivalent:@""];root.tag=(group+1)*100;root.submenu=[[NSMenu alloc] initWithTitle:root.title];root.submenu.autoenablesItems=NO;
        for(unsigned choice=0;choice<analyzerChoices[group].count;++choice){auto* item=[[NSMenuItem alloc] initWithTitle:analyzerChoices[group][choice] action:@selector(changeAnalyzer:) keyEquivalent:@""];item.target=self;item.tag=root.tag+choice;[root.submenu addItem:item];}[analyzer.menu addItem:root];
    }
    [self addSubview:analyzer];
    displayRange=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];[displayRange addItemsWithTitles:@[@"±24 dB",@"±18 dB",@"±12 dB",@"±6 dB"]];[displayRange selectItemAtIndex:1];displayRange.target=self;displayRange.action=@selector(changeRange:);displayRange.identifier=@"JustEQ.DisplayRange";[self addSubview:displayRange];
    enabledButton=[NSButton checkboxWithTitle:@"Band on" target:self action:@selector(changeEnabled:)];[self addSubview:enabledButton];
    struct Item {const char* title;int tag;const char* const* labels;int count;};
    for(auto item:{Item{"Dynamic",dynamicEnabled,dynamic_enabledLabels,2},Item{"Detector",detector,detectorLabels,2},Item{"Source",source,sourceLabels,2}}){auto* l=eqLabel([NSString stringWithUTF8String:item.title]);auto* p=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(int i=0;i<item.count;++i)[p addItemWithTitle:[NSString stringWithUTF8String:item.labels[i]]];p.tag=item.tag;p.target=self;p.action=@selector(changeEnum:);[details addObject:l];[details addObject:p];[enums addObject:p];[self addSubview:l];[self addSubview:p];}
    return self;
}
- (void)buildControls {
    if(controlsBand==model.selected)return;
    rotary.clear();controlBindings.clear();for(NSView* v in cells)[v removeFromSuperview];[cells removeAllObjects];controlsBand=model.selected;
    const int tags[]={frequency,gain,q,range,threshold,knee,attack,release,1001,1002};
    const char* titles[]={"Frequency","Gain","Q","Range","Threshold","Knee","Attack","Release","Input","Output"};
    const char* zh[]={"频率","增益","Q","范围","阈值","拐点","起音","释放","输入","输出"};
    for(int i=0;i<10;++i){auto* cell=[[JustEqControlCell alloc] initWithFrame:NSZeroRect];cell.protectsReadout=i<3;[cells addObject:cell];[(i<3?selectedPanel:self) addSubview:cell];auto idx=tags[i]>=1000?std::size_t(tags[i]-1000):index(model.selected,Field(tags[i]));auto spec=parameters[idx];spec.title=titles[i];just::DisplayPolicy policy=i<3?panelDisplayPolicy():just::DisplayPolicy{};policy.labelZh=zh[i];if(i==0)policy.format=panelFrequencyFormat;if(i==2){policy.decimals=2;spec.unit="Q";}
        auto context=std::make_unique<PanelEditBinding>();context->services=model.editorServices();context->parameter=spec.id;context->canvas=self;context->panelControl=i<3;
        just::RotaryBinding binding;binding.owner=context.get();binding.view=context->services.view;
        binding.read=[](void* p){auto& c=*static_cast<PanelEditBinding*>(p);return c.services.readTarget?c.services.readTarget(c.services.owner,c.parameter):0.;};
        binding.begin=[](void* p){auto& c=*static_cast<PanelEditBinding*>(p);if(c.panelControl && ![c.canvas canBeginPanelControl])return false;
            bool began=c.services.beginEdit && c.services.beginEdit(c.services.owner,c.parameter);if(began && c.panelControl)[c.canvas beginPanelControl];return began;};
        binding.write=[](void* p,double value){auto& c=*static_cast<PanelEditBinding*>(p);return c.services.performEdit && c.services.performEdit(c.services.owner,c.parameter,value);};
        binding.end=[](void* p){auto& c=*static_cast<PanelEditBinding*>(p);if(c.services.endEdit)c.services.endEdit(c.services.owner,c.parameter);if(c.panelControl)[c.canvas endPanelControl];};
        auto control=just::RotaryControl::create((__bridge void*)cell,binding,spec,policy);
        if(control){auto* native=(__bridge NSView*)control->nativeHandle();native.identifier=[NSString stringWithFormat:@"JustEQ.Control.%u",spec.id];if(i<3)native.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];}
        controlBindings.push_back(std::move(context));rotary.push_back(std::move(control));
    }
}
- (void)layout {
    [super layout];double w=self.bounds.size.width;
    const double mainHeight=mainViewHeight(viewportHeight,model.advanced);
    graph=NSMakeRect(40,20,MAX(280,w-92),mainHeight-52);
    auto point=[self selectedNode];NSRect reserved=NSMakeRect(point.x-12,point.y-12,24,24);
    if(int(model.value(target)))reserved=NSUnionRect(reserved,[self modeBadge:point field:int(model.value(target))]);
    reserved=NSInsetRect(reserved,-PanelPlacement::nodeClearance,-PanelPlacement::nodeClearance);
    reserved=NSUnionRect(reserved,[self selectedBadge]);
    auto placed=placement.place({graph.origin.x,graph.origin.y,graph.size.width,graph.size.height},point.x,
        {reserved.origin.x,reserved.origin.y,reserved.size.width,reserved.size.height},dragging,presentation.layoutLocked(),presentation.choice());
    presentation.automaticForm(placed.form);
    const BOOL compact=placed.form==PanelPresentation::Form::compact;
    selectedPanel.hidden=!placed.available || !presentation.acceptsInput([self panelNow]);
    if(placed.available)selectedPanel.frame=NSMakeRect(placed.rect.x,placed.rect.y,placed.rect.w,placed.rect.h);
    selectedPanel.toolTip=placed.temporarilyCoversNode?[self word:"手动展开：可能暂时覆盖所选节点；可收起" english:"Manually expanded: may temporarily cover the selected node; collapse to clear it"]:model.isCut()?[self word:"滚轮调整斜率，单位 dB/oct" english:"Wheel adjusts slope in dB/oct"]:[self word:"滚轮调整 Q" english:"Wheel adjusts Q"];
    selectedTitle.frame=compact?NSMakeRect(12,7,204,18):NSMakeRect(12,9,72,18);
    selectedTitle.font=[NSFont systemFontOfSize:compact?12:13 weight:NSFontWeightSemibold];
    if(compact)selectedTitle.stringValue=[NSString stringWithFormat:@"%@ %zu · %@ · %@",[self word:"频段" english:"BAND"],model.selected+1,shape.titleOfSelectedItem,field.titleOfSelectedItem];
    shape.frame=NSMakeRect(88,4,105,28);field.frame=NSMakeRect(372,4,94,28);
    shape.font=field.font=[NSFont systemFontOfSize:13];
    closeButton.frame=compact?NSMakeRect(226,29,24,22):NSMakeRect(472,44,24,24);
    formButton.frame=compact?NSMakeRect(226,4,24,24):NSMakeRect(472,5,24,24);
    formButton.title=compact?@"↗":@"⌄";formButton.toolTip=compact?[self word:"展开频段控制" english:"Expand band controls"]:[self word:"收起频段控制" english:"Collapse band controls"];
    compactSummary.frame=NSMakeRect(12,29,208,20);compactSummary.hidden=!compact;
    shape.hidden=field.hidden=soloButton.hidden=deleteButton.hidden=compact;wheelHint.hidden=YES;
    for(int i=0;i<3 && i<int(cells.count);++i){auto* cell=cells[i];cell.hidden=compact || (i==1 && model.isCut());cell.frame=NSMakeRect(12+i*120,32,112,96);cell.bounds=NSMakeRect(0,0,112,96);if(rotary[i])rotary[i]->resize(0,0,112,96);}
    soloButton.frame=NSMakeRect(372,76,112,28);deleteButton.frame=NSMakeRect(372,106,112,22);
    soloButton.font=[NSFont systemFontOfSize:13];wheelHint.font=[NSFont systemFontOfSize:11];deleteButton.font=[NSFont systemFontOfSize:11];
    slopeTitle.frame=NSMakeRect(132,32,112,20);cutSlope.frame=NSMakeRect(140,59,96,30);slopeUnit.frame=NSMakeRect(132,103,112,22);
    slopeTitle.font=slopeUnit.font=[NSFont systemFontOfSize:13];cutSlope.font=[NSFont systemFontOfSize:20];
    slopeTitle.hidden=cutSlope.hidden=slopeUnit.hidden=compact || !model.isCut();
    analyzer.frame=NSMakeRect(NSMaxX(graph)-132,0,124,18);displayRange.frame=NSMakeRect(180,mainHeight+10,116,28);enabledButton.frame=NSMakeRect(316,mainHeight+10,100,28);
    int columns=w<600?3:4;double cellWidth=(w-32)/columns;
    for(NSUInteger i=0;i<details.count/2;++i){details[2*i].frame=NSMakeRect(18+i*cellWidth,mainHeight+53,cellWidth-12,18);details[2*i+1].frame=NSMakeRect(18+i*cellWidth,mainHeight+73,cellWidth-12,30);}
    for(int i=3;i<int(cells.count);++i){cells[i].frame=NSMakeRect(16+((i-3)%columns)*cellWidth,mainHeight+124+((i-3)/columns)*150,cellWidth-10,140);cells[i].bounds=NSMakeRect(0,0,cellWidth-10,140);if(rotary[i])rotary[i]->resize(0,0,int(cellWidth-10),140);}
}
- (NSString*)word:(const char*)zh english:(const char*)en {const auto* view=model.editorServices().view;return [NSString stringWithUTF8String:view?just::localized(*view,zh,en):en];}
- (double)panelNow {return presentationClock.now(monotonicMilliseconds());}
- (void)setVisualsPaused:(BOOL)paused {
    presentationClock.setPaused(paused,monotonicMilliseconds());
    [selectedPanel setVisualsPaused:paused];[self updatePresentation];
}
- (void)showSelection {presentation.select([self panelNow]);placement.reset();[self updatePresentation];}
- (BOOL)canBeginPanelControl {return model.panelVisible && !dragging && presentation.acceptsInput([self panelNow]);}
- (void)beginPanelControl {++activeControlGestures;presentation.activity(PanelPresentation::Activity::knobDrag,true,[self panelNow]);[self updatePresentation];}
- (void)endPanelControl {if(activeControlGestures)--activeControlGestures;if(!activeControlGestures)presentation.activity(PanelPresentation::Activity::knobDrag,false,[self panelNow]);[self updatePresentation];}
- (void)updatePresentation {
    if(!model.panelVisible)presentation.hide();
    const double now=[self panelNow];NSResponder* responder=self.window.firstResponder;BOOL panelTextResponder=NO;
    if([responder isKindOfClass:NSTextView.class] && [(NSTextView*)responder isFieldEditor]) {
        NSObject* delegate=[(NSTextView*)responder delegate];panelTextResponder=[self ownsPanelControl:delegate];
    }
    presentation.activity(PanelPresentation::Activity::textFocus,panelTextResponder && self.window.isKeyWindow,now);
    BOOL hidden=!presentation.acceptsInput(now);
    if(hidden && (panelTextResponder || ([responder isKindOfClass:NSView.class] && [(NSView*)responder isDescendantOf:selectedPanel])))[self.window makeFirstResponder:self];
    // The whole card fades, while hover animates only its background. Hidden
    // controls leave the native hit/focus tree and cannot be woken by hover.
    selectedPanel.hidden=hidden;selectedPanel.alphaValue=presentation.fade(now);
    selectedPanel.nodeCaptured=dragging;
    selectedPanel.held=presentation.layoutLocked() || dragging;
    [selectedPanel refreshOpacity];
}
- (BOOL)ownsPanelControl:(NSObject*)object {return [object isKindOfClass:NSView.class] && [(NSView*)object isDescendantOf:selectedPanel];}
- (void)textBegan:(NSNotification*)notice {if([self ownsPanelControl:notice.object]){presentation.activity(PanelPresentation::Activity::textFocus,true,[self panelNow]);[self updatePresentation];}}
- (void)textEnded:(NSNotification*)notice {if([self ownsPanelControl:notice.object]){presentation.activity(PanelPresentation::Activity::textFocus,false,[self panelNow]);[self updatePresentation];}}
- (BOOL)ownsPanelMenu:(NSMenu*)menu {
    for(NSMenu* m=menu;m;m=m.supermenu)if(m==shape.menu || m==field.menu || m==cutSlope.menu || m==contextMenu)return YES;
    return NO;
}
- (void)menuBegan:(NSNotification*)notice {if([self ownsPanelMenu:notice.object]){[trackingMenus addObject:notice.object];presentation.activity(PanelPresentation::Activity::menu,true,[self panelNow]);[self updatePresentation];}}
- (void)menuEnded:(NSNotification*)notice {if([trackingMenus containsObject:notice.object]){[trackingMenus removeObject:notice.object];if(!trackingMenus.count)presentation.activity(PanelPresentation::Activity::menu,false,[self panelNow]);[self updatePresentation];}}
- (void)sync {
    model.refresh();[self buildControls];
    soloButton.enabled=model.canSolo();soloButton.title=model.soloToken?(model.soloStatus.phase==just::AuditionPhase::active?@"Listening":@"Pending"):@"Hold SOLO";
    if(soloHeld && !model.soloToken)[self stopSolo:nil];
    selectedTitle.stringValue=[NSString stringWithFormat:@"● %@ %zu",[self word:"频段" english:"BAND"],model.selected+1];
    [field selectItemAtIndex:int(model.value(target))];[shape selectItemAtIndex:int(model.value(type))];[cutSlope selectItemAtIndex:model.slopeChoice()];
    BOOL cut=model.isCut(),hasGain=int(model.value(type))<=2;
    cutSlope.hidden=slopeTitle.hidden=slopeUnit.hidden=!cut;wheelHint.stringValue=cut?[self word:"滚轮：斜率" english:"Wheel: slope"]:[self word:"滚轮：Q" english:"Wheel: Q"];
    for(int i=0;i<int(cells.count);++i){cells[i].hidden=i<3?(i==1 && cut):!model.advanced;if(rotary[i])rotary[i]->refresh(i==1?hasGain:(i>=3 && i<=7?hasGain:true));}
    for(NSView* v in details)v.hidden=!model.advanced;
    for(NSPopUpButton* p in enums){[p selectItemAtIndex:int(model.value(Field(p.tag)))];p.enabled=hasGain;}
    analyzer.hidden=NO;displayRange.hidden=enabledButton.hidden=!model.advanced;enabledButton.state=model.value(enabled)>.5?NSControlStateValueOn:NSControlStateValueOff;
    const auto& analyzerSettings=model.analyzer.settings;
    const int analyzerSelections[]={int(analyzerSettings.source),int((analyzerSettings.rangeDb-60)/30),analyzerSettings.tiltDbPerOctave>0?1:0,analyzerSettings.fftSize==8192?1:0,int(analyzerSettings.response)};
    [analyzer itemAtIndex:0].title=[self word:"频谱分析" english:"Analyzer"];
    const char* groupZh[]={"来源","范围","倾斜","分辨率","响应"};const char* groupEn[]={"Source","Range","Tilt","Resolution","Response"};
    for(unsigned group=0;group<5;++group){auto* root=[analyzer.menu itemWithTag:(group+1)*100];root.title=[self word:groupZh[group] english:groupEn[group]];for(NSMenuItem* item in root.submenu.itemArray)item.state=item.tag%100==analyzerSelections[group]?NSControlStateValueOn:NSControlStateValueOff;}
    analyzer.toolTip=[NSString stringWithFormat:@"%d dB range · %d samples · %.1f dB/oct tilt (1 kHz pivot). Display preferences only. 0 tilt shows calibrated dBFS; Hann off-bin tones may read up to 1.42 dB lower.",analyzerSettings.rangeDb,analyzerSettings.fftSize,analyzerSettings.tiltDbPerOctave];
    deleteButton.title=[self word:"删除频段" english:"Delete band"];enabledButton.title=[self word:"频段开启" english:"Band on"];slopeTitle.stringValue=[self word:"斜率" english:"Slope"];
    if(!model.soloToken)soloButton.title=[self word:"按住 SOLO" english:"Hold SOLO"];
    const char* shapeZh[]={"钟形","低架","高架","低切","高切","陷波"};const char* shapeEn[]={"Bell","Low Shelf","High Shelf","Low Cut","High Cut","Notch"};
    for(int i=0;i<6;++i)[shape itemAtIndex:i].title=[self word:shapeZh[i] english:shapeEn[i]];
    const char* fieldZh[]={"立体声","中置 Mid","侧边 Side"};for(int i=0;i<3;++i)[field itemAtIndex:i].title=[self word:fieldZh[i] english:targetLabels[i]];
    double hz=model.value(frequency);NSString* frequencyText=hz>=1000?[NSString stringWithFormat:@"%.2fk",hz/1000]:[NSString stringWithFormat:@"%.0f Hz",hz];
    compactSummary.stringValue=model.isCut()?[NSString stringWithFormat:@"%@ · %s dB/oct · Q %.2f",frequencyText,slopeChoiceLabels[model.slopeChoice()],model.value(q)]:[NSString stringWithFormat:@"%@ · %+.1f dB · Q %.2f",frequencyText,model.value(gain),model.value(q)];
    [self updatePresentation];[self layout];self.needsDisplay=YES;
}
- (void)startSolo:(NSEvent*)event {
    if(!self.window.isKeyWindow)return;[self.window makeFirstResponder:self];[self cancel];soloHeld=model.beginSolo();
    if(soloHeld){presentation.activity(PanelPresentation::Activity::solo,true,[self panelNow]);model.beginSoloSweep();soloLastX=event.locationInWindow.x;dragHz=model.value(frequency);soloTimer=[NSTimer timerWithTimeInterval:.075 target:self selector:@selector(renewSolo:) userInfo:nil repeats:YES];[[NSRunLoop mainRunLoop] addTimer:soloTimer forMode:NSRunLoopCommonModes];}
    [self sync];
}
- (void)sweepSolo:(NSEvent*)event {if(!soloHeld)return;double amount=(event.modifierFlags&NSEventModifierFlagShift)?.1:1;dragHz=std::clamp(dragHz*std::pow(2.,(event.locationInWindow.x-soloLastX)*amount/120),20.,20000.);soloLastX=event.locationInWindow.x;model.sweepSolo(dragHz);[self sync];}
- (void)renewSolo:(NSTimer*)timer {if(!soloHeld || !self.window.isKeyWindow || !(NSEvent.pressedMouseButtons&1) || !model.renewSolo())[self stopSolo:nil];}
- (void)stopSolo:(NSObject*)sender {[soloTimer invalidate];soloTimer=nil;soloHeld=NO;model.endSolo();model.cancelDrag();presentation.activity(PanelPresentation::Activity::solo,false,[self panelNow]);[self updatePresentation];soloButton.title=@"Hold SOLO";self.needsDisplay=YES;}
- (void)changeField:(NSObject*)sender {[self cancel];model.write(target,field.indexOfSelectedItem);[self sync];}
- (void)changeShape:(NSObject*)sender {[self cancel];model.write(type,shape.indexOfSelectedItem);[self sync];}
- (void)changeSlope:(NSObject*)sender {[self cancel];model.writeSlopeChoice(int(cutSlope.indexOfSelectedItem));[self sync];}
- (void)changeEnabled:(NSObject*)sender {[self cancel];model.write(enabled,enabledButton.state==NSControlStateValueOn?1:0);[self sync];}
- (void)changeEnum:(NSPopUpButton*)sender {model.write(Field(sender.tag),sender.indexOfSelectedItem);[self sync];}
- (void)changeAnalyzer:(NSMenuItem*)sender {
    auto settings=model.analyzer.settings;const int choice=int(sender.tag%100);
    switch(sender.tag/100){case 1:settings.source=just::AnalyzerSource(choice);break;case 2:settings.rangeDb=60+30*choice;break;case 3:settings.tiltDbPerOctave=choice?4.5:0;break;case 4:settings.fftSize=choice?8192:4096;break;case 5:settings.response=just::AnalyzerResponse(choice);break;default:return;}
    model.configureAnalyzer(settings);[self sync];
}
- (void)changeRange:(NSObject*)sender {const double ranges[]={24,18,12,6};rangeDb=ranges[displayRange.indexOfSelectedItem];self.needsDisplay=YES;}
- (void)addBand:(NSObject*)sender {[self.window makeFirstResponder:self];[self cancel];model.create(1000);if(model.panelVisible)[self showSelection];[self sync];}
- (void)togglePanelForm:(NSObject*)sender {presentation.choose(presentation.form()==PanelPresentation::Form::full?PanelPresentation::Form::compact:PanelPresentation::Form::full,[self panelNow]);[self sync];}
- (void)closePanel:(NSObject*)sender {[self.window makeFirstResponder:self];[self cancel];model.hidePanel();[self sync];}
- (void)deleteBand:(NSObject*)sender {[self.window makeFirstResponder:self];[self cancel];model.write(enabled,0);model.hidePanel();[self sync];}
- (double)xFor:(double)hz {return graph.origin.x+std::log(hz/20)/std::log(1000)*graph.size.width;}
- (double)yFor:(double)db {return NSMidY(graph)-std::clamp(db,-rangeDb,rangeDb)/(2*rangeDb)*graph.size.height;}
- (double)hzFor:(double)x {return std::clamp(20*std::pow(1000.0,(x-graph.origin.x)/graph.size.width),20.0,20000.0);}
- (NSPoint)selectedNode {return NSMakePoint([self xFor:model.value(frequency)],[self yFor:int(model.value(type))<=2?model.value(gain):0]);}
- (NSRect)selectedBadge {auto p=[self selectedNode];double width=int(model.value(type))<=2?128:86;return NSMakeRect(std::clamp(p.x-width/2,graph.origin.x,NSMaxX(graph)-width),MAX(graph.origin.y,p.y-44),width,26);}
- (NSRect)modeBadge:(NSPoint)p field:(int)field {
    double x=field==1?p.x-34:p.x+14;
    if(x<graph.origin.x)x=p.x+14;else if(x+20>NSMaxX(graph))x=p.x-34;
    return NSMakeRect(x,std::clamp(p.y-9,graph.origin.y,NSMaxY(graph)-18),20,18);
}
- (void)text:(NSString*)text at:(NSPoint)p color:(NSColor*)color size:(double)size {[text drawAtPoint:p withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],NSForegroundColorAttributeName:color}];}
- (void)drawRect:(NSRect)dirty {
    [[NSColor colorWithSRGBRed:.96 green:.985 blue:.99 alpha:.65] setFill];NSRectFillUsingOperation(self.bounds,NSCompositingOperationSourceOver);
    if(model.analyzer.availability!=just::AnalysisAvailability::fresh)[self text:model.analyzer.availability==just::AnalysisAvailability::stale?@"Spectrum paused":@"Spectrum unavailable" at:NSMakePoint(20,NSMaxY(graph)+24) color:NSColor.secondaryLabelColor size:9];
    for(double db:{-rangeDb,-rangeDb*2/3,-rangeDb/3,0.,rangeDb/3,rangeDb*2/3,rangeDb}) {
        double y=[self yFor:db];[[NSColor colorWithWhite:db==0?0.70:0.87 alpha:1] setStroke];auto* p=[NSBezierPath bezierPath];[p moveToPoint:NSMakePoint(graph.origin.x,y)];[p lineToPoint:NSMakePoint(NSMaxX(graph),y)];[p stroke];
        [self text:[NSString stringWithFormat:@"%+.0f",db] at:NSMakePoint(6,y-6) color:NSColor.secondaryLabelColor size:10];
    }
    for(double hz:{20.,50.,100.,200.,500.,1000.,2000.,5000.,10000.,20000.}) {
        double x=[self xFor:hz];[[NSColor colorWithWhite:0.87 alpha:1] setStroke];auto* p=[NSBezierPath bezierPath];[p moveToPoint:NSMakePoint(x,graph.origin.y)];[p lineToPoint:NSMakePoint(x,NSMaxY(graph))];[p stroke];
        [self text:[NSString stringWithFormat:hz>=1000?@"%.0fk":@"%.0f",hz>=1000?hz/1000:hz] at:NSMakePoint(std::clamp(x-8,graph.origin.x,NSMaxX(graph)-22),NSMaxY(graph)+3) color:NSColor.secondaryLabelColor size:10];
    }
    // Measured spectrum and EQ response use separate labeled axes.
    const auto& settings=model.analyzer.settings;
    for(unsigned tick=0;tick<=3;++tick){const double db=tick?-double(settings.rangeDb)*tick/3.:0.;[self text:[NSString stringWithFormat:@"%.0f",db] at:NSMakePoint(NSMaxX(graph)+5,graph.origin.y+SpectrumDisplay::verticalPosition(db,settings.rangeDb)*graph.size.height-6) color:NSColor.secondaryLabelColor size:10];}
    [self text:settings.tiltDbPerOctave>0?@"dBFS*":@"dBFS" at:NSMakePoint(NSMaxX(graph)+2,graph.origin.y-16) color:NSColor.secondaryLabelColor size:9];
    [self text:@"EQ dB" at:NSMakePoint(1,graph.origin.y-16) color:NSColor.secondaryLabelColor size:9];
    [NSGraphicsContext saveGraphicsState];NSRectClip(graph);
    if(model.analyzer.display.hasData()){
        NSColor* colors[]={NSColor.systemGrayColor,fieldColor(0)};
        const auto pixelWidth=[self convertRectToBacking:graph].size.width;const auto columns=std::size_t(std::clamp(std::ceil(pixelWidth)+1,2.,8192.));
        for(unsigned tap=0;tap<2;++tap){if((settings.source==just::AnalyzerSource::pre && tap==1)||(settings.source==just::AnalyzerSource::post && tap==0))continue;auto* trace=[NSBezierPath bezierPath];BOOL started=NO;
            for(std::size_t x=0;x<columns;++x){const auto measured=model.analyzer.display.point(tap,x,columns,settings);if(!measured.valid)continue;auto pt=NSMakePoint(graph.origin.x+double(x)/(columns-1)*graph.size.width,graph.origin.y+SpectrumDisplay::verticalPosition(measured.db,settings.rangeDb)*graph.size.height);if(!started){[trace moveToPoint:pt];started=YES;}else[trace lineToPoint:pt];}
            if(started){auto* area=[trace copy];[area lineToPoint:NSMakePoint(NSMaxX(trace.bounds),NSMaxY(graph))];[area lineToPoint:NSMakePoint(NSMinX(trace.bounds),NSMaxY(graph))];[area closePath];[[colors[tap] colorWithAlphaComponent:tap?.055:.10] setFill];[area fill];}
            [colors[tap] setStroke];trace.lineWidth=tap?1.35:1.;[trace stroke];}
    }
    if(model.soloToken && model.soloStatus.phase==just::AuditionPhase::active){double hz=model.value(frequency),q=model.value(just::eq::q),lo=hz/std::pow(2.,0.5/std::max(.1,q)),hi=hz*std::pow(2.,0.5/std::max(.1,q));int shapeValue=int(model.value(type));if(shapeValue==3 || shapeValue==1)lo=20;if(shapeValue==4 || shapeValue==2)hi=20000;[[fieldColor(0) colorWithAlphaComponent:.10] setFill];NSRectFillUsingOperation(NSMakeRect([self xFor:std::max(20.,lo)],graph.origin.y,[self xFor:std::min(20000.,hi)]-[self xFor:std::max(20.,lo)],graph.size.height),NSCompositingOperationSourceOver);}

    const bool separateFields=model.active(1)||model.active(2);
    for(int f=0;f<3;++f)if((f==0 && !separateFields && model.active(0)) || (f>0 && separateFields && (model.active(0)||model.active(f)))) {
        auto* p=[NSBezierPath bezierPath];
        for(int x=0;x<=int(graph.size.width);++x) {double px=graph.origin.x+x,hz=[self hzFor:px];if(hz>model.responseRate()*0.499)break;auto db=model.finalResponse(hz,model.responseRate());auto pt=NSMakePoint(px,[self yFor:db[f==0?0:f-1]]);if(x==0)[p moveToPoint:pt];else[p lineToPoint:pt];}
        [fieldColor(f) setStroke];p.lineWidth=f==0?2.8:2.2;CGFloat ds[]={f==1?8.0:2.0,f==1?5.0:4.0};if(f)[p setLineDash:ds count:2 phase:0];[p stroke];
    }
    [NSGraphicsContext restoreGraphicsState];
    for(std::size_t b=0;b<bandCount;++b)if(physical(model.state,index(b,enabled))>0.5) {
        int f=int(physical(model.state,index(b,target))),s=int(physical(model.state,index(b,type)));
        auto pt=NSMakePoint([self xFor:physical(model.state,index(b,frequency))],[self yFor:s<=2?physical(model.state,index(b,gain)):0]);
        if(b==model.selected){
            [fieldColor(f) setStroke];auto* guide=[NSBezierPath bezierPath];[guide moveToPoint:NSMakePoint(pt.x,graph.origin.y)];[guide lineToPoint:NSMakePoint(pt.x,NSMaxY(graph))];CGFloat dash[]={3,4};[guide setLineDash:dash count:2 phase:0];guide.lineWidth=.5;[guide stroke];
            [[NSColor whiteColor] setFill];auto* halo=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(pt.x-12,pt.y-12,24,24)];[halo fill];[halo stroke];
        }
        NSBezierPath* node=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(pt.x-6,pt.y-6,12,12)];
        [(model.soloToken && b!=model.selected)?[fieldColor(f) colorWithAlphaComponent:.3]:fieldColor(f) setFill];[node fill];[[NSColor whiteColor] setStroke];node.lineWidth=b==model.selected?3:1;[node stroke];
        if(b==model.selected){
            double hz=physical(model.state,index(b,frequency));NSString* title=hz>=1000?[NSString stringWithFormat:@"%.2f kHz",hz/1000]:[NSString stringWithFormat:@"%.0f Hz",hz];
            if(s<=2)title=[title stringByAppendingFormat:@" · %+.1f dB",physical(model.state,index(b,gain))];
            NSRect badge=[self selectedBadge];auto* box=[NSBezierPath bezierPathWithRoundedRect:badge xRadius:6 yRadius:6];
            [[NSColor colorWithWhite:1 alpha:.96] setFill];[[NSColor colorWithSRGBRed:.81 green:.88 blue:.90 alpha:1] setStroke];[box fill];[box stroke];
            [self text:title at:NSMakePoint(badge.origin.x+7,badge.origin.y+6) color:[NSColor colorWithSRGBRed:.20 green:.40 blue:.46 alpha:1] size:10];
        }
        if(f){NSRect badge=[self modeBadge:pt field:f];[[fieldColor(f) colorWithAlphaComponent:.22] setFill];[[NSBezierPath bezierPathWithRoundedRect:badge xRadius:4 yRadius:4] fill];[self text:f==1?@"M":@"S" at:NSMakePoint(badge.origin.x+5,badge.origin.y+2) color:[NSColor colorWithSRGBRed:.08 green:.19 blue:.23 alpha:1] size:11];}
    }
    if(!model.active(0) && !model.active(1) && !model.active(2))[self text:[self word:"所有频段关闭 · 双击添加频段" english:"All bands off · double-click to add a band"] at:NSMakePoint(graph.origin.x+18,NSMidY(graph)-26) color:NSColor.secondaryLabelColor size:12];
    if(model.soloToken)[self text:[self word:"SOLO · 左右拖动试听范围" english:"SOLO · drag horizontally to sweep"] at:NSMakePoint(20,33) color:fieldColor(0) size:10];
}
- (NSInteger)hit:(NSPoint)p {
    // Give the selected node priority when several neutral bands overlap.
    auto near=[&](std::size_t b){int s=int(physical(model.state,index(b,type)));return physical(model.state,index(b,enabled))>.5 && std::hypot(p.x-[self xFor:physical(model.state,index(b,frequency))],p.y-[self yFor:s<=2?physical(model.state,index(b,gain)):0])<16;};
    if(near(model.selected))return model.selected;for(int b=11;b>=0;--b)if(near(b))return b;return -1;
}
- (void)mouseDown:(NSEvent*)event {
    auto p=[self convertPoint:event.locationInWindow fromView:nil];if(!NSPointInRect(p,graph))return;
    [self.window makeFirstResponder:self];[self cancel];auto b=[self hit:p];
    if(b>=0){lastDrag=p;dragHz=physical(model.state,index(b,frequency));dragDb=physical(model.state,index(b,gain));model.beginDrag(b);[self showSelection];dragging=YES;presentation.activity(PanelPresentation::Activity::nodeDrag,true,[self panelNow]);[self sync];}
    else if(event.clickCount==2){model.create([self hzFor:p.x]);if(model.panelVisible)[self showSelection];[self sync];}
    else {model.hidePanel();[self sync];}
}
- (void)mouseDragged:(NSEvent*)event {if(!dragging)return;auto p=[self convertPoint:event.locationInWindow fromView:nil];double amount=(event.modifierFlags&NSEventModifierFlagShift)?.1:1;dragHz=std::clamp(dragHz*std::pow(1000.,(p.x-lastDrag.x)*amount/graph.size.width),20.,20000.);dragDb=std::clamp(dragDb-(p.y-lastDrag.y)*amount*2*rangeDb/graph.size.height,-24.,24.);lastDrag=p;model.drag(dragHz,dragDb);[self sync];}
- (void)mouseUp:(NSEvent*)event {[self cancel];[self sync];}
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];[[NSNotificationCenter defaultCenter] removeObserver:self];
    if(wheelMonitor){[NSEvent removeMonitor:wheelMonitor];wheelMonitor=nil;}
    if(self.window){__weak JustEqCanvas* weakSelf=self;wheelMonitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent*(NSEvent* event){
        JustEqCanvas* canvas=weakSelf;if(!canvas || event.window!=canvas.window || canvas.hidden)return event;
        auto point=[canvas convertPoint:event.locationInWindow fromView:nil];
        if(canvas->selectedPanel.hidden || canvas->dragging || !canvas->presentation.acceptsInput([canvas panelNow]) || !NSPointInRect(point,canvas->selectedPanel.frame))return event;
        // EQ's panel wheel is a module semantic. Leave native text/select input
        // alone; route dial/background wheel to Q or the real cut slope.
        NSView* hit=[canvas->selectedPanel hitTest:point];
        for(NSView* v=hit;v && v!=canvas->selectedPanel;v=v.superview)if([v isKindOfClass:NSTextField.class] || [v isKindOfClass:NSTextView.class] || [v isKindOfClass:NSPopUpButton.class])return event;
        [canvas scrollWheel:event];return nil;
    }];}
    if(self.window){auto* center=[NSNotificationCenter defaultCenter];
        [center addObserver:self selector:@selector(lostFocus:) name:NSWindowDidResignKeyNotification object:self.window];[center addObserver:self selector:@selector(lostFocus:) name:NSWindowWillCloseNotification object:self.window];
        [center addObserver:self selector:@selector(textBegan:) name:NSControlTextDidBeginEditingNotification object:nil];[center addObserver:self selector:@selector(textEnded:) name:NSControlTextDidEndEditingNotification object:nil];
        [center addObserver:self selector:@selector(menuBegan:) name:NSMenuDidBeginTrackingNotification object:nil];[center addObserver:self selector:@selector(menuEnded:) name:NSMenuDidEndTrackingNotification object:nil];
    }else[self cancel];
}
- (void)lostFocus:(NSNotification*)notice {
    // Finish the ordinary AppKit text session before the inactivity clock can
    // expire it in an inactive plugin window; the shared parser owns the edit.
    NSResponder* responder=self.window.firstResponder;
    if([responder isKindOfClass:NSTextView.class] && [(NSTextView*)responder isFieldEditor] && [self ownsPanelControl:[(NSTextView*)responder delegate]])[self.window makeFirstResponder:self];
    [self cancel];presentation.activity(PanelPresentation::Activity::textFocus,false,[self panelNow]);[trackingMenus removeAllObjects];presentation.activity(PanelPresentation::Activity::menu,false,[self panelNow]);[self updatePresentation];
}
- (void)dealloc {if(wheelMonitor)[NSEvent removeMonitor:wheelMonitor];[soloTimer invalidate];[[NSNotificationCenter defaultCenter] removeObserver:self];}
- (void)cancel {dragging=NO;presentation.activity(PanelPresentation::Activity::nodeDrag,false,[self panelNow]);[self stopSolo:nil];model.cancelDrag();}
- (BOOL)resignFirstResponder {[self cancel];return [super resignFirstResponder];}
- (void)scrollWheel:(NSEvent*)event {
    auto p=[self convertPoint:event.locationInWindow fromView:nil];auto b=[self hit:p];BOOL panel=!selectedPanel.hidden && presentation.acceptsInput([self panelNow]) && NSPointInRect(p,selectedPanel.frame);
    if(b>=0 || panel){if(event.phase==NSEventPhaseEnded || event.momentumPhase!=NSEventPhaseNone || event.scrollingDeltaY==0)return;if(b>=0){[self.window makeFirstResponder:self];[self cancel];model.select(b);[self showSelection];}else presentation.interact([self panelNow]);model.wheel(event.scrollingDeltaY,(event.modifierFlags&NSEventModifierFlagShift)!=0);[self sync];}else[super scrollWheel:event];
}
- (void)rightMouseDown:(NSEvent*)event {
    auto p=[self convertPoint:event.locationInWindow fromView:nil];if(!NSPointInRect(p,graph))return;auto b=[self hit:p];[self.window makeFirstResponder:self];[self cancel];
    if(b<0){
        auto* menu=[[NSMenu alloc] initWithTitle:@"EQ"];menu.autoenablesItems=NO;
        auto* item=[[NSMenuItem alloc] initWithTitle:[self word:"添加频段" english:"Add band"] action:@selector(menuAddBand:) keyEquivalent:@""];item.target=self;item.representedObject=@([self hzFor:p.x]);item.enabled=NO;
        for(std::size_t band=0;band<bandCount;++band)if(physical(model.state,index(band,enabled))<.5)item.enabled=YES;
        [menu addItem:item];[NSMenu popUpContextMenu:menu withEvent:event forView:self];[self sync];return;
    }
    model.select(b);[self showSelection];[self sync];
    auto* menu=[[NSMenu alloc] initWithTitle:@"Band"];
    auto add=[&](NSString* title,SEL action,int tag){auto* item=[[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];item.target=self;item.tag=tag;[menu addItem:item];};
    add(model.value(enabled)>.5?@"Disable band":@"Enable band",@selector(menuEnable:),0);
    [menu addItem:NSMenuItem.separatorItem];for(int t=0;t<6;++t)add([NSString stringWithFormat:@"Shape · %s",typeLabels[t]],@selector(menuShape:),t);
    [menu addItem:NSMenuItem.separatorItem];for(int f=0;f<3;++f)add([NSString stringWithFormat:@"Target · %s",targetLabels[f]],@selector(menuField:),f);
    [menu addItem:NSMenuItem.separatorItem];add(@"Duplicate to unused band",@selector(menuDuplicate:),0);add(@"Delete band",@selector(deleteBand:),0);
    contextMenu=menu;presentation.activity(PanelPresentation::Activity::menu,true,[self panelNow]);[NSMenu popUpContextMenu:menu withEvent:event forView:self];contextMenu=nil;[trackingMenus removeAllObjects];presentation.activity(PanelPresentation::Activity::menu,false,[self panelNow]);[self sync];
}
- (void)menuField:(NSMenuItem*)item {model.write(target,item.tag);}
- (void)menuShape:(NSMenuItem*)item {model.write(type,item.tag);}
- (void)menuEnable:(NSMenuItem*)item {model.write(enabled,model.value(enabled)>.5?0:1);}
- (void)menuAddBand:(NSMenuItem*)item {model.create([item.representedObject doubleValue]);if(model.panelVisible)[self showSelection];}
- (void)menuDuplicate:(NSMenuItem*)item {if(model.duplicate())[self showSelection];else NSBeep();}
- (void)keyDown:(NSEvent*)event {
    if(event.keyCode==51 || event.keyCode==117)[self deleteBand:nil];else if(event.keyCode==53){[self cancel];model.hidePanel();[self sync];}
    else if(event.keyCode==48){[self cancel];model.select((model.selected+1)%bandCount);[self showSelection];[self sync];}
    else [super keyDown:event];
}
@end
namespace just {
class EqMacEditor final:public EditorContent {
    NSScrollView* scroll=nil;JustEqCanvas* canvas=nil;int width=680,height=460;
public:
    void setVisualsPaused(bool paused) override {[canvas setVisualsPaused:paused];}
    ~EqMacEditor() override {[canvas cancel];canvas->rotary.clear();[scroll removeFromSuperview];}
    bool attach(void* parent,const EditorServices& services) override {
        canvas=[[JustEqCanvas alloc] init];canvas.identifier=@"JustEQ.Canvas";canvas->model.connect(services);if(canvas->model.panelVisible)[canvas showSelection];
        scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.hasVerticalScroller=YES;scroll.hasHorizontalScroller=NO;scroll.scrollerStyle=NSScrollerStyleOverlay;scroll.autohidesScrollers=YES;scroll.drawsBackground=NO;scroll.documentView=canvas;
        [(__bridge NSView*)parent addSubview:scroll];[canvas sync];resize(width,height);return true;
    }
    // Common supplies logical content bounds after applying the module minimum
    // and UI scale. Keep the main graph/panel visible; only advanced details scroll.
    void resize(int w,int h) override {
        width=w;height=h;scroll.frame=NSMakeRect(0,0,w,h);canvas->viewportHeight=h;
        const int cw=std::max(430,w),rows=cw<600?3:2,mainHeight=mainViewHeight(h,canvas->model.advanced);
        canvas.frame=NSMakeRect(0,0,cw,canvas->model.advanced?std::max(h,mainHeight+130+rows*150):mainHeight);
        canvas.needsLayout=YES;[canvas layoutSubtreeIfNeeded];
    }
    void refresh(const EditorViewState&,const StatusSnapshot&) override {bool old=canvas->model.advanced;[canvas sync];if(old!=canvas->model.advanced){[canvas cancel];resize(width,height);[scroll.contentView scrollToPoint:NSZeroPoint];}}
};
EditorContent* createEqEditor(){return new EqMacEditor;}
}
