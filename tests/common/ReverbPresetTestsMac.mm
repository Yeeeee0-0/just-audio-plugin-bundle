#import <Cocoa/Cocoa.h>
#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "Presets.hpp"
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
@interface NSView (PresetAcceptance)
- (NSMenu*)factoryPresetMenu;
- (void)refresh:(id)sender;
@end
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
class Handler final:public FObject,public IComponentHandler,public IComponentHandler2 {
public:
    unsigned starts=0,writes=0,ends=0,groups=0,finishes=0,dirty=0;std::array<bool,512> active{};
    tresult PLUGIN_API beginEdit(ParamID id) override{check(id<512 && !active[id],"preset begin unique");active[id]=true;++starts;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v) override{check(id<512 && active[id] && std::isfinite(v),"preset perform in gesture");++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID id) override{check(id<512 && active[id],"preset end balanced");active[id]=false;++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    tresult PLUGIN_API setDirty(TBool b) override{dirty+=b;return kResultOk;}
    tresult PLUGIN_API requestOpenEditor(FIDString) override{return kResultOk;}
    tresult PLUGIN_API startGroupEdit() override{++groups;return kResultOk;}
    tresult PLUGIN_API finishGroupEdit() override{++finishes;return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
        DEF_INTERFACE(IComponentHandler2)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static NSButton* findPreset(NSView* view){for(NSView* child in view.subviews){if([child isKindOfClass:NSButton.class] && child.tag==100)return (NSButton*)child;if(auto* nested=findPreset(child))return nested;}return nil;}
static std::vector<std::uint8_t> state(just::Processor& p){MemoryStream m;check(p.getState(&m)==kResultOk,"component state save");auto* b=reinterpret_cast<std::uint8_t*>(m.getData());return {b,b+m.getSize()};}
int main(int argc,char** argv){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];HostApplication host;auto p=owned(new just::Processor);auto reference=owned(new just::Processor);auto c=owned(new just::Controller);auto handler=owned(new Handler);
    check(p->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"actual Reverb processor/controller initialize");c->setComponentHandler(handler);p->connect(c);c->connect(p);
    ProcessSetup setup{kRealtime,kSample64,512,48000};check(p->setupProcessing(setup)==kResultOk && reference->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk && reference->setActive(true)==kResultOk,"prepare actual Reverb twins");
    NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,850,550) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;
    auto view=owned(c->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)window.contentView,kPlatformTypeNSView)==kResultOk,"native common editor attached");NSButton* preset=findPreset(window.contentView);check(preset && preset.enabled,"native preset menu is usable");NSView* shell=preset.target;check([shell respondsToSelector:@selector(loadPreset:)] && [shell respondsToSelector:@selector(undoPreset:)],"actual menu actions connected");
    NSMenu* menu=[shell factoryPresetMenu];check(menu.numberOfItems==16 && !menu.itemArray.lastObject.enabled,"real common menu has 14 entries, separator and disabled initial Undo");
    std::array<double,512> input{},outL{},outR{},refL{},refR{};double* ins[]={input.data(),input.data()},*outs[]={outL.data(),outR.data()},*refs[]={refL.data(),refR.data()};AudioBusBuffers in{},out{},refOut{};in.numChannels=out.numChannels=refOut.numChannels=2;in.channelBuffers64=ins;out.channelBuffers64=outs;refOut.channelBuffers64=refs;
    ProcessData data{};data.numSamples=512;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
    auto process=[&](bool twin=false){check(p->process(data)==kResultOk,"actual Reverb process");if(twin){auto d=data;d.outputs=&refOut;check(reference->process(d)==kResultOk && outL==refL && outR==refR,"preset route equals standard host state route sample-for-sample");}};
    auto restore=[&](just::Processor& processor,const just::SoundState& s){auto bytes=just::encodeState(s,just::reverb::registry);MemoryStream stream;int32 written=0;stream.write(bytes.data(),bytes.size(),&written);stream.seek(0,IBStream::kIBSeekSet,nullptr);check(processor.setState(&stream)==kResultOk,"host full state restore");};
    just::SoundState before;check(c->readCompleteSoundState(before),"read complete initial");before.seed=0x12345678abcdefull;just::reverb::set(before,just::reverb::Bell1Gain,3.456789);restore(*p,before);restore(*reference,before);process(true);p->onTimer(nullptr);check(c->setComponentState(nullptr)==kResultFalse,"bad controller restore rejected");
    const auto beforeBytes=state(*p);const bool initialAdvanced=c->viewState.advanced;
    for(unsigned i=0;i<std::size(just::reverb::presets);++i){
        NSMenuItem* item=[[shell factoryPresetMenu] itemAtIndex:i];check(item.tag==i && item.enabled && [item.title containsString:[NSString stringWithUTF8String:just::reverb::presets[i].name]],"real native entry matches stable catalog order and label");
        check([NSApp sendAction:item.action to:item.target from:item],"invoke actual native preset menu action");check(c->readPresetTransaction()==just::PresetTransactionStatus::pending && !preset.enabled && [preset.title isEqualToString:@"Pending…"],"native load awaits audio applied and disables repeated selection");
        auto desired=just::reverb::presetState(i,before.plugin);desired.targets[0]=before.targets[0];restore(*reference,desired);input.fill(0);input[0]=.7;process(true);p->onTimer(nullptr);
        [shell refresh:nil];just::SoundState actual;check(c->readPresetTransaction()==just::PresetTransactionStatus::applied && preset.enabled && c->readCompleteSoundState(actual) && just::sameSoundState(actual,desired,just::reverb::registry),"all hidden targets and seed loaded and menu reenabled");
        for(unsigned block=0;block<8;++block){input.fill(0);process(true);}check(c->viewState.advanced==initialAdvanced,"preset preserves view state");
        NSMenuItem* undo=[shell factoryPresetMenu].itemArray.lastObject;
        check(c->canUndoLastPreset() && undo.enabled && [NSApp sendAction:undo.action to:undo.target from:undo],"actual native undo action");restore(*reference,before);process(true);p->onTimer(nullptr);[shell refresh:nil];check(state(*p)==beforeBytes && !c->canUndoLastPreset() && ![shell factoryPresetMenu].itemArray.lastObject.enabled,"native undo restores precise complete before bytes and disables consumed Undo");
        std::cout<<"verified "<<just::reverb::presets[i].name<<"\n";
    }
    check(handler->starts==handler->writes && handler->writes==handler->ends && handler->groups==28 && handler->finishes==28 && handler->dirty==28,"14 loads plus 14 undo host groups balanced");
    if(argc>1){[window.contentView layoutSubtreeIfNeeded];NSBitmapImageRep* bitmap=[window.contentView bitmapImageRepForCachingDisplayInRect:window.contentView.bounds];[window.contentView cacheDisplayInRect:window.contentView.bounds toBitmapImageRep:bitmap];NSData* png=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];check([png writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES],"native Reverb menu fixture screenshot");}
    check(view->removed()==kResultOk,"editor removed");view=nullptr;[window close];p->setActive(false);reference->setActive(false);p->disconnect(c);c->disconnect(p);c->setComponentHandler(nullptr);c->terminate();p->terminate();reference->terminate();std::cout<<"PASS "<<checks<<" actual Reverb 14-preset native/full-state/undo/audio checks\n";
}}
