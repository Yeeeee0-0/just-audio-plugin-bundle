#import <Cocoa/Cocoa.h>
#include "EditorModel.hpp"
#include "common/ui/AnalysisView.hpp"
#include "common/ui/Controls.hpp"
#include <vector>
using namespace just;using namespace just::distortion;

static NSString* actualAudioState(const AnalysisWindow& w) {
    NSString* state=@"";
    if(w.header.flags&analysisBypassed)state=@" · Bypass";
    else if((w.header.flags&analysisTransportKnown) && !(w.header.flags&analysisPlaying))state=@" · Host stopped";
    if(std::max(w.channels[0].peak,w.channels[1].peak)==0)state=[state stringByAppendingString:std::max(w.channels[2].peak,w.channels[3].peak)>0?@" · Tail/output active":@" · Silence"];
    if(w.header.flags&analysisInvalid)state=[state stringByAppendingString:@" · Invalid input"];
    return state;
}
static NSTextField* jdLabel(NSString* text) {
    NSTextField* f=[NSTextField labelWithString:text];
    f.textColor=[NSColor colorWithRed:.09 green:.20 blue:.24 alpha:1];return f;
}
@interface JustDistortionDocument : NSView @end
@implementation JustDistortionDocument
- (BOOL)isFlipped{return YES;}
@end
struct DistortionRotary {
    ID id;NSView* container=nil;std::unique_ptr<RotaryControl> control;
};
struct DistortionMenu {
    ID id;NSTextField* label=nil;NSPopUpButton* control=nil;
};
static DistortionRotary jdRotary(ID id,NSView* parent,const EditorServices& services,NSString* key) {
    DistortionRotary r;r.id=id;r.container=[[JustDistortionDocument alloc] initWithFrame:NSZeroRect];r.container.identifier=key;[parent addSubview:r.container];
    auto spec=parameters[registry.index(id)];if(id==post_lp_hz)spec.title="High Cut";
    DisplayPolicy policy;policy.labelZh=parameterLabelZh(id);
    r.control=RotaryControl::create((__bridge void*)r.container,services,spec,policy);return r;
}
@interface JustDistortionView : NSView <NSTextFieldDelegate> {
@public EditorModel editor;
@private
    NSScrollView* scroll;JustDistortionDocument* document;
    NSPopUpButton* models;NSTextField *modelLabel,*modelNote,*holdLabel,*holdField,*notice,*pendingNotice,*advancedTitle;
    NSView* advancedPanel;
    std::vector<DistortionRotary> simple,advanced;
    std::vector<DistortionMenu> menus;
    std::unique_ptr<AnalysisView> feedback;
    AnalysisCursor analysisCursor;
    std::uint64_t visualResumeGeneration;
    TextEditSession holdEdit;BOOL holdTyping,holdCancelled,advancedVisible;
    NSString* measuredNotice;
}
- (instancetype)initWithServices:(EditorServices)s;
- (void)refresh;
- (void)finish;
@end
@implementation JustDistortionView
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (instancetype)initWithServices:(EditorServices)s {
    self=[super initWithFrame:NSMakeRect(0,0,1120,460)];if(!self)return nil;editor.services=s;
    scroll=[[NSScrollView alloc] initWithFrame:self.bounds];scroll.drawsBackground=NO;scroll.autohidesScrollers=YES;
    document=[[JustDistortionDocument alloc] initWithFrame:self.bounds];scroll.documentView=document;[self addSubview:scroll];
    feedback=AnalysisView::create((__bridge void*)document,s,AnalysisViewMode::waveform);
    NSView* plot=(__bridge NSView*)feedback->nativeHandle();plot.wantsLayer=YES;plot.layer.cornerRadius=12;plot.layer.borderWidth=1;plot.layer.borderColor=[NSColor colorWithRed:.85 green:.90 blue:.92 alpha:1].CGColor;plot.layer.masksToBounds=YES;
    measuredNotice=@"Waveform unavailable · fixed PDC 32 samples";
    modelLabel=jdLabel(@"Model");modelLabel.font=[NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];[document addSubview:modelLabel];
    models=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];models.target=self;models.action=@selector(changeModel:);models.identifier=@"distortion.model";
    for(auto ordinal:modelDisplayOrder)[models addItemWithTitle:[NSString stringWithUTF8String:modelLabels[ordinal]]];[document addSubview:models];
    modelNote=[NSTextField wrappingLabelWithString:@""];modelNote.font=[NSFont systemFontOfSize:9];modelNote.textColor=[NSColor colorWithRed:.46 green:.57 blue:.61 alpha:1];[document addSubview:modelNote];
    simple.push_back(jdRotary(drive_db,document,s,@"distortion.simple.drive"));
    simple.push_back(jdRotary(crush_bits,document,s,@"distortion.simple.bits"));
    simple.push_back(jdRotary(mix,document,s,@"distortion.simple.mix"));
    holdLabel=jdLabel(@"Hold Rate");holdLabel.font=[NSFont systemFontOfSize:9];[document addSubview:holdLabel];
    holdField=[[NSTextField alloc] initWithFrame:NSZeroRect];holdField.identifier=@"distortion.simple.hold.value";holdField.alignment=NSTextAlignmentCenter;holdField.font=[NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];holdField.bordered=NO;holdField.drawsBackground=NO;holdField.delegate=self;holdField.target=self;holdField.action=@selector(commitHold:);holdField.toolTip=@"Actual Crush hold rate in Hz; capped to the running sample rate without rewriting its saved target.";[document addSubview:holdField];
    notice=jdLabel(@"");notice.identifier=@"distortion.measured-status";notice.font=[NSFont systemFontOfSize:9];notice.textColor=[NSColor colorWithRed:.40 green:.55 blue:.59 alpha:1];[document addSubview:notice];
    advancedPanel=[[JustDistortionDocument alloc] initWithFrame:NSZeroRect];advancedPanel.identifier=@"distortion.advanced";[document addSubview:advancedPanel];
    advancedTitle=jdLabel(@"ADVANCED");advancedTitle.font=[NSFont systemFontOfSize:10 weight:NSFontWeightMedium];[advancedPanel addSubview:advancedTitle];
    for(const auto& p:parameters){
        if(p.id==bypass || p.id==model || p.id==mix)continue;
        ID id=static_cast<ID>(p.id);NSString* key=[NSString stringWithUTF8String:p.stableKey];
        if(p.enumLabels){
            DistortionMenu row{id};row.label=jdLabel(id==post_lp_enabled?@"High Cut Enable":[NSString stringWithUTF8String:p.title]);row.label.alignment=NSTextAlignmentCenter;row.label.font=[NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
            row.control=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];for(unsigned n=0;n<=p.stepCount;++n)[row.control addItemWithTitle:[NSString stringWithUTF8String:p.enumLabels[n]]];row.control.target=self;row.control.action=@selector(changeEnum:);row.control.tag=p.id;row.control.identifier=key;row.control.enabled=id!=quality;
            [advancedPanel addSubview:row.label];[advancedPanel addSubview:row.control];menus.push_back(row);
        }else advanced.push_back(jdRotary(id,advancedPanel,s,key));
    }
    pendingNotice=[NSTextField wrappingLabelWithString:@"Quality: actual fixed 4x · PDC 32 samples. Saved requests remain pending until host PDC support is available."];
    pendingNotice.identifier=@"distortion.quality-status";pendingNotice.font=[NSFont systemFontOfSize:10];pendingNotice.textColor=[NSColor colorWithRed:.40 green:.55 blue:.59 alpha:1];[advancedPanel addSubview:pendingNotice];
    [self refresh];return self;
}
- (void)finish {
    // RotaryControl destruction closes host gestures before destroying parent views.
    holdField.delegate=nil;simple.clear();advanced.clear();feedback.reset();
}
- (void)changeModel:(NSPopUpButton*)sender {
    if(sender.indexOfSelectedItem<0 || sender.indexOfSelectedItem>=std::size(modelDisplayOrder))return;
    editor.write(model,double(modelDisplayOrder[sender.indexOfSelectedItem])/5);[self refresh];
}
- (void)changeEnum:(NSPopUpButton*)sender {
    if(!sender.enabled)return;const auto& p=parameters[registry.index(sender.tag)];editor.write(static_cast<ID>(p.id),double(sender.indexOfSelectedItem)/p.stepCount);[self refresh];
}
- (void)controlTextDidBeginEditing:(NSNotification*)n {
    if(n.object!=holdField)return;holdTyping=YES;holdCancelled=NO;
    const auto& p=parameters[registry.index(crush_hold_hz)];auto text=holdEdit.begin(p,{},editor.normalized(crush_hold_hz));holdField.stringValue=[NSString stringWithUTF8String:text.c_str()];
    if(holdField.currentEditor){holdField.currentEditor.string=holdField.stringValue;[holdField.currentEditor selectAll:nil];}
}
- (void)commitHold:(id)sender {
    if(!holdTyping || holdCancelled || editor.currentModel()!=Model::Crush)return;
    double value;const auto& p=parameters[registry.index(crush_hold_hz)];
    if(holdEdit.changed(p,{},holdField.stringValue.UTF8String,value)){
        editor.write(crush_hold_hz,value);
        holdEdit.begin(p,{},editor.normalized(crush_hold_hz));
    }
}
- (void)controlTextDidEndEditing:(NSNotification*)n {
    if(n.object!=holdField)return;[self commitHold:holdField];holdTyping=NO;[self refresh];
}
- (BOOL)control:(NSControl*)c textView:(NSTextView*)v doCommandBySelector:(SEL)command {
    if(c==holdField && command==@selector(cancelOperation:)){holdCancelled=YES;holdTyping=NO;[holdField abortEditing];[self.window makeFirstResponder:self];[self refresh];return YES;}return NO;
}
- (void)refresh {
    BOOL requested=editor.services.view && editor.services.view->advanced;
    BOOL changed=requested!=advancedVisible;advancedVisible=requested;
    const auto active=editor.currentModel();unsigned selected=0;while(selected<std::size(modelDisplayOrder) && modelDisplayOrder[selected]!=unsigned(active))++selected;
    if(selected<std::size(modelDisplayOrder))[models selectItemAtIndex:selected];
    const bool zh=editor.services.view && editor.services.view->language==UiLanguage::chinese;
    modelLabel.stringValue=zh?@"模型":@"Model";holdLabel.stringValue=zh?@"保持速率":@"Hold Rate";advancedTitle.stringValue=zh?@"高级参数":@"ADVANCED";
    modelNote.stringValue=active==Model::Clean?(zh?@"Clean 保留原信号，驱动暂不可调":@"Clean preserves the signal; Drive is inactive"):(zh?@"模型切换保留当前参数":@"Switching models preserves values");
    for(auto& r:simple){r.container.hidden=r.id==drive_db?active==Model::Crush:r.id==crush_bits?active!=Model::Crush:NO;if(r.control)r.control->refresh(!r.container.hidden && (r.id!=drive_db || active!=Model::Clean));}
    if(active!=Model::Crush && holdTyping){holdCancelled=YES;holdTyping=NO;[holdField abortEditing];}
    holdLabel.hidden=holdField.hidden=active!=Model::Crush;
    if(!holdTyping && !holdField.currentEditor)holdField.stringValue=[NSString stringWithUTF8String:editor.text(crush_hold_hz).c_str()];
    advancedPanel.hidden=!advancedVisible;scroll.hasVerticalScroller=advancedVisible;
    for(auto& r:advanced)if(r.control)r.control->refresh(advancedVisible);
    for(auto& row:menus){[row.control selectItemAtIndex:int(editor.target(row.id))];row.label.stringValue=[NSString stringWithUTF8String:zh?parameterLabelZh(row.id):(row.id==post_lp_enabled?"High Cut Enable":parameters[registry.index(row.id)].title)];}
    pendingNotice.stringValue=[NSString stringWithFormat:@"Quality: actual fixed 4x · PDC 32 samples. Saved target: %s%@",qualityLabels[int(editor.target(quality))],editor.target(quality)!=2?@" · pending host PDC support":@""];
    feedback->refresh();
    const auto* view=editor.services.view;
    if(view && visualResumeGeneration!=view->visualResumeGeneration){analysisCursor={};measuredNotice=@"Waveform unavailable · fixed PDC 32 samples";visualResumeGeneration=view->visualResumeGeneration;}
    if(!view || !view->visualsPaused){
    AnalysisBatch batch;AnalysisWindow latest{};bool got=false;auto available=AnalysisAvailability::unavailable;
    for(unsigned i=0;i<16 && editor.services.readAnalysis;++i){available=editor.services.readAnalysis(editor.services.owner,analysisCursor,batch);if(available!=AnalysisAvailability::fresh || !batch.count)break;latest=batch.windows[batch.count-1];got=true;}
    if(got){measuredNotice=[NSString stringWithFormat:@"Measured L waveform · PDC %u samples · Output peak %.1f dBFS%@",latest.header.latencySamples,20*std::log10(std::max(1e-8,std::max(latest.channels[2].peak,latest.channels[3].peak))),std::max(latest.channels[2].peak,latest.channels[3].peak)>1?@" · fixed ±1 display exceeded":@""];measuredNotice=[measuredNotice stringByAppendingString:actualAudioState(latest)];}
    else if(available!=AnalysisAvailability::fresh)measuredNotice=available==AnalysisAvailability::stale?@"Measured waveform stale":@"Waveform unavailable · fixed PDC 32 samples";
    }
    notice.stringValue=editor.hiddenCustom()?[measuredNotice stringByAppendingString:@" · Advanced: Custom"]:measuredNotice;
    self.needsLayout=YES;[self layoutSubtreeIfNeeded];
    if(changed){[document scrollPoint:NSZeroPoint];[scroll reflectScrolledClipView:scroll.contentView];}
}
- (void)layout {
    [super layout];CGFloat w=self.bounds.size.width,h=self.bounds.size.height;scroll.frame=self.bounds;
    // Approved 1120-point mock proportions. Shared shell supplies usable content
    // dimensions; no header offset is assumed here.
    CGFloat width=scroll.contentSize.width,mainHeight=MAX(460,h),margin=MAX(24,width*36/1120),graphHeight=mainHeight-231,controlsY=mainHeight-198;
    CGFloat gap=MAX(42,MIN(89,(width-428)/2));CGFloat total=164+132+132+2*gap,left=(width-total)/2;
    modelLabel.frame=NSMakeRect(left,controlsY+40,164,22);models.frame=NSMakeRect(left,controlsY+67,164,38);modelNote.frame=NSMakeRect(left,controlsY+111,176,42);
    for(auto& r:simple){CGFloat x=r.id==mix?left+164+2*gap+132:left+164+gap;r.container.frame=NSMakeRect(x,controlsY,132,172);if(r.control)r.control->resize(0,0,132,158);}
    CGFloat holdX=left+164+gap;holdLabel.frame=NSMakeRect(holdX-3,controlsY+172,55,20);holdField.frame=NSMakeRect(holdX+53,controlsY+169,102,24);
    feedback->resize(margin,26,width-2*margin,graphHeight);
    notice.frame=NSMakeRect(margin+12,26+graphHeight-19,width-2*margin-24,17);
    const unsigned columns=MAX(3,unsigned((width-52)/144));const CGFloat cell=(width-52)/columns;
    const unsigned count=unsigned(advanced.size()+menus.size());CGFloat panelHeight=52+((count+columns-1)/columns)*144+56;
    advancedPanel.frame=NSMakeRect(0,mainHeight,width,panelHeight);advancedTitle.frame=NSMakeRect(26,18,width-52,20);
    unsigned i=0;for(auto& r:advanced){CGFloat x=26+(i%columns)*cell,y=52+(i/columns)*144;r.container.frame=NSMakeRect(x,y,cell,136);if(r.control)r.control->resize((cell-98)/2,0,98,114);++i;}
    for(auto& row:menus){CGFloat x=26+(i%columns)*cell,y=52+(i/columns)*144;row.label.frame=NSMakeRect(x,y,cell,22);row.control.frame=NSMakeRect(x+7,y+52,cell-14,32);++i;}
    pendingNotice.frame=NSMakeRect(26,panelHeight-50,width-52,40);
    document.frame=NSMakeRect(0,0,width,mainHeight+(advancedVisible?panelHeight:0));
    // Keep the operation canvas transparent so common's replaceable background
    // remains visible without owning or intercepting module input.
}
@end
namespace just::distortion {
class MacContent final:public EditorContent {
    JustDistortionView* view=nil;
public:
    ~MacContent() override {[view finish];[view removeFromSuperview];view=nil;}
    bool attach(void* parent,const EditorServices& services) override {
        if(!parent || view)return false;view=[[JustDistortionView alloc] initWithServices:services];[(__bridge NSView*)parent addSubview:view];return view!=nil;
    }
    void resize(int width,int height) override {view.frame=NSMakeRect(0,0,width,height);view.needsLayout=YES;[view layoutSubtreeIfNeeded];}
    void refresh(const EditorViewState&,const StatusSnapshot&) override {[view refresh];}
};
EditorContent* createEditorContent(){return new MacContent;}
}
