#import <Cocoa/Cocoa.h>
#include "EditorModel.hpp"
#include "common/ui/Controls.hpp"
#include "FieldMac.hpp"
#include <map>

@interface JWiderView : NSView {
@public
    just::EditorServices services;
    std::unique_ptr<just::stereo::EditorModel> model;
    std::map<just::ParamID,std::unique_ptr<just::RotaryControl>> rotary;
    std::map<just::ParamID,std::unique_ptr<just::RotaryControl>> faders;
    NSMutableDictionary<NSNumber*,NSPopUpButton*>* menus;
    NSMutableDictionary<NSNumber*,NSTextField*>* menuLabels;
    JWMeasuredField *field,*meters;
    NSSegmentedControl *inputMode,*meterMode;
    NSButton *phaseLeft,*phaseRight,*swap,*mono;
    NSTextField *inputLabel,*meterLabel,*status,*advancedLabel,*inputHint;
    BOOL advanced;
    just::AnalysisAvailability availability;
    std::uint64_t visualResumeGeneration;
}
- (instancetype)initWithServices:(just::EditorServices)s;
- (void)refreshState:(const just::EditorViewState&)state;
- (void)cancelEdits;
@end
@implementation JWiderView
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (NSTextField*)label:(NSString*)title size:(double)size {
    NSTextField* v=[NSTextField labelWithString:title];v.font=[NSFont systemFontOfSize:size];v.textColor=JWColor(.44,.59,.64);[self addSubview:v];return v;
}
- (NSButton*)button:(NSString*)title action:(SEL)action identifier:(NSString*)identifier {
    NSButton* b=[NSButton buttonWithTitle:title target:self action:action];b.bezelStyle=NSBezelStyleRounded;b.accessibilityIdentifier=identifier;[self addSubview:b];return b;
}
- (NSSegmentedControl*)mode:(SEL)action identifier:(NSString*)identifier {
    NSSegmentedControl* c=[NSSegmentedControl segmentedControlWithLabels:@[@"L / R",@"M / S"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:action];c.selectedSegment=0;c.segmentStyle=NSSegmentStyleRounded;c.selectedSegmentBezelColor=JWColor(.81,.94,.95);c.accessibilityIdentifier=identifier;[self addSubview:c];return c;
}
- (instancetype)initWithServices:(just::EditorServices)s {
    self=[super initWithFrame:NSZeroRect];if(!self)return nil;using namespace just::stereo;
    services=s;model=std::make_unique<EditorModel>(s);self.accessibilityIdentifier=@"just.wider.content";
    self.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
    menus=[NSMutableDictionary dictionary];menuLabels=[NSMutableDictionary dictionary];
    just::EditorServices controls;controls.owner=(__bridge void*)self;controls.view=s.view;
    controls.readTarget=[](void* p,just::ParamID id){auto* v=(__bridge JWiderView*)p;return v->services.readTarget(v->services.owner,id);};
    controls.beginEdit=[](void* p,just::ParamID id){auto* v=(__bridge JWiderView*)p;v->model->releaseMono();return v->services.beginEdit(v->services.owner,id);};
    controls.performEdit=[](void* p,just::ParamID id,double n){auto* v=(__bridge JWiderView*)p;return v->services.performEdit(v->services.owner,id,n);};
    controls.endEdit=[](void* p,just::ParamID id){auto* v=(__bridge JWiderView*)p;v->services.endEdit(v->services.owner,id);};
    for(auto id:{Input,FieldWidth,Asymmetry,Rotation}){auto p=spec(id);if(id==Input)p.title="Gain";just::DisplayPolicy policy;policy.decimals=1;policy.labelZh=id==Input?"增益":id==FieldWidth?"宽度":id==Asymmetry?"不对称":"旋转";
        // All gesture and text-edit implementation belongs to common.
        policy.style=id==Input || id==FieldWidth?just::ControlStyle::vertical:just::ControlStyle::horizontal;
        faders[id]=just::RotaryControl::create((__bridge void*)self,controls,p,policy);
        NSView* native=(__bridge NSView*)faders[id]->nativeHandle();native.accessibilityIdentifier=[NSString stringWithFormat:@"stereo.fader.%u",id];
        native.toolTip=id==FieldWidth?@"Final side width after generation, shear and rotation. 0.0× removes all side.":id==Asymmetry?@"JUST side-to-mid shear: M′ = M + sin(angle) × S; before rotation and overall width.":id==Rotation?@"JUST orthogonal mid/side rotation, before overall Width, Swap and Mono Check.":@"Input gain";
    }
    for(auto id:{Width,Existing,Mix,Low,High,Output}){just::DisplayPolicy policy;policy.decimals=1;policy.labelZh=id==Width?"生成宽度":id==Existing?"原始侧信号":id==Mix?"混合":id==Low?"生成侧信号低切":id==High?"生成侧信号高切":"输出增益";rotary[id]=just::RotaryControl::create((__bridge void*)self,controls,spec(id),policy);}
    for(auto id:{Character,HighEnabled}){auto& p=spec(id);auto* label=[self label:[NSString stringWithUTF8String:p.title] size:11];menuLabels[@(id)]=label;
        NSPopUpButton* menu=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(unsigned n=0;n<=p.stepCount;++n)[menu addItemWithTitle:[NSString stringWithUTF8String:p.enumLabels[n]]];menu.target=self;menu.action=@selector(changeMenu:);menu.tag=id;menus[@(id)]=menu;[self addSubview:menu];}
    field=[[JWMeasuredField alloc] initWithFrame:NSZeroRect];field.accessibilityIdentifier=@"just.wider.measured-field";[self addSubview:field];
    meters=[[JWMeasuredField alloc] initWithFrame:NSZeroRect];meters->meterOnly=YES;meters.accessibilityIdentifier=@"just.wider.output-meters";[self addSubview:meters];
    inputLabel=[self label:@"INPUT MODE" size:9];inputLabel.alignment=NSTextAlignmentCenter;inputMode=[self mode:@selector(changeInput:) identifier:@"stereo.input-mode"];
    inputMode.toolTip=@"L/R decodes the two channels to mid/side. M/S treats channel 1 as mid and channel 2 as side.";
    inputHint=[self label:@"" size:9];inputHint.alignment=NSTextAlignmentCenter;
    meterLabel=[self label:@"METER MODE" size:9];meterLabel.alignment=NSTextAlignmentCenter;meterMode=[self mode:@selector(changeMeter:) identifier:@"stereo.meter-mode"];
    meterMode.toolTip=@"Display only. Peak levels of the final output sample window; M/S = 0.5 × (L ± R).";
    phaseLeft=[self button:@"L +" action:@selector(changePolarity:) identifier:@"stereo.invert-left"];phaseLeft.tag=InvertLeft;
    phaseRight=[self button:@"R +" action:@selector(changePolarity:) identifier:@"stereo.invert-right"];phaseRight.tag=InvertRight;
    swap=[self button:@"⇄" action:@selector(changePolarity:) identifier:@"stereo.swap"];swap.tag=Swap;swap.toolTip=@"Swap left/right by reversing the final side signal.";
    mono=[self button:@"Mono Check" action:@selector(changeMono:) identifier:@"stereo.mono-check"];mono.toolTip=@"Click to lock. Hold M to audition; release restores the previous lock.";
    status=[self label:@"Waiting for audio" size:10];status.alignment=NSTextAlignmentCenter;
    advancedLabel=[self label:@"GENERATED SIDE / OUTPUT" size:10];
    [self refreshState:*s.view];return self;
}
- (void)changePolarity:(NSButton*)b {auto id=just::ParamID(b.tag);model->write(id,model->read(id)>=.5?0:1);[self refreshState:*services.view];}
- (void)changeInput:(NSSegmentedControl*)c {model->write(just::stereo::InputMode,double(c.selectedSegment));}
- (void)changeMeter:(NSSegmentedControl*)c {meters->midSide=c.selectedSegment==1;meters.needsDisplay=YES;}
- (void)changeMenu:(NSPopUpButton*)m {const auto& p=just::stereo::spec(just::ParamID(m.tag));model->write(p.id,double(m.indexOfSelectedItem)/p.stepCount);}
- (void)changeMono:(id)sender {(void)sender;model->toggleMono();[self.window makeFirstResponder:self];}
- (void)keyDown:(NSEvent*)e {if([e.charactersIgnoringModifiers.lowercaseString isEqualToString:@"m"]){if(!e.isARepeat)model->holdMono();return;}[super keyDown:e];}
- (void)keyUp:(NSEvent*)e {if([e.charactersIgnoringModifiers.lowercaseString isEqualToString:@"m"]){model->releaseMono();return;}[super keyUp:e];}
- (void)cancelEdits {if(model){model->releaseMono();model->end();}}
- (BOOL)resignFirstResponder {[self cancelEdits];return [super resignFirstResponder];}
- (void)viewWillMoveToWindow:(NSWindow*)window {
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidResignKeyNotification object:self.window];
    if(window)[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowResigned:) name:NSWindowDidResignKeyNotification object:window];else [self cancelEdits];
    [super viewWillMoveToWindow:window];
}
- (void)windowResigned:(NSNotification*)n {(void)n;[self cancelEdits];}
- (void)dealloc {[self cancelEdits];[[NSNotificationCenter defaultCenter] removeObserver:self];}
- (void)updateAnalysis {[self refreshState:*services.view];}
- (void)refreshState:(const just::EditorViewState&)state {
    using namespace just;using namespace just::stereo;advanced=state.advanced;const bool zh=state.language==UiLanguage::chinese;
    field->zh=meters->zh=zh;inputLabel.stringValue=zh?@"输入模式":@"INPUT MODE";meterLabel.stringValue=zh?@"电平模式":@"METER MODE";advancedLabel.stringValue=zh?@"生成侧信号 / 输出":@"GENERATED SIDE / OUTPUT";
    menuLabels[@(Character)].stringValue=zh?@"特性":@"Character";menuLabels[@(HighEnabled)].stringValue=zh?@"启用生成侧信号高切":@"High Cut Enabled";
    BusLayoutSnapshot bus;const bool known=services.readBusLayout && services.readBusLayout(services.owner,bus) && bus.valid() && (bus.validFields&layoutBuses);
    const bool stereoIn=known && bus.inputChannels==2;
    for(auto& item:rotary){bool enabled=item.first!=High || model->read(HighEnabled)>=.5;if(item.first==Existing)enabled=stereoIn;item.second->refresh(enabled);}
    for(auto id:{Input,FieldWidth,Asymmetry,Rotation})faders[id]->refresh(true);
    for(auto id:{Character,HighEnabled})[menus[@(id)] selectItemAtIndex:NSInteger(std::round(model->read(id)))];
    inputMode.enabled=stereoIn;inputMode.selectedSegment=model->read(InputMode)>=.5?1:0;
    inputMode.toolTip=stereoIn?@"L/R input or encoded M/S input":known?@"M/S input needs a two-channel bus; saved choice is retained.":@"Input bus layout unavailable; no input mode change until the host reports it.";
    phaseLeft.title=model->read(InvertLeft)>=.5?@"L −":@"L +";phaseRight.title=model->read(InvertRight)>=.5?@"R −":@"R +";
    phaseRight.enabled=stereoIn;swap.title=model->read(Swap)>=.5?@"⇄ On":@"⇄";
    inputHint.stringValue=known && !stereoIn?(zh?@"单声道输入：M/S 不启用":@"Mono input · M/S inactive"):inputMode.selectedSegment==1?@"1 = M  /  2 = S":@"1 = L  /  2 = R";
    for(NSButton* b in @[phaseLeft,phaseRight,swap])b.contentTintColor=model->read(ParamID(b.tag))>=.5?JWColor(.02,.59,.66):JWColor(.45,.59,.63);
    mono.title=model->isHoldingMono()?(zh?@"单声道 · 按住":@"Mono · held"):model->read(Mono)>=.5?(zh?@"单声道 · 锁定":@"Mono · locked"):(zh?@"单声道试听":@"Mono Check");
    if(visualResumeGeneration!=state.visualResumeGeneration){field->measured={};meters->measured={};availability=AnalysisAvailability::unavailable;visualResumeGeneration=state.visualResumeGeneration;}
    if(!state.visualsPaused){
        SampleFrame samples;availability=services.readSamples?services.readSamples(services.owner,samples):AnalysisAvailability::unavailable;
        if(availability==AnalysisAvailability::fresh){if(!field->measured.accept(samples))availability=AnalysisAvailability::unavailable;}
        if(availability!=AnalysisAvailability::fresh)field->measured.clear();
    }
    field->availability=meters->availability=availability;meters->measured=field->measured;field.needsDisplay=meters.needsDisplay=YES;
    if(availability==AnalysisAvailability::fresh){auto& m=field->measured;NSString* correlation=m.correlationValid?[NSString stringWithFormat:zh?@"相关度 %.2f":@"Correlation %.2f",m.correlation]:(zh?@"相关度 —":@"Correlation —");
        NSString* transport=m.header.flags&analysisBypassed?@" · Bypass":(m.header.flags&analysisTransportKnown) && !(m.header.flags&analysisPlaying)?@" · Host stopped":@"";
        status.stringValue=[correlation stringByAppendingString:transport];status.textColor=m.correlationValid && m.correlation<0?JWColor(.78,.39,.25):JWColor(.44,.59,.64);
    }else {status.stringValue=availability==AnalysisAvailability::stale?(zh?@"测量已过期":@"Measurement stale"):(zh?@"等待音频":@"Waiting for audio");status.textColor=JWColor(.44,.59,.64);}
    [self layout];
}
- (void)layout {
    [super layout];using namespace just::stereo;const double w=self.bounds.size.width;if(w<300)return;
    const double pad=27,gap=20,total=w-2*pad-2*gap,left=total*1.05/4.94,center=total*2.85/4.94,right=total*1.04/4.94;
    const double cx=pad+left+gap,rx=cx+center+gap;
    faders[Input]->resize(int(pad),27,int(left*.5-2),324);faders[FieldWidth]->resize(int(pad+left*.5+2),27,int(left*.5-2),324);
    inputHint.frame=NSMakeRect(pad,443,left,15);inputLabel.frame=NSMakeRect(pad,360,left,14);inputMode.frame=NSMakeRect(pad,378,left,27);
    const double button=(left-8)/3;phaseLeft.frame=NSMakeRect(pad,412,button,25);swap.frame=NSMakeRect(pad+button+4,412,button,25);phaseRight.frame=NSMakeRect(pad+2*(button+4),412,button,25);
    field.frame=NSMakeRect(cx,27,center,292);faders[Asymmetry]->resize(int(cx),339,int(center),38);faders[Rotation]->resize(int(cx),386,int(center),38);
    meters.frame=NSMakeRect(rx,36,right,312);meterLabel.frame=NSMakeRect(rx,359,right,15);meterMode.frame=NSMakeRect(rx,378,right,27);
    mono.frame=NSMakeRect(cx+(center-132)/2,433,132,26);status.frame=NSMakeRect(pad,467,w-pad*2,21);
    for(auto& item:rotary){NSView* native=(__bridge NSView*)item.second->nativeHandle();native.hidden=!advanced;}
    for(NSNumber* key in menus){menus[key].hidden=menuLabels[key].hidden=!advanced;}advancedLabel.hidden=!advanced;
    if(advanced){advancedLabel.frame=NSMakeRect(pad,510,w-2*pad,18);const unsigned columns=std::max(3u,unsigned((w-2*pad)/132));const double cell=(w-2*pad)/columns;
        unsigned n=0;for(auto id:{Width,Existing,Mix,Low,High,Output}){rotary[id]->resize(int(pad+(n%columns)*cell+(cell-112)/2),int(543+(n/columns)*156),112,148);++n;}
        const double top=543+std::ceil(6./columns)*156;unsigned i=0;for(auto id:{Character,HighEnabled}){menuLabels[@(id)].frame=NSMakeRect(pad+i*220,top,200,19);menus[@(id)].frame=NSMakeRect(pad+i*220,top+22,192,28);++i;}}
}
@end
namespace just::stereo {
class MacContent final:public EditorContent {
    JWiderView* __strong view=nil;NSScrollView* __strong scroll=nil;int width=0,height=0;
public:
    ~MacContent() override {[view cancelEdits];[scroll removeFromSuperview];view=nil;scroll=nil;}
    bool attach(void* parent,const EditorServices& services) override {
        if(!parent || !services.view || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return false;
        view=[[JWiderView alloc] initWithServices:services];scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.hasVerticalScroller=YES;scroll.autohidesScrollers=YES;scroll.drawsBackground=NO;scroll.documentView=view;[(__bridge NSView*)parent addSubview:scroll];return view!=nil;
    }
    void resize(int w,int h) override {width=w;height=h;scroll.frame=NSMakeRect(0,0,w,h);const double cw=scroll.contentSize.width;const auto columns=std::max(3u,unsigned((cw-54)/132));double needed=view->advanced?613+std::ceil(6./columns)*156:498;view.frame=NSMakeRect(0,0,cw,MAX(needed,h));[view layout];}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {[view refreshState:state];resize(width,height);}
};
EditorContent* createEditorContent(){return new MacContent;}
}
