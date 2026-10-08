#include "Presets.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"usage: just_reverb_export_presets <output-directory>\n";return 1;}
    std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    const auto uid=just::pluginIdentities[1].processor;FUID classID(uid.words[0],uid.words[1],uid.words[2],uid.words[3]);
    for(std::size_t i=0;i<std::size(just::reverb::presets);++i) {
        auto state=just::reverb::presetState(i,uid);auto encoded=just::encodeState(state,just::reverb::registry);
        MemoryStream component(encoded.data(),encoded.size()),preset;
        if(!PresetFile::savePreset(&preset,classID,static_cast<IBStream*>(&component)))return 2;
        preset.seek(0,IBStream::kIBSeekSet,nullptr);PresetFile verify(&preset);
        if(!verify.readChunkList() || verify.getClassID()!=classID || !verify.contains(kComponentState))return 3;
        const auto& p=just::reverb::presets[i];auto folder=root/p.category;std::filesystem::create_directories(folder);
        std::ofstream file(folder/(std::string(p.name)+".vstpreset"),std::ios::binary);file.write(preset.getData(),preset.getSize());if(!file)return 4;
    }
    std::cout<<"PASS exported 14 complete .vstpreset component states with SDK-verified class/chunk headers\n";
}
