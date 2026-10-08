#include "common/vst3/PluginIdentities.hpp"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <iostream>
#include <cstring>
extern "C" Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory();
int main(){using namespace Steinberg;auto* factory=GetPluginFactory();PFactoryInfo info{};
    if(!factory || factory->getFactoryInfo(&info)!=kResultOk || std::strcmp(info.vendor,"Yee Huang") || factory->countClasses()!=2)return 1;
    for(int i=0;i<2;++i){PClassInfo klass{};if(factory->getClassInfo(i,&klass)!=kResultOk)return 2;const auto& expected=i?just::pluginIdentities[0].controller:just::pluginIdentities[0].processor;const auto& w=expected.words;FUID uid(w[0],w[1],w[2],w[3]);
        if(std::memcmp(klass.cid,uid.toTUID(),sizeof(TUID)) || std::strcmp(klass.name,just::pluginIdentities[0].name))return 3;}
    factory->release();std::cout<<"PASS actual shared VST3 factory vendor Yee Huang, same processor/controller FUIDs and product name; no plugin instance/window created\n";
}
