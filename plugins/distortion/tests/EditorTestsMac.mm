#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "plugins/distortion/EditorModel.hpp"
#include "plugins/distortion/Engine.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "common/ui/ObjCNames.hpp"
#include <iostream>
using namespace just;using namespace just::distortion;
static unsigned checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct Host {
    SoundState sound=initialState(pluginIdentities[9].processor,registry);EditorViewState view;
    unsigned begin=0,write=0,end=0;
    static double read(void* p,ParamID id){return static_cast<Host*>(p)->sound.targets[registry.index(id)];}
    static bool start(void* p,ParamID){++static_cast<Host*>(p)->begin;return true;}
    static bool perform(void* p,ParamID id,double n){auto& h=*static_cast<Host*>(p);++h.write;h.sound.targets[registry.index(id)]=n;return true;}
    static void stop(void* p,ParamID){++static_cast<Host*>(p)->end;}
    EditorServices services(){return {this,&view,read,start,perform,stop};}
};
static NSView* identified(NSView* root,NSString* id){if([root.identifier isEqualToString:id])return root;for(NSView* v in root.subviews)if(auto c=identified(v,id))return c;return nil;}
static NSView* privateView(NSView* root,NSString* name){for(NSView* v in root.subviews){if([NSStringFromClass(v.class) isEqualToString:name])return v;if(auto found=privateView(v,name))return found;}return nil;}
static NSTextField* editable(NSView* root){for(NSView* v in root.subviews){if([v isKindOfClass:NSTextField.class] && [(NSTextField*)v isEditable])return (NSTextField*)v;if(auto c=editable(v))return c;}return nil;}
static NSTextField* value(NSView* root,NSString* key){NSView* wrapper=identified(root,key);check(wrapper!=nil,"module rotary wrapper exists");check(identified(wrapper,[NSString stringWithUTF8String:rotaryViewIdentifier])!=nil,"wrapper uses actual shared rotary");auto f=editable(wrapper);check(f!=nil,"shared precise value field exists");return f;}
static void privateIdentity(NSView* root){
    for(NSString* name in @[@"JustDistortionView",@"JustDistortionDocument"]){NSView* v=privateView(root,name);check(v && v.class==NSClassFromString(name) && v.class==objc_getClass(name.UTF8String),"module private view has exact unique ObjC runtime identity");}
    check(privateView(root,@"JustDistortionSlider")==nil,"no module NSSlider implementation remains");
    for(const char* name:{"JDView","JDSlider","JDDocument"})check(objc_getClass(name)==nullptr,"legacy collision class is absent");
}
static void action(NSControl* c){check(c!=nil,"native control exists");[NSApp sendAction:c.action to:c.target from:c];}
// Delegate-level verification only. This deliberately never activates a window
// and must not be reported as physical mouse/keyboard acceptance.
static void begin(NSTextField* f){[f.delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:f]];}
static void end(NSTextField* f){[f.delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:f]];}
static void type(NSTextField* f,NSString* text){begin(f);f.stringValue=text;end(f);}
static void capture(NSView* view,const std::string& path){[view layoutSubtreeIfNeeded];auto rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];auto data=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([data writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"offscreen layout capture");}
int main(int argc,char** argv){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    Host host;check(physical(host.sound,model)==1,"new instance uses Soft");host.sound.targets[registry.index(model)]=0;host.sound.seed=77557;host.sound.targets[registry.index(fold_shape)]=.77;
    auto serialized=encodeState(host.sound,registry);auto original=host.sound;
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,840,474)];NSWindow* window=[[NSWindow alloc] initWithContentRect:parent.frame styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.contentView=parent;
    std::unique_ptr<EditorContent> ui(createEditorContent());check(ui->attach((__bridge void*)parent,host.services()),"attach");ui->resize(840,474);ui->refresh(host.view,{});[window displayIfNeeded];privateIdentity(parent);
    check(host.write==0 && encodeState(host.sound,registry)==serialized,"attach/read/refresh leave legacy Clean state byte-identical");
    auto drive=value(parent,@"distortion.simple.drive");check(!drive.enabled,"Clean Drive is inactive");
    auto cut=identified(parent,@"distortion.post_lp_hz");check(cut.isHiddenOrHasHiddenAncestor,"High Cut is absent from Simple");
    check(!value(parent,@"distortion.simple.mix").isHiddenOrHasHiddenAncestor,"Simple Mix is visible");
    if(argc>1)capture(parent,std::string(argv[1])+"/simple-clean.png");
    auto modelMenu=(NSPopUpButton*)identified(parent,@"distortion.model");[modelMenu selectItemAtIndex:4];action(modelMenu);ui->refresh(host.view,{});
    check(host.write==1 && physical(host.sound,model)==5,"presentation Crush maps to original enum5 and edits only Model");
    for(unsigned n=0;n<registry.count;++n)if(parameters[n].id!=model)check(host.sound.targets[n]==original.targets[n],"model retains all inactive parameters");
    check(identified(parent,@"distortion.simple.drive").isHiddenOrHasHiddenAncestor,"Crush hides Drive");
    type(value(parent,@"distortion.simple.bits"),@"6 bit");check(physical(host.sound,crush_bits)==6 && physical(host.sound,drive_db)==0,"Crush Bits writes its original actual ID");
    auto hold=(NSTextField*)identified(parent,@"distortion.simple.hold.value");type(hold,@"12345.6789 Hz");check(std::abs(physical(host.sound,crush_hold_hz)-12345.6789)<1e-8,"conditional Hold writes actual Hz ID without quantizing DSP");
    auto retained=host.sound.targets;unsigned edits=host.write;
    for(unsigned n=0;n<18;++n){host.view.advanced=!host.view.advanced;ui->refresh(host.view,{});check(host.sound.targets==retained && host.write==edits,"18 view toggles preserve complete targets without edits");}
    if(argc>1)capture(parent,std::string(argv[1])+"/simple-crush.png");
    [modelMenu selectItemAtIndex:0];action(modelMenu);ui->refresh(host.view,{});check(physical(host.sound,model)==1,"Soft presentation index0 preserves host ordinal1");
    type(value(parent,@"distortion.simple.drive"),@"18.123456789 dB");check(std::abs(physical(host.sound,drive_db)-18.123456789)<1e-11,"shared Drive restores its precise original binding after Crush");
    drive=value(parent,@"distortion.simple.drive");ui->refresh(host.view,{});check([drive.stringValue hasPrefix:@"18.1"],"idle Drive is one decimal");
    auto precise=host.sound.targets;auto writes=host.write;begin(drive);check(std::abs(drive.doubleValue-18.123456789)<1e-11,"focused value reveals full physical precision");
    for(unsigned i=0;i<120;++i)ui->refresh(host.view,{});end(drive);check(host.sound.targets==precise && host.write==writes,"timer-equivalent refresh and unchanged precise input never write rounded DSP values");
    type(drive,@"NaN");check(host.write==writes,"invalid shared numeric text cannot edit sound");
    host.view.advanced=true;ui->refresh(host.view,{});check(!identified(parent,@"distortion.post_lp_hz").isHiddenOrHasHiddenAncestor,"High Cut appears in Advanced");
    type(value(parent,@"distortion.post_lp_hz"),@"8000 Hz");auto enable=(NSPopUpButton*)identified(parent,@"distortion.post_lp_enabled");[enable selectItemAtIndex:1];action(enable);
    check(std::abs(physical(host.sound,post_lp_hz)-8000)<1e-8 && physical(host.sound,post_lp_enabled)==1,"Advanced High Cut edits original Hz and Enable IDs");
    [enable selectItemAtIndex:0];action(enable);check(physical(host.sound,post_lp_enabled)==0 && std::abs(physical(host.sound,post_lp_hz)-8000)<1e-8,"High Cut Off retains saved cutoff");
    auto qualityMenu=(NSPopUpButton*)identified(parent,@"distortion.quality");check(!qualityMenu.enabled,"Quality saved request remains honestly unavailable");
    check(simpleHasCustom(host.sound),"conditional Custom detects hidden High Cut and shape state");
    if(argc>1)capture(parent,std::string(argv[1])+"/advanced.png");
    auto before=host.sound;serialized=encodeState(host.sound,registry);ui.reset();check(parent.subviews.count==0,"close removes owned subtree");ui.reset(createEditorContent());check(ui->attach((__bridge void*)parent,host.services()),"reopen");ui->refresh(host.view,{});privateIdentity(parent);
    check(encodeState(host.sound,registry)==serialized && host.view.advanced,"close/reopen preserve complete sound and view");
    DistortionEngine a,b;a.prepare({48000,128,1,1});b.prepare({48000,128,1,1});a.applyTargets(before,0);b.applyTargets(host.sound,0);double input[128],outA[128],outB[128];for(unsigned n=0;n<128;++n)input[n]=.2*std::sin(.13*n);
    AudioBlock<double> block;block.inputs[0]=input;block.outputChannels=block.inputChannels=1;block.samples=128;block.outputs[0]=outA;a.process(block,{});block.outputs[0]=outB;b.process(block,{});for(unsigned n=0;n<128;++n)check(outA[n]==outB[n],"actual rendered audio is unchanged by editor lifecycle");
    check(host.begin==host.end && host.begin==host.write,"complete balanced one-edit host gestures");[window orderOut:nil];
    std::cout<<"PASS offscreen shared-control delegates/layout: "<<checks<<" checks; Soft Init, legacy Clean bytes, actual Bits/Hold/Drive IDs, Advanced HighCut, precise refresh retention, model-only changes, balanced gestures, lifecycle audio equality. Physical OS input NOT tested.\n";
}}
