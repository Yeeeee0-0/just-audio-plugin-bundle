#pragma once
#include <cstdint>
#include <cmath>
namespace just {
enum LayoutFields : std::uint32_t {layoutBuses=1,layoutSampleRate=2};
// Read-only runtime metadata. Never serialized as sound or editor state.
struct BusLayoutSnapshot {
    std::uint32_t version=1,validFields=0;
    std::uint64_t sequence=0,session=0;
    double sampleRate=0;
    std::uint32_t inputChannels=0,outputChannels=0,sidechainChannels=0;
    bool sidechainActive=false,active=false,offline=false;
    bool valid() const noexcept {
        if(version!=1 || (validFields&~std::uint32_t(3)) || !std::isfinite(sampleRate))return false;
        if(validFields&layoutBuses) {
            if(inputChannels<1 || inputChannels>2 || outputChannels<1 || outputChannels>2 || sidechainChannels>2)return false;
        } else if(inputChannels || outputChannels || sidechainChannels || sidechainActive)return false;
        if((validFields&layoutSampleRate) && !(validFields&layoutBuses))return false;
        return (validFields&layoutSampleRate)?sampleRate>0:sampleRate==0;
    }
};
}
