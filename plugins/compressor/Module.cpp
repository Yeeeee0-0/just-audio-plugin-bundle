#include "common/vst3/Module.hpp"
#include "Engine.hpp"
namespace just {
namespace {
Engine* createEngine(){return new compressor::CompressorEngine;}
const SimpleControlBinding bindings[]={
    {"Threshold",{100},1,true,nullptr},
    {"Ratio",{101},1,true,nullptr},
    {"Attack",{102},1,true,nullptr},
    {"Release",{103},1,true,nullptr}
};
const ParamID dynamics[]={104,105,106,107,108};
const ParamID gains[]={109,110,111,112};
const ParamID sidechain[]={113,114,115,116,117,118};
const ParamID latency[]={119};
const AdvancedGroup groups[]={{"Dynamics",dynamics,std::size(dynamics)},{"Gain and stereo",gains,std::size(gains)},
    {"Sidechain",sidechain,std::size(sidechain)},{"Lookahead (pending)",latency,std::size(latency)}};
bool validatePresetState(const SoundState& s) {
    // Common first validates UID, versions, all targets and configuration bounds.
    // Authorize only the reviewed saved request; this does not prepare Lookahead
    // or change the engine's pending/actual-zero-PDC behavior. Absent is Init.
    if(s.configurationCount>1)return false;
    if(!s.configurationCount)return true;
    const auto& c=s.configurations[0];
    return c.id==module::maximumLookaheadConfigurationID && std::isfinite(c.value) &&
        c.value>=0 && c.value<=2 && c.value==std::floor(c.value);
}
StatusSnapshot status(const SoundState& s) {
    bool pending=module::physical(s,119)>0;
    for(std::uint32_t i=0;i<std::min<std::uint32_t>(s.configurationCount,s.configurations.size());++i)
        pending|=s.configurations[i].id==module::maximumLookaheadConfigurationID && s.configurations[i].value>0;
    const bool punch=module::physical(s,104)>=0.5;
    StatusSnapshot result;
    result.mode=pending?(punch?"Punch · Lookahead pending · actual 0 samples":"Clean · Lookahead pending · actual 0 samples"):
        (punch?"Punch · actual 0 samples":"Clean · actual 0 samples");
    return result;
}
const ModuleDefinition definition=[] {
    ModuleDefinition configured{module::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),
    compressor::createCompressorEditor,status,{true,true,false,true}};
    configured.minimumEditorWidth=720;
    configured.minimumEditorHeight=420;
    configured.validatePresetState=validatePresetState;
    return configured;
}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
