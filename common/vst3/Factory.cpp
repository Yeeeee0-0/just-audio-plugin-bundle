#if defined(__APPLE__) && !defined(JUST_OBJC_NAMESPACE)
#error "macOS VST3 products require a unique JUST_OBJC_NAMESPACE token (use just_add_plugin)"
#endif
#include "Processor.hpp"
#include "Controller.hpp"
#include "public.sdk/source/main/pluginfactory.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
bool InitModule(){return true;}
bool DeinitModule(){return true;}
static const auto& product=just::pluginIdentities[JUST_PLUGIN_INDEX];
static const auto& p=product.processor.words;
static const auto& c=product.controller.words;
BEGIN_FACTORY_DEF("Yee Huang","https://github.com/Yeeeee0-0/just-audio-plugin","")
DEF_CLASS2(INLINE_UID(p[0],p[1],p[2],p[3]),PClassInfo::kManyInstances,kVstAudioEffectClass,
    product.name,0,"Fx","0.1.0",kVstVersionString,just::Processor::createInstance)
DEF_CLASS2(INLINE_UID(c[0],c[1],c[2],c[3]),PClassInfo::kManyInstances,kVstComponentControllerClass,
    product.name,0,"","0.1.0",kVstVersionString,just::Controller::createInstance)
END_FACTORY
