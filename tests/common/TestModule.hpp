#pragma once
#include "common/vst3/Module.hpp"
namespace just::test {
struct Trace {
    bool extendedTelemetry=false;
    unsigned prepareCalls=0,resetCalls=0,applyCalls=0;std::int32_t offset=0;
    std::array<double,4> targets{};
    ProcessContext context{};std::uint32_t sidechainChannels=0;
};
extern Trace trace;
}
