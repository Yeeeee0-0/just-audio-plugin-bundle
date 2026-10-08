#import <Cocoa/Cocoa.h>
#include "../Editor.hpp"
#include "../EditorModel.hpp"
#include "common/ui/ObjCNames.hpp"
#include <iostream>
#include <filesystem>
namespace t=just::tremolo;
using namespace just;
@interface TremoloTestCanvas : NSView
@end
@implementation TremoloTestCanvas
- (void)drawRect:(NSRect)dirty{[[NSColor colorWithRed:.97 green:.985 blue:.99 alpha:1] setFill];NSRectFill(dirty);}
@end
static unsigned checks=0;
static void check(bool v,const char* name){++checks;if(!v){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}}
struct Owner {
    SoundState sound=initialState({},t::registry);EditorViewState view{};
    unsigned starts=0,writes=0,ends=0;
    static double read(void* p,ParamID id){return static_cast<Owner*>(p)->sound.targets[t::registry.index(id)];}
    static bool begin(void* p,ParamID){++static_cast<Owner*>(p)->starts;return true;}
    static bool write(void* p,ParamID id,double value){auto& o=*static_cast<Owner*>(p);o.sound.targets[t::registry.index(id)]=value;++o.writes;return true;}
    static void end(void* p,ParamID){++static_cast<Owner*>(p)->ends;}
    EditorServices services(){return {this,&view,read,begin,write,end};}
};
static void set(Owner& o,ParamID id,double v){o.sound.targets[t::registry.index(id)]=t::spec(id).toNormalized(v);}
static NSArray<NSView*>* descendants(NSView* root){NSMutableArray<NSView*>* result=[NSMutableArray array];for(NSView* view in root.subviews){[result addObject:view];[result addObjectsFromArray:descendants(view)];}return result;}
static bool rotary(NSView* v){return [v.identifier isEqualToString:[NSString stringWithUTF8String:rotaryViewIdentifier]];}
static unsigned visibleRotaries(NSView* parent){unsigned n=0;for(NSView* view in descendants(parent))if(rotary(view) && !view.isHiddenOrHasHiddenAncestor)++n;return n;}
static NSView* primary(NSView* parent,ParamID id){for(NSView* v in descendants(parent))if(rotary(v) && [v.accessibilityIdentifier isEqualToString:[NSString stringWithFormat:@"just.tremolo.primary.%u",unsigned(id)]] && !v.isHiddenOrHasHiddenAncestor)return v;return nil;}
static void screenshot(NSView* view,const std::string& path){auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];auto* data=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([data writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"save passive-view screenshot");}
int main(int argc,char** argv){@autoreleasepool{
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    NSView* parent=[[TremoloTestCanvas alloc] initWithFrame:NSMakeRect(0,0,720,320)];
    Owner owner;std::unique_ptr<EditorContent> editor(t::createEditorContent());
    check(editor->attach((__bridge void*)parent,owner.services()),"passive native attach");
    for(auto size:{NSMakeSize(680,310),NSMakeSize(720,296),NSMakeSize(720,320),NSMakeSize(1120,460)}){
        parent.frame=NSMakeRect(0,0,size.width,size.height);editor->resize(size.width,size.height);editor->refresh(owner.view,{});
        check(visibleRotaries(parent)==3,"Simple exactly three shared rotaries");
        for(NSView* child in descendants(parent)){
            check(![child isKindOfClass:NSSlider.class],"no private NSSlider remains");
            if(rotary(child) && !child.isHiddenOrHasHiddenAncestor){
                const auto rect=[child convertRect:child.bounds toView:parent];check(NSContainsRect(parent.bounds,rect),"three complete shared controls fit legal content");
                check(child.bounds.size.width>=112 && child.bounds.size.height>=144,"full primary rotary geometry");
            }
        }
    }
    parent.frame=NSMakeRect(0,0,720,320);editor->resize(720,320);editor->refresh(owner.view,{});
    if(argc>1){std::filesystem::create_directories(argv[1]);screenshot(parent,std::string(argv[1])+"/simple-passive.png");}
    const auto before=owner.sound;set(owner,t::sync,1);const auto synced=owner.sound;
    editor->refresh(owner.view,{});check(primary(parent,t::division) && !primary(parent,t::rateHz),"Sync exposes original Division control");
    check(visibleRotaries(parent)==3 && owner.writes==0,"Sync view refresh preserves targets and control count");
    check(owner.sound.targets==synced.targets,"Sync view does not recalculate Free Rate or Division");
    set(owner,t::sync,0);editor->refresh(owner.view,{});check(primary(parent,t::rateHz) && !primary(parent,t::division),"Free restores original Frequency view");
    check(owner.sound.targets==before.targets,"Free saved exact state retained");
    set(owner,t::shape,4);set(owner,t::mix,50);set(owner,t::duty,70);set(owner,t::phase,360);
    const auto baseline=owner.sound;t::TremoloEngine a,b;check(a.prepare({48000,2048,2,2}) && b.prepare({48000,2048,2,2}),"audio prepare");a.applyTargets(baseline,0);b.applyTargets(baseline,0);
    std::array<double,173> input,l1,r1,l2,r2;input.fill(1);
    for(unsigned i=0;i<16;++i){owner.view.advanced=!owner.view.advanced;editor->refresh(owner.view,{});
        AudioBlock<double> first;first.inputChannels=first.outputChannels=2;first.samples=input.size();first.inputs={input.data(),input.data()};first.outputs={l1.data(),r1.data()};auto second=first;second.outputs={l2.data(),r2.data()};a.process(first,{});b.process(second,{});
        check(l1==l2 && r1==r2,"passive view toggles preserve exact LFO audio");check(owner.sound.targets==baseline.targets && owner.writes==0,"passive toggle preserves all hidden targets");
    }
    owner.view.advanced=true;editor->refresh(owner.view,{});check(visibleRotaries(parent)==12,"Advanced appends nine shared controls to three primaries");
    if(argc>1)screenshot(parent,std::string(argv[1])+"/advanced-passive.png");
    editor.reset();editor.reset(t::createEditorContent());check(editor->attach((__bridge void*)parent,owner.services()),"passive reopen");editor->resize(720,320);
    for(unsigned i=0;i<600;++i)editor->refresh(owner.view,{});
    check(owner.sound.targets==baseline.targets && owner.writes==0,"600 timer-equivalent refreshes produce zero edits");
    editor.reset();check(owner.starts==owner.ends,"passive teardown leaves balanced gestures");
    std::cout<<"PASS passive Cocoa view "<<checks<<" checks; shared rotary layout, Sync binding, full state, audio twins, reopen, 600 refresh reads; no native-input acceptance claimed\n";
}}
