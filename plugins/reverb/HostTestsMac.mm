#import <Cocoa/Cocoa.h>
#include "Presets.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned starts=0,writes=0,ends=0;
    tresult PLUGIN_API beginEdit(ParamID) override{++starts;return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue v) override{check(std::isfinite(v),"gesture finite");++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{++ends;return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static bool roleTitle(NSString* title,NSString* name) {
    if([name isEqualToString:@"Advanced"])return [@[@"Advanced",@"Simple",@"高级",@"简易"] containsObject:title];
    if([name isEqualToString:@"Preset"])return [title hasPrefix:@"Preset"] || [title hasPrefix:@"预设"];
    if([name isEqualToString:@"Bypass"])return [title containsString:@"Bypass"] || [title containsString:@"旁路"];
    return [title isEqualToString:name];
}
static NSButton* button(NSView* parent,NSString* name) {
    for(NSView* child in parent.subviews){if([child isKindOfClass:NSButton.class] && roleTitle([(NSButton*)child title],name))return (NSButton*)child;if(auto nested=button(child,name))return nested;}return nil;
}
static void waitRefresh(){[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.12]];}
static void screenshot(NSView* v,const std::filesystem::path& p) {
    [v layoutSubtreeIfNeeded];auto* image=[v bitmapImageRepForCachingDisplayInRect:v.bounds];[v cacheDisplayInRect:v.bounds toBitmapImageRep:image];
    check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:p.c_str()] atomically:YES],"host screenshot");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc==4,"usage: host-tests plugin preset-root screenshots");[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);check(bool(module),error.c_str());HostApplication host;
        auto classes=module->getFactory().classInfos();check(classes.size()==2,"fixed processor/controller pair");
        auto component=module->getFactory().createInstance<IComponent>(classes[0].ID());auto reference=module->getFactory().createInstance<IComponent>(classes[0].ID());
        auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        check(component && reference && controller,"create loaded components");check(component->initialize(&host)==kResultOk && reference->initialize(&host)==kResultOk && controller->initialize(&host)==kResultOk,"initialize");
        check(controller->getParameterCount()==36,"36 actual host parameters");
        auto handler=owned(new Handler);controller->setComponentHandler(handler.get());
        FUnknownPtr<IAudioProcessor> audio(component),refAudio(reference);ProcessSetup setup{kRealtime,kSample64,256,48000};
        check(audio->setupProcessing(setup)==kResultOk && refAudio->setupProcessing(setup)==kResultOk,"prepare actual plugin");
        component->setActive(true);reference->setActive(true);audio->setProcessing(true);refAudio->setProcessing(true);
        const auto uid=just::pluginIdentities[1].processor;FUID processorID(uid.words[0],uid.words[1],uid.words[2],uid.words[3]);
        for(std::size_t p=0;p<std::size(just::reverb::presets);++p) {
            const auto& preset=just::reverb::presets[p];auto path=std::filesystem::path(argv[2])/preset.category/(std::string(preset.name)+".vstpreset");
            std::ifstream file(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});check(!bytes.empty(),"preset file exists");MemoryStream source(bytes.data(),bytes.size());
            check(PresetFile::loadPreset(&source,processorID,component.get(),controller.get()),"actual plugin loads SDK preset");
            auto expected=just::reverb::presetState(p,uid);MemoryStream stored;check(component->getState(&stored)==kResultOk,"pending preset complete state readable");just::SoundState decoded;
            check(just::decodeState(reinterpret_cast<const std::uint8_t*>(stored.getData()),stored.getSize(),uid,just::reverb::registry,decoded)==just::StateResult::ok && decoded.targets==expected.targets,"loaded preset includes every hidden target");
            for(std::size_t i=0;i<just::reverb::registry.count;++i)check(controller->getParamNormalized(just::reverb::parameters[i].id)==expected.targets[i],"controller matches loaded preset");
        }
        auto baseline=just::reverb::presetState(5,uid);auto bytes=just::encodeState(baseline,just::reverb::registry);
        MemoryStream aState(bytes.data(),bytes.size()),bState(bytes.data(),bytes.size()),uiState(bytes.data(),bytes.size());component->setState(&aState);reference->setState(&bState);controller->setComponentState(&uiState);
        MemoryStream oldUI;IBStreamer oldWriter(&oldUI,kLittleEndian);oldWriter.writeInt32(1);oldWriter.writeInt32(480);oldWriter.writeInt32(260);oldWriter.writeDouble(1);oldWriter.writeBool(false);oldUI.seek(0,IBStream::kIBSeekSet,nullptr);
        check(controller->setState(&oldUI)==kResultOk,"legacy schema-1 editor size restores");
        NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1120,584)];auto view=owned(controller->createView(ViewType::kEditor));
        ViewRect size;check(view && view->getSize(&size)==kResultOk && size.getWidth()==880 && size.getHeight()==485,"legacy view restores at seven-control minimum without changing sound");
        ViewRect tooSmall(0,0,480,260);check(view->checkSizeConstraint(&tooSmall)==kResultTrue && tooSmall.getWidth()==880 && tooSmall.getHeight()==485,"host resize clamps to seven-control minimum");
        check(view->onSize(&tooSmall)==kResultOk && tooSmall.getWidth()==880 && tooSmall.getHeight()==485,"onSize applies seven-control minimum");
        ViewRect full(0,0,1120,584);check(view->onSize(&full)==kResultOk,"Reverb editor can grow after compact restore");
        check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"attach actual VST3 native editor");
        auto* advanced=button(parent,@"Advanced");check(advanced && button(parent,@"Bypass") && button(parent,@"Preset"),"common title bar retained");waitRefresh();
        check(button(parent,@"Preset").enabled && std::size(just::reverb::factoryPresets)==14,"all 14 factory presets available in native menu");
        screenshot(parent,std::filesystem::path(argv[3])/"simple.png");
        std::array<double,256> input{},right{},leftA{},rightA{},leftB{},rightB{};double* inputs[]={input.data(),right.data()},*outA[]={leftA.data(),rightA.data()},*outB[]={leftB.data(),rightB.data()};
        AudioBusBuffers in{},aOut{},bOut{};in.numChannels=aOut.numChannels=bOut.numChannels=2;in.channelBuffers64=inputs;aOut.channelBuffers64=outA;bOut.channelBuffers64=outB;
        ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&aOut;
        for(int n=0;n<30;++n) {
            [advanced performClick:nil];waitRefresh();
            input.fill(0);right.fill(0);if(n==0)input[0]=right[0]=.5;
            data.outputs=&aOut;check(audio->process(data)==kResultOk,"editor instance processes");data.outputs=&bOut;check(refAudio->process(data)==kResultOk,"reference instance processes");
            check(leftA==leftB && rightA==rightB,"view toggles preserve bit-identical sounding tail");
            check(handler->writes==0,"view toggles emit no host edits");
        }
        [advanced performClick:nil];waitRefresh();screenshot(parent,std::filesystem::path(argv[3])/"advanced.png");
        MemoryStream savedUI;controller->getState(&savedUI);view->removed();view.reset();view=owned(controller->createView(ViewType::kEditor));check(view && view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"loaded editor reopens");waitRefresh();
        check(handler->writes==0,"reopen no target writes");view->removed();view.reset();
        check(audio->getLatencySamples()==0 && audio->getTailSamples()>48000,"real zero PDC and finite tail report");
        audio->setProcessing(false);refAudio->setProcessing(false);component->setActive(false);reference->setActive(false);controller->setComponentHandler(nullptr);controller->terminate();component->terminate();reference->terminate();
        std::cout<<"PASS actual VST3 factory, 36 IDs, 14 host-loaded complete presets, native attach/reopen, 30 automated view toggles with bit-identical live tail and zero sound writes; no NSWindow or physical-input acceptance\n";
    }
}
