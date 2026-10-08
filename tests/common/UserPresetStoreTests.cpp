#include "common/state/UserPresetStore.hpp"
#include "common/ui/PresentationClock.hpp"
#include "common/vst3/Module.hpp"
#include "plugins/eq/Parameters.hpp"
#include "plugins/limiter/Parameters.hpp"
#include "plugins/reverb/Presets.hpp"
#include <iostream>
#include <thread>
#include <atomic>
using namespace just;
static unsigned checks=0;
static void check(bool b,const char* what){++checks;if(!b){std::cerr<<"FAIL "<<what<<'\n';std::exit(1);}}
static Engine* noEngine(){return nullptr;}
int main(int argc,char** argv){
    check(argc==2,"explicit isolated writable root required");const std::filesystem::path root=argv[1];
    check(!std::filesystem::exists(root),"fixture never overwrites an existing directory");
    Uid eqUID{{1,2,3,4}},limiterUID{{5,6,7,8}};
    UserPresetStore eqStore(root,eqUID,eq::registry),eqPeer(root,eqUID,eq::registry),limiterStore(root,limiterUID,limiter::registry);
    check(eqStore.list().presets.empty() && !std::filesystem::exists(root),"construction/listing do not touch disk");
    ModuleDefinition m;m.parameters=limiter::registry;m.createEngine=noEngine;
    SoundState initial;check(effectiveFactoryPresetCount(m)==1 && buildFactoryPreset(m,0,limiterUID,initial),"unregistered factory list has exactly one Default");
    for(std::size_t i=0;i<m.parameters.count;++i)check(initial.targets[i]==m.parameters.specs[i].toNormalized(m.parameters.specs[i].initial),"factory Default uses actual registered init, not zero");
    check(initial.targets[limiter::algorithmVersion]==1,"new Limiter factory preset uses algorithm 1");
    auto oldRegistry=limiter::registry;--oldRegistry.count;auto oldBytes=encodeState(initialState(limiterUID,oldRegistry),oldRegistry);SoundState old;
    check(decodeState(oldBytes.data(),oldBytes.size(),limiterUID,limiter::registry,old)==StateResult::ok && old.targets[limiter::algorithmVersion]==0,"old missing Limiter algorithm still restores 0");
    m.parameters=reverb::registry;m.factoryPresets=reverb::factoryPresets;m.factoryPresetCount=std::size(reverb::factoryPresets);
    check(effectiveFactoryPresetCount(m)==14,"Reverb's 14 existing factories preserved");
    for(unsigned i=0;i<14;++i){SoundState through,direct;check(buildFactoryPreset(m,i,eqUID,through) && reverb::factoryPresets[i].build(eqUID,direct) && sameSoundState(through,direct,reverb::registry),"Reverb factory builder unchanged");}
    check(!buildFactoryPreset(m,14,eqUID,initial),"factory index is bounded");
    SoundState eq=initialState(eqUID,eq::registry);eq.seed=0x123456789abcdef0;eq.configurationCount=2;eq.configurations[0]={701,3.25};eq.configurations[1]={702,-42};
    for(std::size_t i=0;i<eq::registry.count;++i)eq.targets[i]=double(i+1)/(eq::registry.count+1);
    check(eq::registry.count==195 && limiter::registry.count==11,"test uses accepted 195/11 parameter registries");
    std::string id,error;check(eqStore.save("我的完整预设",eq,id,error),"save full 195-parameter EQ sound");
    UserPreset read;check(eqPeer.load(id,read,error) && sameSoundState(eq,read.state,eq::registry),"another instance reads all parameters, seed and configurations exactly");
    check(read.name=="我的完整预设" && read.encodedSound==encodeState(eq,eq::registry),"payload is existing sound codec only, with UTF-8 metadata envelope");
    check(limiterStore.list().presets.empty() && !limiterStore.load(id,read,error),"UID directories isolate plugins");
    const auto before=eqStore.list().presets[0].encodedSound;
    check(eqPeer.rename(id,"Recalled sound",error) && eqStore.load(id,read,error) && read.encodedSound==before,"rename keeps sound bytes unchanged across instances");
    std::string duplicate;check(!eqStore.save("Recalled sound",eq,duplicate,error),"same name cannot overwrite a user preset");
    check(!eqStore.rename(id,"\ninvalid",error) && !eqStore.remove("../escape",error),"invalid names and path-shaped IDs rejected");
    check(!UserPresetStore::validName(std::string("\xc0\xaf",2)) && !UserPresetStore::validName(" ") && !UserPresetStore::validName(std::string(241,'x')),"UTF-8, empty-space and length bounds");
    auto wrong=eq;wrong.plugin=limiterUID;check(!eqStore.save("wrong plugin",wrong,duplicate,error),"wrong UID never saved");
    auto unknown=root/"unrelated.txt";{std::ofstream f(unknown);f<<"keep me";}
    auto corrupt=eqStore.path()/"00000000000000000000000000000000.justpreset";{std::ofstream f(corrupt);f<<"bad";}
    check(eqStore.list().unreadable==1 && eqStore.list().presets.size()==1,"corrupt entries skipped without being removed");
    // Future unknown IDs remain byte-for-byte intact when only renaming metadata.
    const auto futureID=std::string(32,'f');auto sound=encodeState(eq,eq::registry);sound[36]=std::uint8_t(eq::registry.count+1);sound[37]=0;
    const std::uint32_t unknownID=900001;const double unknownValue=.375;std::uint64_t bits;std::memcpy(&bits,&unknownValue,8);std::vector<std::uint8_t> entry;
    for(unsigned i=0;i<4;++i)entry.push_back(std::uint8_t(unknownID>>(8*i)));for(unsigned i=0;i<8;++i)entry.push_back(std::uint8_t(bits>>(8*i)));
    sound.insert(sound.begin()+44+12*eq::registry.count,entry.begin(),entry.end());
    {std::ofstream f(eqStore.path()/(futureID+".justpreset"),std::ios::binary);f.write("JUP1\x06\0\0\0" "Future",14);f.write(reinterpret_cast<const char*>(sound.data()),sound.size());}
    check(eqStore.rename(futureID,"Future renamed",error) && eqStore.load(futureID,read,error) && read.encodedSound==sound,"rename preserves unknown serialized parameter entries too");
    std::atomic<unsigned> successes{0};auto saveMany=[&](int owner){UserPresetStore store(root,eqUID,eq::registry);for(unsigned i=0;i<8;++i){std::string id,e;if(store.save("instance-"+std::to_string(owner)+"-"+std::to_string(i),eq,id,e))++successes;}};
    std::thread first(saveMany,1),second(saveMany,2);first.join();second.join();check(successes==16 && eqStore.list().presets.size()==18,"concurrent instances keep all atomic saves");
    check(eqStore.remove(id,error) && !eqPeer.load(id,read,error),"explicit delete removes only selected user preset");
    check(std::filesystem::exists(unknown) && std::filesystem::exists(corrupt) && eqStore.list().presets.size()==17,"unrelated, corrupt, and other user files preserved");
    PresentationClock clock;check(clock.now(1000)==1000,"normal presentation clock unchanged");clock.setPaused(true,1500);clock.setPaused(true,1900);check(clock.now(9000)==1500,"pause is stable and idempotent");clock.setPaused(false,9500);check(clock.now(10000)==2000,"resume omits paused elapsed time");clock.setPaused(false,10001);check(clock.now(11500)==3500,"EQ normal 3-second interval remains exact");
    std::cout<<"PASS "<<checks<<" real-registry default, complete user preset persistence/isolation/concurrency, unchanged Reverb factory, and presentation clock checks\n";
}
