#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#include "../state/UserPresetStore.hpp"
#include "NativeEditor.hpp"
#include "VisualAssetsMac.hpp"
#define JustFoundationView JUST_OBJC_CLASS(FoundationView)
#define JustModalView JUST_OBJC_CLASS(ModalView)
@interface JustModalView:NSView @end
@implementation JustModalView
- (BOOL)isFlipped{return YES;}
@end
@interface JustFoundationView : NSView {
    just::EditorCallbacks callbacks;const just::PluginIdentity* product;
    NSTextField *title,*status,*feedback,*footer,*mode;
    NSButton *bypass,*advanced,*preset,*settings,*about;
    NSImageView* icon;NSImage* productIcon;
    NSTimer* timer;JustBackgroundView* background;
    NSView* contentParent;std::unique_ptr<just::EditorContent> content;
    JustAssetStore assets;JustModalView *overlay,*panel;NSInteger modalTab;NSString* scaleError;
    std::unique_ptr<just::UserPresetStore> userPresets;
    NSPopUpButton* managedPresets;NSTextField* presetName;NSString *presetMessage,*presetSelection,*pendingDelete,*presetNameDraft;
    NSTextField* presetNotice;BOOL waitingForPreset;
    BOOL presentationInitialized,lastPaused;NSMapTable<NSView*,NSArray*>* previousFilters;
    NSTextField* diagnostics;NSPopUpButton* scaleMenu;NSScrollView* modalScroll;
}
- (instancetype)initWithProduct:(const just::PluginIdentity*)p callbacks:(just::EditorCallbacks)c;
- (void)stop;
- (double)renderScale;
@end
@implementation JustFoundationView
- (double)renderScale{return callbacks.view->renderScale;}
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (NSString*)zh:(NSString*)zh en:(NSString*)en {return callbacks.view->language==just::UiLanguage::chinese?zh:en;}
- (NSButton*)button:(NSString*)text action:(SEL)action parent:(NSView*)parent {
    auto b=[NSButton buttonWithTitle:text target:self action:action];b.bezelStyle=NSBezelStyleRounded;b.bordered=NO;b.wantsLayer=YES;b.layer.backgroundColor=NSColor.whiteColor.CGColor;b.layer.cornerRadius=8;b.layer.borderWidth=1;b.layer.borderColor=justColor(0xd5e2e7).CGColor;b.font=[NSFont systemFontOfSize:12];[parent addSubview:b];return b;
}
- (instancetype)initWithProduct:(const just::PluginIdentity*)p callbacks:(just::EditorCallbacks)c {
    self=[super initWithFrame:NSMakeRect(0,0,c.view->width,c.view->height)];if(!self)return nil;
    self.identifier=[NSString stringWithUTF8String:just::foundationViewIdentifier];self.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];callbacks=c;product=p;
    self.wantsLayer=YES;self.layer.backgroundColor=justColor(0xfbfdfe).CGColor;
    assets.load(self.class,p->slug);self.layer.backgroundColor=assets.color(@"header",0xfbfdfe).CGColor;productIcon=assets.icon(p->slug);
    icon=[[NSImageView alloc] initWithFrame:NSZeroRect];icon.image=productIcon;icon.imageScaling=NSImageScaleProportionallyUpOrDown;icon.wantsLayer=YES;icon.layer.cornerRadius=11;icon.layer.masksToBounds=YES;[self addSubview:icon];
    title=[NSTextField labelWithString:[NSString stringWithUTF8String:just::displayProductName(p->slug,p->name)]];title.font=[NSFont systemFontOfSize:22 weight:NSFontWeightSemibold];title.textColor=assets.color(@"ink",0x17333c);[self addSubview:title];
    preset=[self button:@"" action:@selector(showPresets:) parent:self];preset.tag=100;
    bypass=[self button:@"" action:@selector(changeBypass:) parent:self];bypass.buttonType=NSButtonTypePushOnPushOff;bypass.layer.borderWidth=0;bypass.toolTip=[NSString stringWithUTF8String:just::moduleBypassTooltip(just::moduleDefinition())];[self addSubview:bypass];
    advanced=[self button:@"" action:@selector(changeView:) parent:self];advanced.buttonType=NSButtonTypePushOnPushOff;
    settings=[self button:@"⚙" action:@selector(showSettings:) parent:self];settings.accessibilityLabel=@"Settings";settings.font=[NSFont systemFontOfSize:20];settings.layer.borderWidth=0;
    about=[self button:@"" action:@selector(showAbout:) parent:self];
    footer=[NSTextField labelWithString:[NSString stringWithUTF8String:just::productSubtitle(p->slug)]];footer.font=[NSFont systemFontOfSize:10 weight:NSFontWeightSemibold];footer.textColor=justColor(0x6b959e);[self addSubview:footer];
    mode=[NSTextField labelWithString:@""];mode.font=[NSFont systemFontOfSize:10];mode.textColor=assets.color(@"muted",0x758b93);[self addSubview:mode];
    status=[NSTextField labelWithString:@""]; // Technical status lives only in Settings.
    // No artwork decode or background timer in this version, even for old UI chunks.
    background=[[JustBackgroundView alloc] initWithFrame:NSZeroRect];background->enabled=NO;[self addSubview:background];
    NSString* root=c.presetDirectoryOverride?[NSString stringWithUTF8String:c.presetDirectoryOverride]:[[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject.path stringByAppendingPathComponent:@"JUST/Presets/v1"];
    userPresets=std::make_unique<just::UserPresetStore>(root?root.fileSystemRepresentation:"",p->processor,just::moduleDefinition().parameters);
    previousFilters=[NSMapTable strongToStrongObjectsMapTable];
    feedback=[NSTextField wrappingLabelWithString:@""];feedback.alignment=NSTextAlignmentCenter;[self addSubview:feedback];
    if(auto factory=just::moduleDefinition().createEditorContent){contentParent=[[NSView alloc] initWithFrame:NSZeroRect];[self addSubview:contentParent];content.reset(factory());if(!content || !content->attach((__bridge void*)contentParent,callbacks.services)){content.reset();[contentParent removeFromSuperview];contentParent=nil;}}
    timer=[NSTimer scheduledTimerWithTimeInterval:1./(callbacks.view->lowPerformance?15:30) target:self selector:@selector(refresh:) userInfo:nil repeats:YES];[self refresh:nil];[self layout];return self;
}
- (void)drawRect:(NSRect)dirty {
    [(callbacks.view->visualsPaused?NSColor.lightGrayColor:justColor(0xd9e5e9)) setStroke];auto path=[NSBezierPath bezierPath];[path moveToPoint:NSMakePoint(0,84)];[path lineToPoint:NSMakePoint(self.bounds.size.width,84)];[path moveToPoint:NSMakePoint(0,self.bounds.size.height-40)];[path lineToPoint:NSMakePoint(self.bounds.size.width,self.bounds.size.height-40)];[path stroke];
}
- (void)updatePresentationState {
    const bool paused=callbacks.getBypass(callbacks.owner);
    if(!presentationInitialized || lastPaused!=paused){
        if(callbacks.view->visualsPaused && !paused)++callbacks.view->visualResumeGeneration;
        callbacks.view->visualsPaused=paused;
        if(callbacks.setVisualsPaused)callbacks.setVisualsPaused(callbacks.owner,paused);
        if(content)content->setVisualsPaused(paused);
        presentationInitialized=YES;lastPaused=paused;self.needsDisplay=YES;
    }
    bypass.layer.backgroundColor=justColor(paused?0xd83e48:0xffffff).CGColor;
    bypass.contentTintColor=paused?NSColor.whiteColor:justColor(0x17333c);
    self.layer.backgroundColor=(paused?NSColor.whiteColor:assets.color(@"header",0xfbfdfe)).CGColor;
    // Filter presentation only. No hit-testing overlay, disabled controls,
    // field-editor cancellation, gesture termination or audio writes here.
    for(NSView* child in self.subviews){if(child==bypass)continue;
        if(paused){if(![previousFilters objectForKey:child]){
            [previousFilters setObject:child.contentFilters?:@[] forKey:child];child.wantsLayer=YES;
            auto filter=[CIFilter filterWithName:@"CIColorControls"];[filter setValue:@0 forKey:kCIInputSaturationKey];
            child.contentFilters=[(child.contentFilters?:@[]) arrayByAddingObject:filter];
        }}else if(auto filters=[previousFilters objectForKey:child]){child.contentFilters=filters;[previousFilters removeObjectForKey:child];}
    }
}
- (void)refresh:(id)sender {
    const double interval=1./(callbacks.view->lowPerformance?15:30);
    if(timer && std::abs(timer.timeInterval-interval)>1e-6){[timer invalidate];timer=[NSTimer scheduledTimerWithTimeInterval:interval target:self selector:@selector(refresh:) userInfo:nil repeats:YES];}
    const auto& s=callbacks.services;
    if(self.window && !self.window.isKeyWindow && s.readAuditionStatus && s.endAudition){auto a=s.readAuditionStatus(s.owner);if(a.token)s.endAudition(s.owner,a.token);}
    const auto transaction=s.readPresetTransaction?s.readPresetTransaction(s.owner):just::PresetTransactionStatus::unavailable;
    preset.enabled=transaction!=just::PresetTransactionStatus::pending;
    [self updatePresentationState];
    preset.title=transaction==just::PresetTransactionStatus::pending?[self zh:@"等待预设…" en:@"Pending…"]:transaction==just::PresetTransactionStatus::rejected?[self zh:@"重试预设" en:@"Retry preset"]:[self zh:@"预设 ▾" en:@"Preset ▾"];
    bypass.title=[self zh:@"● 旁路" en:@"● Bypass"];bypass.state=callbacks.getBypass(callbacks.owner)?NSControlStateValueOn:NSControlStateValueOff;
    advanced.title=callbacks.view->advanced?[self zh:@"简易" en:@"Simple"]:[self zh:@"高级" en:@"Advanced"];advanced.state=callbacks.view->advanced?NSControlStateValueOn:NSControlStateValueOff;advanced.layer.backgroundColor=justColor(callbacks.view->advanced?0xd8f5f5:0xffffff).CGColor;
    about.title=[self zh:@"关于" en:@"About"];mode.stringValue=callbacks.view->advanced?[self zh:@"高级视图" en:@"Advanced view"]:[self zh:@"简易视图" en:@"Simple view"];
    auto snapshot=callbacks.readStatus(callbacks.owner);status.stringValue=[NSString stringWithUTF8String:just::statusText(snapshot,bypass.state==NSControlStateValueOn).c_str()];
    feedback.hidden=bool(content);feedback.stringValue=[self zh:@"此模块的原生内容不可用" en:@"Native content is unavailable for this module"];
    if(content)content->refresh(*callbacks.view,snapshot);
    if(waitingForPreset && (transaction==just::PresetTransactionStatus::applied || transaction==just::PresetTransactionStatus::rejected)){waitingForPreset=NO;presetMessage=transaction==just::PresetTransactionStatus::applied?[self zh:@"预设已载入。" en:@"Preset loaded."]:[self zh:@"载入被拒绝；当前声音保留。" en:@"Load rejected; current sound preserved."];if(presetNotice)presetNotice.stringValue=presetMessage;}
    if(diagnostics)diagnostics.stringValue=[NSString stringWithFormat:@"%@\n%@",status.stringValue,scaleError?:[self zh:@"界面偏好随宿主保存。" en:@"UI preferences save with the host."]];

}
- (NSString*)factoryTitle:(NSUInteger)i {
    const auto& m=just::moduleDefinition();
    if(!m.factoryPresetCount)return [self zh:@"默认" en:@"Default"];
    return [NSString stringWithFormat:@"%s — %s",m.factoryPresets[i].category,m.factoryPresets[i].name];
}
- (NSMenu*)factoryPresetMenu {
    const auto& module=just::moduleDefinition();NSMenu* menu=[[NSMenu alloc] initWithTitle:@"Presets"];
    for(std::size_t i=0;i<just::effectiveFactoryPresetCount(module);++i){
        NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:[self factoryTitle:i] action:@selector(loadPreset:) keyEquivalent:@""];item.target=self;item.tag=i;[menu addItem:item];}
    auto list=userPresets->list();
    if(!list.presets.empty())[menu addItem:NSMenuItem.separatorItem];
    for(const auto& p:list.presets){auto item=[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%@ · %s",[self zh:@"用户" en:@"User"],p.name.c_str()] action:@selector(loadUserPreset:) keyEquivalent:@""];item.target=self;item.representedObject=[NSString stringWithUTF8String:p.id.c_str()];[menu addItem:item];}
    [menu addItem:NSMenuItem.separatorItem];auto manage=[[NSMenuItem alloc] initWithTitle:[self zh:@"管理预设…" en:@"Manage presets…"] action:@selector(showPresetManager:) keyEquivalent:@""];manage.target=self;[menu addItem:manage];
    NSMenuItem* undo=[[NSMenuItem alloc] initWithTitle:[self zh:@"撤销上次预设" en:@"Undo last preset"] action:@selector(undoPreset:) keyEquivalent:@""];undo.target=self;
    undo.enabled=callbacks.services.canUndoLastPreset && callbacks.services.canUndoLastPreset(callbacks.services.owner);[menu addItem:undo];menu.autoenablesItems=NO;
    return menu;
}
- (void)showPresets:(id)sender {[[self factoryPresetMenu] popUpMenuPositioningItem:nil atLocation:NSMakePoint(0,28) inView:preset];}
- (BOOL)applyPresetState:(just::SoundState&)desired {
    const auto& m=just::moduleDefinition();const auto& s=callbacks.services;
    // Existing preset semantics: loading never turns bypass on/off. Undo remains
    // the Controller's original whole-state transaction; it is not replaced here.
    desired.targets[m.parameters.index(just::bypassParamID)]=callbacks.getBypass(callbacks.owner)?1:0;
    const bool valid=just::validCompleteState(desired,product->processor,m.parameters) && (!m.validatePresetState || m.validatePresetState(desired));
    const bool ok=valid && s.requestApplySoundState && s.requestApplySoundState(s.owner,desired);
    waitingForPreset=ok;
    presetMessage=ok?[self zh:@"已请求载入；等待音频事务确认。" en:@"Load requested; awaiting audio confirmation."]:[self zh:@"无法载入：状态未连接、正在变化，或配置未获模块支持。" en:@"Load unavailable: disconnected, changing state, or unsupported configuration."];
    [self refresh:nil];return ok;
}
- (void)loadPreset:(NSMenuItem*)sender {
    just::SoundState desired;
    if(sender.tag>=0 && just::buildFactoryPreset(just::moduleDefinition(),sender.tag,product->processor,desired) && ![self applyPresetState:desired] && !overlay)[self showPresetManager:nil];
}
- (void)loadUserPreset:(NSMenuItem*)sender {
    just::UserPreset p;std::string error;NSString* key=sender.representedObject;
    if(key && userPresets->load(key.UTF8String,p,error)){if(![self applyPresetState:p.state] && !overlay)[self showPresetManager:nil];}
    else{presetMessage=[NSString stringWithUTF8String:error.c_str()];[self showPresetManager:nil];}
}
- (void)showPresetManager:(id)sender {modalTab=2;pendingDelete=nil;[self showModal];}
- (void)presetSelected:(id)sender {presetSelection=managedPresets.selectedItem.representedObject;pendingDelete=nil;presetName=nil;presetNameDraft=nil;[self showModal];}
- (void)saveUserPreset:(id)sender {
    const auto& s=callbacks.services;just::SoundState state;
    if(!s.capturePresetState || !s.capturePresetState(s.owner,state)){presetMessage=[self zh:@"声音状态尚未同步，请稍后再保存。" en:@"Sound state is not synchronized yet. Please retry shortly."];[self showModal];return;}
    std::string id,error;const bool ok=userPresets->save(presetName.stringValue.UTF8String,state,id,error);
    presetMessage=ok?[self zh:@"用户预设已保存。" en:@"User preset saved."]:[NSString stringWithUTF8String:error.c_str()];
    if(ok)presetSelection=[@"user:" stringByAppendingString:[NSString stringWithUTF8String:id.c_str()]];[self showModal];
}
- (void)loadManagedPreset:(id)sender {
    auto item=managedPresets.selectedItem;NSString* key=item.representedObject;
    if([key hasPrefix:@"user:"]){auto selection=[[NSMenuItem alloc] init];selection.representedObject=[key substringFromIndex:5];[self loadUserPreset:selection];}
    else [self loadPreset:item];[self showModal];
}
- (void)renameUserPreset:(id)sender {
    NSString* key=managedPresets.selectedItem.representedObject;if(![key hasPrefix:@"user:"])return;
    std::string error;const bool ok=userPresets->rename([key substringFromIndex:5].UTF8String,presetName.stringValue.UTF8String,error);
    presetMessage=ok?[self zh:@"预设已重命名。" en:@"Preset renamed."]:[NSString stringWithUTF8String:error.c_str()];pendingDelete=nil;[self showModal];
}
- (void)deleteUserPreset:(id)sender {
    NSString* key=managedPresets.selectedItem.representedObject;if(![key hasPrefix:@"user:"])return;
    pendingDelete=[key copy];[self showModal];
}
- (void)cancelDeletePreset:(id)sender {pendingDelete=nil;[self showModal];}
- (void)confirmDeletePreset:(id)sender {
    NSString* key=managedPresets.selectedItem.representedObject;
    if(!pendingDelete || ![key isEqualToString:pendingDelete] || ![key hasPrefix:@"user:"])return;
    std::string error;const bool ok=userPresets->remove([key substringFromIndex:5].UTF8String,error);
    presetMessage=ok?[self zh:@"用户预设已删除。" en:@"User preset deleted."]:[NSString stringWithUTF8String:error.c_str()];pendingDelete=nil;if(ok)presetSelection=nil;[self showModal];
}
- (void)buildPresetManager:(NSView*)body {
    auto label=[NSTextField labelWithString:[self zh:@"工厂预设只读；用户预设仅属于当前插件。" en:@"Factory presets are read-only. User presets belong to this plugin."]];label.font=[NSFont systemFontOfSize:11];label.frame=NSMakeRect(10,8,416,24);[body addSubview:label];
    managedPresets=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(10,42,414,28) pullsDown:NO];managedPresets.identifier=@"just.presets.selection";managedPresets.target=self;managedPresets.action=@selector(presetSelected:);[body addSubview:managedPresets];
    for(NSUInteger i=0;i<just::effectiveFactoryPresetCount(just::moduleDefinition());++i){[managedPresets addItemWithTitle:[NSString stringWithFormat:@"%@ · %@",[self zh:@"工厂" en:@"Factory"],[self factoryTitle:i]]];managedPresets.lastItem.tag=i;managedPresets.lastItem.representedObject=[NSString stringWithFormat:@"factory:%lu",(unsigned long)i];}
    auto list=userPresets->list();for(const auto& p:list.presets){[managedPresets addItemWithTitle:[NSString stringWithFormat:@"%@ · %s",[self zh:@"用户" en:@"User"],p.name.c_str()]];managedPresets.lastItem.tag=-1;managedPresets.lastItem.representedObject=[@"user:" stringByAppendingString:[NSString stringWithUTF8String:p.id.c_str()]];}
    for(NSMenuItem* item in managedPresets.itemArray)if([item.representedObject isEqual:presetSelection]){[managedPresets selectItem:item];break;}
    presetSelection=managedPresets.selectedItem.representedObject;
    const BOOL isUser=[presetSelection hasPrefix:@"user:"];
    auto nameLabel=[NSTextField labelWithString:[self zh:@"名称" en:@"Name"]];nameLabel.frame=NSMakeRect(10,91,68,24);[body addSubview:nameLabel];
    presetName=[[NSTextField alloc] initWithFrame:NSMakeRect(78,86,346,28)];presetName.identifier=@"just.presets.name";presetName.usesSingleLineMode=YES;presetName.maximumNumberOfLines=1;
    if(presetNameDraft)presetName.stringValue=presetNameDraft;
    else if(isUser){for(const auto& p:list.presets)if([presetSelection isEqual:[@"user:" stringByAppendingString:[NSString stringWithUTF8String:p.id.c_str()]]])presetName.stringValue=[NSString stringWithUTF8String:p.name.c_str()];}
    else presetName.stringValue=[self zh:@"我的预设" en:@"My preset"];[body addSubview:presetName];
    auto save=[self button:[self zh:@"保存为新预设" en:@"Save new"] action:@selector(saveUserPreset:) parent:body];save.frame=NSMakeRect(10,132,126,30);save.identifier=@"just.presets.save";
    auto load=[self button:[self zh:@"载入" en:@"Load"] action:@selector(loadManagedPreset:) parent:body];load.frame=NSMakeRect(146,132,80,30);load.identifier=@"just.presets.load";
    auto rename=[self button:[self zh:@"重命名" en:@"Rename"] action:@selector(renameUserPreset:) parent:body];rename.frame=NSMakeRect(236,132,88,30);rename.enabled=isUser;rename.identifier=@"just.presets.rename";
    auto remove=[self button:[self zh:@"删除…" en:@"Delete…"] action:@selector(deleteUserPreset:) parent:body];remove.frame=NSMakeRect(334,132,90,30);remove.enabled=isUser;remove.identifier=@"just.presets.delete";
    if(pendingDelete){auto confirmation=[NSTextField wrappingLabelWithString:[self zh:@"确定删除所选用户预设？此操作不会改变当前声音。" en:@"Delete the selected user preset? The current sound will stay unchanged."]];confirmation.frame=NSMakeRect(10,178,414,44);[body addSubview:confirmation];
        auto confirm=[self button:[self zh:@"确认删除" en:@"Confirm delete"] action:@selector(confirmDeletePreset:) parent:body];confirm.frame=NSMakeRect(10,226,142,30);confirm.identifier=@"just.presets.confirm-delete";
        auto cancel=[self button:[self zh:@"取消" en:@"Cancel"] action:@selector(cancelDeletePreset:) parent:body];cancel.frame=NSMakeRect(162,226,100,30);[body addSubview:cancel];}
    NSString* message=presetMessage?:[self zh:@"保存完整声音状态；载入保留当前旁路。界面偏好不在预设中。" en:@"Save the complete sound state. Loading preserves current bypass. UI preferences are excluded."];
    if(!list.error.empty())message=[message stringByAppendingFormat:@"\n%s",list.error.c_str()];
    if(list.unreadable)message=[message stringByAppendingFormat:[self zh:@"\n%lu 个无法读取的文件已保留。" en:@"\n%lu unreadable files were left untouched."],(unsigned long)list.unreadable];
    auto note=[NSTextField wrappingLabelWithString:message];presetNotice=note;note.frame=NSMakeRect(10,pendingDelete?270:180,414,100);note.font=[NSFont systemFontOfSize:11];note.identifier=@"just.presets.message";[body addSubview:note];
}
- (void)undoPreset:(id)sender {if(callbacks.services.undoLastPreset)callbacks.services.undoLastPreset(callbacks.services.owner);[self refresh:nil];}
- (void)changeBypass:(id)sender {callbacks.setBypass(callbacks.owner,bypass.state==NSControlStateValueOn);[self refresh:nil];}
- (void)changeView:(id)sender {callbacks.view->advanced=advanced.state==NSControlStateValueOn;[self refresh:nil];}
- (void)showSettings:(id)sender {modalTab=0;[self showModal];}
- (void)showAbout:(id)sender {modalTab=1;[self showModal];}
- (void)closeModal:(id)sender {diagnostics=nil;scaleMenu=nil;managedPresets=nil;presetName=nil;presetNotice=nil;if(overlay)[previousFilters removeObjectForKey:overlay];[overlay removeFromSuperview];overlay=nil;panel=nil;[self.window makeFirstResponder:self];}
- (void)settingChanged:(NSControl*)sender {
    if(sender.tag==1){double next=scaleMenu.selectedItem.tag/100.;if(!callbacks.requestScale || !callbacks.requestScale(callbacks.resizeOwner,next))scaleError=[self zh:@"无法应用此缩放，已保持原尺寸。" en:@"Scale could not be applied; size is unchanged."];else scaleError=nil;}
    if(sender.tag==2)callbacks.view->language=((NSPopUpButton*)sender).indexOfSelectedItem?just::UiLanguage::english:just::UiLanguage::chinese;
    if(sender.tag==5){callbacks.view->lowPerformance=((NSButton*)sender).state==NSControlStateValueOn;[timer invalidate];timer=[NSTimer scheduledTimerWithTimeInterval:1./(callbacks.view->lowPerformance?15:30) target:self selector:@selector(refresh:) userInfo:nil repeats:YES];}
    [self refresh:nil];[self showModal];
}
- (void)showModal {
    if(presetName)presetNameDraft=[presetName.stringValue copy];
    [self.window makeFirstResponder:self];[self closeModal:nil];
    overlay=[[JustModalView alloc] initWithFrame:self.bounds];overlay.wantsLayer=YES;overlay.layer.backgroundColor=justColor(0x13323c,.18).CGColor;[self addSubview:overlay];
    panel=[[JustModalView alloc] initWithFrame:NSZeroRect];panel.wantsLayer=YES;panel.layer.backgroundColor=justColor(0xf7fbfc).CGColor;panel.layer.cornerRadius=16;[overlay addSubview:panel];
    auto tabS=[self button:[self zh:@"设置" en:@"Settings"] action:@selector(showSettings:) parent:panel];tabS.frame=NSMakeRect(24,18,84,30);
    auto tabA=[self button:[self zh:@"关于" en:@"About"] action:@selector(showAbout:) parent:panel];tabA.frame=NSMakeRect(112,18,84,30);
    auto tabP=[self button:[self zh:@"预设" en:@"Presets"] action:@selector(showPresetManager:) parent:panel];tabP.frame=NSMakeRect(200,18,84,30);
    auto close=[self button:@"×" action:@selector(closeModal:) parent:panel];close.frame=NSMakeRect(412,18,36,30);
    NSScrollView* scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];modalScroll=scroll;scroll.drawsBackground=NO;scroll.hasVerticalScroller=YES;[panel addSubview:scroll];
    auto body=[[JustModalView alloc] initWithFrame:NSMakeRect(0,0,438,modalTab==1?300:394)];scroll.documentView=body;
    if(modalTab==2){[self buildPresetManager:body];
    }else if(modalTab==1){
        auto image=[[NSImageView alloc] initWithFrame:NSMakeRect(177,18,84,84)];image.image=productIcon;image.imageScaling=NSImageScaleProportionallyUpOrDown;[body addSubview:image];
        NSArray* text=@[[NSString stringWithUTF8String:just::displayProductName(product->slug,product->name)],[NSString stringWithUTF8String:just::uiVersion],[NSString stringWithFormat:@"%@ %s",[self zh:@"作者" en:@"Created by"],just::uiAuthor],[NSString stringWithUTF8String:just::uiContact]];
        for(NSUInteger i=0;i<text.count;++i){auto l=[NSTextField labelWithString:text[i]];l.alignment=NSTextAlignmentCenter;l.font=[NSFont systemFontOfSize:i==0?22:12];l.textColor=justColor(i==0?0x17333c:0x5d818e);l.frame=NSMakeRect(0,120+i*34,438,28);[body addSubview:l];}
    }else{
        NSArray* names=@[[self zh:@"界面缩放" en:@"Interface scale"],[self zh:@"语言" en:@"Language"],[self zh:@"低性能模式" en:@"Low performance"]];
        for(int i=0;i<3;++i){auto l=[NSTextField labelWithString:names[i]];l.font=[NSFont systemFontOfSize:12];l.frame=NSMakeRect(10,10+i*48,240,26);[body addSubview:l];}
        scaleMenu=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(260,8,164,28) pullsDown:NO];scaleMenu.tag=1;scaleMenu.target=self;scaleMenu.action=@selector(settingChanged:);for(int n:{75,100,125,150}){[scaleMenu addItemWithTitle:[NSString stringWithFormat:@"%d%%",n]];scaleMenu.lastItem.tag=n;}[scaleMenu selectItemWithTag:int(std::lround(callbacks.view->renderScale*100))];[body addSubview:scaleMenu];
        auto lang=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(260,56,164,28) pullsDown:NO];[lang addItemsWithTitles:@[@"中文",@"English"]];[lang selectItemAtIndex:callbacks.view->language==just::UiLanguage::english?1:0];lang.tag=2;lang.target=self;lang.action=@selector(settingChanged:);[body addSubview:lang];
        auto low=[NSButton checkboxWithTitle:callbacks.view->lowPerformance?@"ON":@"OFF" target:self action:@selector(settingChanged:)];low.frame=NSMakeRect(340,106,84,24);low.tag=5;low.state=callbacks.view->lowPerformance?NSControlStateValueOn:NSControlStateValueOff;[body addSubview:low];
        auto version=[NSTextField labelWithString:[NSString stringWithUTF8String:just::uiVersion]];version.frame=NSMakeRect(10,253,414,22);[body addSubview:version];
        diagnostics=[NSTextField wrappingLabelWithString:@""];diagnostics.font=[NSFont systemFontOfSize:11];diagnostics.textColor=justColor(0x758b93);diagnostics.frame=NSMakeRect(10,286,414,96);[body addSubview:diagnostics];
    }
    [self layout];[self refresh:nil];
}
- (void)keyDown:(NSEvent*)e {if(e.keyCode==53 && overlay){[self closeModal:nil];return;}[super keyDown:e];}
- (void)layout {
    [super layout];CGFloat w=self.bounds.size.width,h=self.bounds.size.height;BOOL narrow=w<900;
    icon.frame=NSMakeRect(28,19,46,46);title.frame=NSMakeRect(90,27,narrow?200:280,32);
    title.font=[NSFont systemFontOfSize:narrow?18:22 weight:NSFontWeightSemibold];
    CGFloat presetX=narrow?300:390;preset.frame=NSMakeRect(presetX,28,MAX(66,MIN(150,w-presetX-270)),30);
    bypass.frame=NSMakeRect(w-250,29,84,26);advanced.frame=NSMakeRect(w-164,26,92,32);settings.frame=NSMakeRect(w-65,26,38,32);
    if(w<700){icon.frame=NSMakeRect(20,9,30,30);title.frame=NSMakeRect(60,10,MAX(110,w-280),30);title.font=[NSFont systemFontOfSize:17 weight:NSFontWeightSemibold];preset.frame=NSMakeRect(w-180,9,110,28);settings.frame=NSMakeRect(w-59,9,38,28);bypass.frame=NSMakeRect(w-228,49,84,26);advanced.frame=NSMakeRect(w-124,46,96,30);}
    background.frame=NSMakeRect(0,84,w,MAX(1,h-124));feedback.frame=NSMakeRect(28,120,w-56,80);
    if(content){contentParent.frame=NSMakeRect(0,84,w,MAX(1,h-124));content->resize(w,MAX(1,h-124));}
    footer.frame=NSMakeRect(28,h-27,190,18);mode.frame=NSMakeRect(w/2-70,h-27,140,18);about.frame=NSMakeRect(w-100,h-33,76,26);
    if(overlay){overlay.frame=self.bounds;CGFloat pw=MIN(472,w-24),ph=MIN(modalTab==1?398:476,h-24);panel.frame=NSMakeRect((w-pw)/2,(h-ph)/2,pw,ph);modalScroll.frame=NSMakeRect(8,58,pw-16,ph-70);}
}
- (void)stop {[timer invalidate];timer=nil;content.reset();}
@end
namespace just {
void* createNativeEditor(void* parent,const PluginIdentity& product,EditorCallbacks callbacks) {
    auto view=[[JustFoundationView alloc] initWithProduct:&product callbacks:callbacks];if(!view)return nullptr;[(__bridge NSView*)parent addSubview:view];return (__bridge_retained void*)view;
}
void destroyNativeEditor(void* handle) {auto view=(__bridge_transfer JustFoundationView*)handle;[view stop];[view removeFromSuperview];}
void resizeNativeEditor(void* handle,int width,int height) {
    auto view=(__bridge JustFoundationView*)handle;view.frame=NSMakeRect(0,0,width,height);
    // Whole owned subtree scales, while modules continue laying out logical points.
    auto scale=[view renderScale];
    view.bounds=NSMakeRect(0,0,width/scale,height/scale);view.needsLayout=YES;[view layoutSubtreeIfNeeded];
}
}
