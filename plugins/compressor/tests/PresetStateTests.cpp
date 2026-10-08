#include "../Engine.hpp"
#include "common/state/UserPresetStore.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>

// This pure test links the actual module and engine without a native editor.
// No UI method is exercised and no window or VST3 acceptance is claimed.
namespace just::compressor {EditorContent* createCompressorEditor(){return nullptr;}}
using namespace just;
static unsigned checks=0;
static void check(bool condition,const char* reason) {
    ++checks;if(!condition){std::cerr<<"FAIL "<<reason<<'\n';std::exit(1);}
}
int main(int argc,char** argv) {
    check(argc<=2,"optional isolated, nonexistent fixture directory only");
    const auto root=argc==2?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/
        ("just-compressor-preset-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!std::filesystem::exists(root),"never touch an existing preset directory");
    const auto uid=pluginIdentities[4].processor;
    const auto& m=moduleDefinition();
    check(m.valid() && m.parameters.count==21 && m.validatePresetState,"actual Compressor module registers reviewed validator");
    check(module::parameters[module::registry.index(119)].transition==Transition::continuous,
          "Lookahead Param119 remains continuous, not a prepared parameter");
    for(const auto& spec:module::parameters)
        check(spec.transition!=Transition::preparedConfiguration,"no other prepared parameter is authorized by this hook");
    const auto accepted=[&](const SoundState& s){return validCompleteState(s,uid,m.parameters) && m.validatePresetState(s);};

    SoundState init;
    check(m.factoryPresetCount==0 && effectiveFactoryPresetCount(m)==1 && buildFactoryPreset(m,0,uid,init),
          "exactly one shared factory fallback uses real Init");
    check(accepted(init) && init.configurationCount==0 && init.seed==0,"factory Init has no fabricated configuration");
    for(std::size_t i=0;i<m.parameters.count;++i)
        check(init.targets[i]==m.parameters.specs[i].toNormalized(m.parameters.specs[i].initial),"all literal registered Init values preserved");
    check(init.targets[module::registry.index(100)]==1 && init.targets[module::registry.index(119)]==0,
          "true Threshold/Lookahead defaults preserved");
    SoundState out;check(!buildFactoryPreset(m,1,uid,out),"no additional mock factory preset");

    UserPresetStore store(root,uid,m.parameters),peer(root,uid,m.parameters);
    std::string id,error;UserPreset loaded;
    check(store.list().presets.empty() && !std::filesystem::exists(root),"read-only listing does not create storage");
    check(store.save("Init fixture",init,id,error) && peer.load(id,loaded,error) &&
          accepted(loaded.state) && loaded.encodedSound==encodeState(init,m.parameters),"Init save/load remains exact");
    for(unsigned ordinal=0;ordinal<=2;++ordinal) {
        auto request=init;request.seed=0x123456789abcdef0;
        for(std::size_t i=0;i<m.parameters.count;++i)request.targets[i]=double(i+1)/(m.parameters.count+1);
        request.targets[module::registry.index(119)]=module::parameters[module::registry.index(119)].toNormalized(7.5);
        request.configurationCount=1;request.configurations[0]={module::maximumLookaheadConfigurationID,double(ordinal)};
        check(accepted(request),"reviewed Maximum ordinal accepted with retained continuous Lookahead request");
        const auto bytes=encodeState(request,m.parameters);SoundState decoded;
        check(decodeState(bytes.data(),bytes.size(),uid,m.parameters,decoded)==StateResult::ok &&
              sameSoundState(decoded,request,m.parameters) && accepted(decoded),"existing codec preserves all targets, seed and config1000");
        check(store.save("Maximum ordinal "+std::to_string(ordinal),request,id,error) && peer.load(id,loaded,error),
              "actual isolated user preset file saves and loads the complete request");
        check(loaded.encodedSound==bytes && sameSoundState(loaded.state,request,m.parameters) && accepted(loaded.state),
              "another store instance preserves complete sound bytes and reviewed configuration");
        std::unique_ptr<Engine> engine(m.createEngine());PrepareSpec preparation;
        preparation.inputChannels=preparation.outputChannels=1;preparation.maxBlockSize=64;
        check(engine && engine->prepare(preparation),"prepare actual engine outside the audio callback");
        engine->applyTargets(loaded.state,0);
        const auto pending=static_cast<compressor::CompressorEngine*>(engine.get())->pendingConfiguration();
        check(pending.pending && pending.requestedMaximumOrdinal==ordinal && pending.requestedLookaheadMs==7.5 &&
              pending.actualLatencySamples==0 && engine->latencySamples()==0 && engine->tailSamples().kind==TailKind::none,
              "preset only retains pending requests; actual Lookahead/PDC/tail unchanged");
        engine->applyTargets(init,0);
        check(!static_cast<compressor::CompressorEngine*>(engine.get())->pendingConfiguration().pending && engine->latencySamples()==0,
              "Init removes Maximum request without preparing or changing PDC");
    }
    auto noMaximum=init;
    noMaximum.targets[module::registry.index(119)]=.75;
    check(accepted(noMaximum),"absent Maximum does not discard saved continuous Lookahead target");
    auto maximumOnly=init;maximumOnly.configurationCount=1;maximumOnly.configurations[0]={1000,2};
    check(accepted(maximumOnly),"nonzero Maximum with zero Lookahead remains a valid pending request");

    auto bad=init;bad.configurationCount=1;
    for(double value:{-1.,.5,1.5,3.,5.,10.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        bad.configurations[0]={1000,value};
        check(!m.validatePresetState(bad),"Maximum requires finite integer ordinals 0/1/2, never milliseconds");
    }
    bad.configurations[0]={119,0};check(!accepted(bad),"Param119 cannot be used as a configuration ID");
    bad.configurations[0]={1001,0};check(!accepted(bad),"unknown configuration remains conservatively rejected");
    bad.configurationCount=2;bad.configurations[0]={1000,0};bad.configurations[1]={1000,1};
    check(!accepted(bad) && !m.validatePresetState(bad),"duplicate Maximum rejected, never silently merged");
    bad.configurations[1]={1001,0};check(!accepted(bad),"known-plus-unknown configuration rejected as a whole");
    bad.configurationCount=65;check(!accepted(bad) && !m.validatePresetState(bad),"configuration count bounded without array overread");
    bad=init;bad.plugin.words[0]^=1;check(!accepted(bad),"shared wrong-UID safeguard retained");
    bad=init;bad.schemaVersion=2;check(!accepted(bad),"shared schema safeguard retained");
    bad=init;bad.engineVersion=2;check(!accepted(bad),"shared engine-version safeguard retained");
    for(double value:{-.1,1.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        bad=init;bad.targets[module::registry.index(119)]=value;
        check(!accepted(bad),"shared finite/bounded sound-target safeguards retained");
    }
    check(store.list().presets.size()==4,"only four explicitly saved isolated fixture presets exist");
    std::cout<<"PASS "<<checks<<" actual Compressor validator/Init, complete isolated preset files, exact seed/config roundtrip and pending zero-PDC checks; no GUI or loaded-VST3 transaction acceptance\n";
    std::cout<<"Isolated preset fixture: "<<root<<'\n';
    if(argc==1)std::filesystem::remove_all(root);
}
