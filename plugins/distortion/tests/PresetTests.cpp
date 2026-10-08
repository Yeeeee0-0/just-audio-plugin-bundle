#include "common/state/UserPresetStore.hpp"
#include "common/vst3/Module.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "plugins/distortion/Parameters.hpp"
#include "LegacyCleanState.hpp"
#include <chrono>
#include <iostream>
#include <limits>

// The actual module factory/validator is linked; this pure fixture has no editor.
namespace just::distortion {EditorContent* createEditorContent(){return nullptr;}}
using namespace just;
using namespace just::distortion;
static unsigned checks=0;
static void check(bool ok,const char* what){
    ++checks;if(!ok){std::cerr<<"FAIL "<<what<<'\n';std::exit(1);}
}
int main(){
    const auto& module=moduleDefinition();const auto uid=pluginIdentities[9].processor;
    check(module.valid() && module.validatePresetState,"actual module registers preset validator");
    check(registry.count==27 && quality==4240,"accepted complete registry and Quality ID retained");
    SoundState initial;
    check(effectiveFactoryPresetCount(module)==1 && buildFactoryPreset(module,0,uid,initial),"one real factory Default");
    const auto expected=initialState(uid,registry);
    check(sameSoundState(initial,expected,registry) && encodeState(initial,registry)==encodeState(expected,registry),"factory uses complete actual Init");
    check(physical(initial,model)==1 && physical(initial,drive_db)==0 && physical(initial,distortion::mix)==100 && physical(initial,quality)==2,"Soft/Drive/Mix/4x defaults unchanged");
    check(module.validatePresetState(initial),"Init can load through complete-state validator");

    auto saved=initial;saved.seed=0x123456789abcdef0ull;saved.revision=73;
    for(std::size_t i=0;i<registry.count;++i){
        const auto& p=parameters[i];
        saved.targets[i]=p.stepCount?double((i+1)%(p.stepCount+1))/p.stepCount:double(i+1)/(registry.count+1);
    }
    saved.targets[registry.index(quality)]=parameters[registry.index(quality)].toNormalized(2);
    check(module.validatePresetState(saved),"all supported hidden/user targets and seed allowed");
    const auto bytes=encodeState(saved,registry);SoundState decoded;
    check(decodeState(bytes.data(),bytes.size(),uid,registry,decoded)==StateResult::ok && sameSoundState(saved,decoded,registry),"complete 27-target state and seed roundtrip");
    check(module.validatePresetState(decoded) && encodeState(decoded,registry)==bytes,"roundtripped supported state remains canonical and loadable");
    for(unsigned ordinal:{0u,1u,3u}){
        auto pending=saved;pending.targets[registry.index(quality)]=double(ordinal)/3;
        const auto pendingBytes=encodeState(pending,registry);SoundState restored;
        check(!module.validatePresetState(pending),"unsupported 1x/2x/8x preset rejects whole state");
        check(decodeState(pendingBytes.data(),pendingBytes.size(),uid,registry,restored)==StateResult::ok && encodeState(restored,registry)==pendingBytes,"old pending Quality host state still restores unchanged");
        check(module.status(restored).qualityPending,"unsupported restored request remains honestly pending");
    }
    for(double target:{.5,.65,.7,std::nextafter(2./3,0.),std::nextafter(2./3,1.)}){
        auto offGrid=saved;offGrid.targets[registry.index(quality)]=target;
        check(!module.validatePresetState(offGrid),"rounded-to-4x noncanonical Quality request rejects");
    }
    for(std::uint32_t id:{0u,4240u,0xffffffffu}){
        auto unsupported=saved;unsupported.configurationCount=1;unsupported.configurations[0]={id,0};
        check(!module.validatePresetState(unsupported),"no configuration-array ID is supported, even zero-valued");
    }
    auto invalid=saved;invalid.configurationCount=65;
    check(!module.validatePresetState(invalid),"out-of-bounds configuration count rejects safely");
    invalid=saved;invalid.plugin=pluginIdentities[0].processor;
    check(!module.validatePresetState(invalid),"other plugin UID rejects");
    invalid=saved;invalid.schemaVersion=2;check(!module.validatePresetState(invalid),"unsupported schema rejects");
    invalid=saved;invalid.engineVersion=2;check(!module.validatePresetState(invalid),"unsupported engine version rejects");
    for(double bad:{-0.1,1.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        for(std::size_t i=0;i<registry.count;++i){invalid=saved;invalid.targets[i]=bad;check(!module.validatePresetState(invalid),"invalid hidden/visible target rejects full state");}
    }
    SoundState legacy;
    check(decodeState(legacyCleanState.data(),legacyCleanState.size(),uid,registry,legacy)==StateResult::ok && physical(legacy,model)==0,"immutable legacy Clean still restores");
    check(module.validatePresetState(legacy) && encodeState(legacy,registry)==std::vector<std::uint8_t>(legacyCleanState.begin(),legacyCleanState.end()),"supported legacy Clean preset is byte-exact and loadable");

    const auto root=std::filesystem::temp_directory_path()/("just-distortion-preset-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!std::filesystem::exists(root),"new isolated user-store root");
    UserPresetStore store(root,uid,registry),peer(root,uid,registry);
    check(store.list().presets.empty() && !std::filesystem::exists(root),"browse does not write to user storage");
    std::string id,error;UserPreset loaded;
    check(store.save("Complete Distortion",saved,id,error) && peer.load(id,loaded,error),"real user store save/load across instances");
    check(sameSoundState(saved,loaded.state,registry) && loaded.encodedSound==bytes && module.validatePresetState(loaded.state),"user file preserves full supported sound and remains transaction-loadable");
    check(peer.rename(id,"完整失真预设",error) && store.load(id,loaded,error) && loaded.encodedSound==bytes,"rename preserves complete sound bytes");
    std::string defaultID;
    check(store.save("Actual Init",initial,defaultID,error) && peer.load(defaultID,loaded,error) && sameSoundState(initial,loaded.state,registry) && module.validatePresetState(loaded.state),"actual factory Init user file roundtrips");
    // Storage preserves old pending values; the module's transaction validator
    // rejects their load rather than silently rewriting or partially applying.
    auto pending=saved;pending.targets[registry.index(quality)]=1;std::string pendingID;
    check(store.save("Old pending request",pending,pendingID,error) && peer.load(pendingID,loaded,error),"pending state can be preserved losslessly in storage");
    check(sameSoundState(pending,loaded.state,registry) && !module.validatePresetState(loaded.state),"unsupported stored preset is rejected without normalization");
    check(store.remove(id,error) && store.remove(defaultID,error) && store.remove(pendingID,error),"remove only isolated fixture presets");
    std::error_code cleanup;std::filesystem::remove_all(root,cleanup);
    check(!cleanup && !std::filesystem::exists(root),"fixture cleaned without production user-store access");
    std::cout<<"PASS "<<checks<<" Distortion actual Init, full user preset/state roundtrip, fixed4x configuration rejection and legacy pending/Clean compatibility; no GUI/audio/SDK host\n";
}
