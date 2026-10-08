#pragma once
#include "State.hpp"
namespace just {
struct FactoryPreset {
    const char* key=nullptr;const char* name=nullptr;const char* category=nullptr;
    bool (*build)(Uid,SoundState&)=nullptr;
};
enum class PresetTransactionStatus:std::uint32_t {unavailable,idle,pending,applied,rejected};
inline bool sameSoundState(const SoundState& a,const SoundState& b,ParameterRegistry r) noexcept {
    if(!(a.plugin==b.plugin) || a.schemaVersion!=b.schemaVersion || a.engineVersion!=b.engineVersion || a.seed!=b.seed || a.configurationCount!=b.configurationCount)return false;
    for(std::size_t i=0;i<r.count;++i)if(a.targets[i]!=b.targets[i])return false;
    for(unsigned i=0;i<a.configurationCount;++i)if(a.configurations[i].id!=b.configurations[i].id || a.configurations[i].value!=b.configurations[i].value)return false;
    return true;
}
inline bool validCompleteState(const SoundState& s,Uid uid,ParameterRegistry r) noexcept {
    if(!(s.plugin==uid) || s.schemaVersion!=1 || s.engineVersion!=1 || s.configurationCount>64)return false;
    for(std::size_t i=0;i<r.count;++i)if(!std::isfinite(s.targets[i]) || s.targets[i]<0 || s.targets[i]>1)return false;
    for(unsigned i=0;i<s.configurationCount;++i){if(!std::isfinite(s.configurations[i].value))return false;for(unsigned j=0;j<i;++j)if(s.configurations[i].id==s.configurations[j].id)return false;}
    return true;
}
struct PresetRequest {
    std::uint64_t session=0,transaction=0,expectedEpoch=0,expectedRestore=0;
    SoundState desired{};
};
struct PresetPublication {
    SoundState state{};
    std::uint64_t epoch=1,transaction=0;
    PresetTransactionStatus status=PresetTransactionStatus::idle;
};
}
