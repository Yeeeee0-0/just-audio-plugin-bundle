#pragma once
#include "../parameters/Parameters.hpp"
#include <vector>
#include <type_traits>
namespace just {
struct Uid {std::array<std::uint32_t,4> words{};};
inline bool operator==(const Uid& a,const Uid& b) noexcept {return a.words==b.words;}
struct ConfigurationValue {std::uint32_t id=0;double value=0;};
struct SoundState {
    Uid plugin{};
    std::uint32_t schemaVersion=1, engineVersion=1;
    std::uint64_t seed=0, revision=0; // revision is internal, never serialized.
    std::array<double,maxParameters> targets{};
    std::array<ConfigurationValue,64> configurations{};
    std::uint32_t configurationCount=0;
};
inline SoundState initialState(Uid identity,ParameterRegistry registry) noexcept {
    SoundState s;s.plugin=identity;
    for(std::size_t i=0;i<registry.count;++i) s.targets[i]=registry.specs[i].toNormalized(registry.specs[i].initial);
    return s;
}
inline constexpr std::size_t maxStateBytes=16384;
// Binary state is bounded, little endian, complete, and independent of view state.
// Codec calls are non-real-time. Unknown ParamIDs are consumed and ignored.
inline std::vector<std::uint8_t> encodeState(const SoundState& s,ParameterRegistry registry) {
    std::vector<std::uint8_t> result;
    result.reserve(48+12*(registry.count+s.configurationCount));
    auto integer=[&](std::uint64_t n,int bytes){for(int i=0;i<bytes;++i) result.push_back(std::uint8_t(n>>(8*i)));};
    auto floating=[&](double v){std::uint64_t bits;std::memcpy(&bits,&v,8);integer(bits,8);};
    integer(0x3154534a,4);
    for(auto word:s.plugin.words) integer(word,4);
    integer(s.schemaVersion,4);integer(s.engineVersion,4);integer(s.seed,8);
    integer(registry.count,4);integer(s.configurationCount,4);
    for(std::size_t i=0;i<registry.count;++i){integer(registry.specs[i].id,4);floating(s.targets[i]);}
    for(std::size_t i=0;i<s.configurationCount;++i){integer(s.configurations[i].id,4);floating(s.configurations[i].value);}
    return result;
}
enum class StateResult {ok,malformed,wrongPlugin,unsupportedSchema};
inline StateResult decodeState(const std::uint8_t* bytes,std::size_t size,Uid identity,ParameterRegistry registry,SoundState& destination) noexcept {
    if(!bytes || size<44 || size>maxStateBytes || !registry.valid()) return StateResult::malformed;
    std::size_t at=0;bool valid=true;
    auto integer=[&](int count)->std::uint64_t {
        if(at+count>size){valid=false;return 0;}
        std::uint64_t n=0;for(int i=0;i<count;++i)n|=std::uint64_t(bytes[at++])<<(8*i);return n;
    };
    auto floating=[&](){auto bits=integer(8);double value;std::memcpy(&value,&bits,8);return value;};
    if(integer(4)!=0x3154534a) return StateResult::malformed;
    SoundState staged=initialState(identity,registry);
    Uid encoded;for(auto& word:encoded.words)word=static_cast<std::uint32_t>(integer(4));
    if(!(encoded==identity))return StateResult::wrongPlugin;
    staged.schemaVersion=static_cast<std::uint32_t>(integer(4));
    staged.engineVersion=static_cast<std::uint32_t>(integer(4));
    if(staged.schemaVersion!=1 || staged.engineVersion!=1) return StateResult::unsupportedSchema;
    staged.seed=integer(8);
    const auto count=integer(4);const auto configCount=integer(4);
    if(count>maxParameters || configCount>64 || 44+12*(count+configCount)!=size) return StateResult::malformed;
    std::array<bool,maxParameters> seen{};
    for(std::size_t n=0;n<count;++n) {
        const auto id=static_cast<ParamID>(integer(4));const auto value=floating();
        if(!std::isfinite(value) || value<0 || value>1) return StateResult::malformed;
        const auto i=registry.index(id);
        if(i<registry.count) {
            if(seen[i])return StateResult::malformed;
            seen[i]=true;staged.targets[i]=value;
        }
    }
    staged.configurationCount=static_cast<std::uint32_t>(configCount);
    for(std::size_t i=0;i<configCount;++i) {
        staged.configurations[i].id=static_cast<std::uint32_t>(integer(4));
        staged.configurations[i].value=floating();
        if(!std::isfinite(staged.configurations[i].value)) return StateResult::malformed;
        for(std::size_t j=0;j<i;++j) if(staged.configurations[j].id==staged.configurations[i].id)return StateResult::malformed;
    }
    if(!valid || at!=size)return StateResult::malformed;
    for(std::size_t i=0;i<registry.count;++i)if(!seen[i] && registry.specs[i].restoreMissingDefault)
        staged.targets[i]=registry.specs[i].toNormalized(*registry.specs[i].restoreMissingDefault);
    destination=staged;return StateResult::ok;
}
}
