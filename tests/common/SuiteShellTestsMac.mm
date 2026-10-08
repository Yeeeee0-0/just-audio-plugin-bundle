// Windowless action/lifecycle wiring only. Never OS input or native focus acceptance.
#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <objc/runtime.h>
#include "common/ui/NativeEditor.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
#include "common/state/UserPresetStore.hpp"
#include <iostream>
static unsigned checks=0;
static void check(bool b,const char* label){++checks;if(!b){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
static id noWindow(id cls,SEL,...){std::cerr<<"FAIL attempted window allocation "<<class_getName(cls)<<'\n';std::exit(2);}
@protocol SuiteShellActions
- (void)refresh:(id)sender;
- (void)showSettings:(id)sender;
- (void)showPresetManager:(id)sender;
- (void)closeModal:(id)sender;
- (void)saveUserPreset:(id)sender;
- (void)loadManagedPreset:(id)sender;
- (void)renameUserPreset:(id)sender;
- (void)deleteUserPreset:(id)sender;
- (void)confirmDeletePreset:(id)sender;
- (void)cancelDeletePreset:(id)sender;
- (void)presetSelected:(id)sender;
- (void)settingChanged:(id)sender;
- (NSMenu*)factoryPresetMenu;
@end
static NSView* find(NSView* root,NSString* identifier){if([root.identifier isEqualToString:identifier])return root;for(NSView* c in root.subviews)if(auto result=find(c,identifier))return result;return nil;}
static NSControl* tagged(NSView* root,NSInteger tag){for(NSView* c in root.subviews){if([c isKindOfClass:NSControl.class] && [(NSControl*)c tag]==tag)return (NSControl*)c;if(auto result=tagged(c,tag))return result;}return nil;}
static NSTextField* number(NSView* root){for(NSView* c in root.subviews){if([c isKindOfClass:NSTextField.class] && [(NSTextField*)c isEditable])return (NSTextField*)c;if(auto result=number(c))return result;}return nil;}
static unsigned hooks=0,refreshes=0,sampleReads=0;static bool lastPause=false;static NSView* controlView;
namespace just {
static const ParameterSpec parameters[]={
 {"fixture.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
 {"fixture.value",1,"Value","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,0}};
static const ParameterRegistry registry{parameters,2};
class SuiteContent:public EditorContent {
    std::unique_ptr<RotaryControl> control;std::unique_ptr<AnalysisView> graph;
public:
    bool attach(void* parent,const EditorServices& services)override{control=RotaryControl::create(parent,services,parameters[1]);controlView=(__bridge NSView*)control->nativeHandle();graph=AnalysisView::create(parent,services,AnalysisViewMode::waveform);return bool(control && graph);}
    void resize(int,int)override{control->resize(24,20,112,148);graph->resize(180,20,420,180);}
    void refresh(const EditorViewState&,const StatusSnapshot&)override{++refreshes;control->refresh(true);graph->refresh();}
    void setVisualsPaused(bool paused)override{++hooks;lastPause=paused;}
};
const ModuleDefinition& moduleDefinition(){static const ModuleDefinition m=[](){ModuleDefinition m;m.parameters=registry;m.createEditorContent=[]()->EditorContent*{return new SuiteContent;};return m;}();return m;}
}
struct State {
    just::SoundState sound=just::initialState(just::pluginIdentities[0].processor,just::moduleDefinition().parameters);
    unsigned starts=0,writes=0,ends=0,applies=0,pauses=0;bool paused=false;just::PresetTransactionStatus transaction=just::PresetTransactionStatus::idle;
};
static void save(NSView* view,NSString* path){[view layoutSubtreeIfNeeded];auto pixels=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:pixels];check([[pixels representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES],"offscreen layout image saved");}
int main(int argc,char** argv){@autoreleasepool{
    check(argc==3,"explicit isolated preset root and evidence folder required");
    // Fail closed before any AppKit calls: tests are not authorized to create windows.
    auto method=class_getClassMethod(NSWindow.class,@selector(allocWithZone:));class_replaceMethod(object_getClass(NSWindow.class),@selector(allocWithZone:),(IMP)noWindow,method_getTypeEncoding(method));
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    just::EditorViewState view;view.width=1000;view.height=600;view.language=just::UiLanguage::english;view.backgroundEnabled=true;view.reduceMotion=true;
    State state;state.sound.seed=831;state.sound.configurationCount=1;state.sound.configurations[0]={1234,.75};state.sound.targets[1]=.6123456789012345;
    just::EditorCallbacks c;c.owner=&state;c.view=&view;c.presetDirectoryOverride=argv[1];
    c.getBypass=[](void* p){return static_cast<State*>(p)->sound.targets[0]>=.5;};c.setBypass=[](void* p,bool b){static_cast<State*>(p)->sound.targets[0]=b;};
    c.setVisualsPaused=[](void* p,bool b){auto& s=*static_cast<State*>(p);++s.pauses;s.paused=b;};
    c.readStatus=[](void*){just::StatusSnapshot s;s.mode="WINDOWLESS WIRING FIXTURE — not audio evidence";return s;};
    auto& s=c.services;s.owner=&state;s.view=&view;
    s.readTarget=[](void* p,just::ParamID id){return static_cast<State*>(p)->sound.targets[id];};
    s.beginEdit=[](void* p,just::ParamID){++static_cast<State*>(p)->starts;return true;};
    s.performEdit=[](void* p,just::ParamID id,double v){auto& s=*static_cast<State*>(p);++s.writes;s.sound.targets[id]=v;return true;};
    s.endEdit=[](void* p,just::ParamID){++static_cast<State*>(p)->ends;};
    s.readSamples=[](void*,just::SampleFrame& out){++sampleReads;out={};return just::AnalysisAvailability::unavailable;};
    s.capturePresetState=s.readCompleteSoundState=[](void* p,just::SoundState& out){out=static_cast<State*>(p)->sound;return true;};
    s.requestApplySoundState=[](void* p,const just::SoundState& in){auto& s=*static_cast<State*>(p);s.sound=in;++s.applies;s.transaction=just::PresetTransactionStatus::applied;return true;};
    s.readPresetTransaction=[](void* p){return static_cast<State*>(p)->transaction;};
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1000,600)];void* handle=just::createNativeEditor((__bridge void*)parent,just::pluginIdentities[0],c);NSView* shell=(__bridge NSView*)handle;auto actions=(id<SuiteShellActions>)shell;just::resizeNativeEditor(handle,1000,600);
    check(hooks==1 && !lastPause && refreshes && sampleReads,"attach invokes initial pause hook before live refresh");
    check(!std::filesystem::exists(argv[1]),"opening editor does not write presets to disk");
    NSTextField* field=number(controlView);check(field && field.enabled,"native numeric field exists and is enabled");
    id<NSTextFieldDelegate> delegate=field.delegate;[delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:field]];NSString* precise=[field.stringValue copy];
    state.sound.targets[0]=1;const auto reads=sampleReads,beforeRefresh=refreshes;[actions refresh:nil];
    check(view.visualsPaused && state.paused && lastPause && hooks==2,"bypass transition updates hook and reader without DSP writes");
    check(refreshes==beforeRefresh+1 && sampleReads==reads && field.enabled,"parameter refresh remains active, shared analysis freezes, controls remain enabled");
    check([field.stringValue isEqualToString:precise] && state.writes==0 && state.starts==state.ends,"bypass does not replace pending precise text or open/end gestures");
    NSButton* bypass=nil;for(NSView* child in shell.subviews)if([child isKindOfClass:NSButton.class] && [[(NSButton*)child title] containsString:@"Bypass"])bypass=(NSButton*)child;
    check(bypass!=nil && bypass.contentFilters.count==0,"bypass button excluded from grayscale filter");
    auto color=[NSColor colorWithCGColor:bypass.layer.backgroundColor];color=[color colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];check(color.redComponent>color.greenComponent*2 && color.redComponent>color.blueComponent*2,"bypass button has red presentation color");
    for(NSView* child in shell.subviews)if(child!=bypass){check(child.contentFilters.count>0,"all other shell subtrees have grayscale filter");check([[(CIFilter*)child.contentFilters.lastObject valueForKey:kCIInputSaturationKey] doubleValue]==0,"filter saturation is exactly zero");}
    state.sound.targets[1]=.75;[actions refresh:nil];check([field.stringValue isEqualToString:precise] && sampleReads==reads,"host automation retains typed text while visuals stay paused");
    [delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:field]];
    check(state.writes==0 && [field.stringValue isEqualToString:@"12.0"],"unchanged blur writes nothing and then shows newest host target");
    state.sound.targets[0]=0;[actions refresh:nil];check(!view.visualsPaused && view.visualResumeGeneration==1 && hooks==3 && sampleReads>reads,"resume signals one generation and immediately resumes live reads");
    for(NSView* child in shell.subviews)check(child.contentFilters.count==0,"normal filter state restored");
    [actions showSettings:nil];check(!tagged(shell,3) && !tagged(shell,4) && tagged(shell,5),"background/reduce-motion controls removed; low-performance kept");
    check(view.backgroundEnabled && view.reduceMotion,"legacy serialized preference values not rewritten");
    auto low=(NSButton*)tagged(shell,5);low.state=NSControlStateValueOn;[actions settingChanged:low];check(view.lowPerformance,"low performance remains editable");
    NSString* folder=[NSString stringWithUTF8String:argv[2]];save(shell,[folder stringByAppendingPathComponent:@"suite-settings-windowless.png"]);
    [actions showPresetManager:nil];auto rename=(NSButton*)find(shell,@"just.presets.rename");auto remove=(NSButton*)find(shell,@"just.presets.delete");check(!rename.enabled && !remove.enabled,"factory presets have no rename or delete controls");
    auto name=(NSTextField*)find(shell,@"just.presets.name");name.stringValue=@"First user sound";const auto saved=state.sound;[actions saveUserPreset:nil];
    just::UserPresetStore store(argv[1],just::pluginIdentities[0].processor,just::moduleDefinition().parameters);auto list=store.list();check(list.presets.size()==1 && just::sameSoundState(list.presets[0].state,saved,just::moduleDefinition().parameters),"manager saves complete sound state through injected isolated root");
    check([(NSButton*)find(shell,@"just.presets.rename") isEnabled],"user selection permits rename");
    state.sound.targets[0]=1;state.sound.targets[1]=.2;[actions loadManagedPreset:nil];check(state.applies==1 && state.sound.targets[0]==1 && state.sound.targets[1]==saved.targets[1] && state.sound.seed==saved.seed,"manager load preserves current bypass and complete sound data");
    name=(NSTextField*)find(shell,@"just.presets.name");name.stringValue=@"Renamed user sound";[actions renameUserPreset:nil];check(store.list().presets[0].name=="Renamed user sound","rename persists to disk");
    [actions deleteUserPreset:nil];check(store.list().presets.size()==1 && find(shell,@"just.presets.confirm-delete"),"delete requires explicit inline confirmation");[actions cancelDeletePreset:nil];check(store.list().presets.size()==1,"cancel delete preserves file");
    save(shell,[folder stringByAppendingPathComponent:@"suite-presets-windowless.png"]);
    [actions deleteUserPreset:nil];[actions confirmDeletePreset:nil];check(store.list().presets.empty() && state.applies==1,"confirmed delete removes only file, never current sound");
    [actions closeModal:nil];check([[actions factoryPresetMenu] numberOfItems]==4,"single fallback factory plus separator/manage/undo remain available");
    // Minimum supported shell dimensions must keep every management action reachable.
    state.sound.targets[0]=0;view.width=480;view.height=260;just::resizeNativeEditor(handle,480,260);[actions showPresetManager:nil];
    auto action=(NSButton*)find(shell,@"just.presets.delete");auto scroll=action.enclosingScrollView;
    check(scroll && NSMaxX(action.frame)<=scroll.contentSize.width,"compact manager keeps rightmost action within scroll viewport");
    save(shell,[folder stringByAppendingPathComponent:@"suite-presets-compact-windowless.png"]);
    just::destroyNativeEditor(handle);check(state.starts==state.ends,"all fixture gestures balanced at teardown");
    std::cout<<"PASS "<<checks<<" windowless shell/preset actions, live target refresh/frozen analysis, precise text preservation, grayscale wiring; zero windows. Native input/rendering QA still required.\n";
}}
