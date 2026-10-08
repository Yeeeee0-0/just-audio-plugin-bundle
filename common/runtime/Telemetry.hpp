#pragma once
#include "../state/State.hpp"
#include <cstdint>
namespace just {
enum TelemetryFields : std::uint64_t {
    telemetryInputPeak=1ull<<0,telemetryOutputPeak=1ull<<1,telemetryInvalidInput=1ull<<2,
    telemetryLatency=1ull<<3,telemetryTempo=1ull<<4,telemetryPpq=1ull<<5,telemetryTransport=1ull<<6,
    telemetryGainReduction=1ull<<7,telemetryEffectiveRate=1ull<<8,telemetryEffectiveDelay=1ull<<9,
    telemetrySync=1ull<<10,telemetryProtection=1ull<<11,telemetrySidechain=1ull<<12,
    telemetryTail=1ull<<13,telemetryDuck=1ull<<14
};
inline constexpr std::uint64_t legacyTelemetryFields=telemetryInputPeak|telemetryOutputPeak|telemetryInvalidInput|telemetryLatency;
inline constexpr std::uint64_t allTelemetryFields=(1ull<<15)-1;
enum TelemetryFlags : std::uint32_t {
    telemetryFlagInvalidInput=1u<<0,telemetryFlagPlaying=1u<<1,telemetryFlagCycle=1u<<2,
    telemetryFlagSyncUnavailable=1u<<3,telemetryFlagProtection=1u<<4,
    telemetryFlagSidechainMissing=1u<<5,telemetryFlagSidechainSilent=1u<<6
};
inline constexpr std::uint32_t allTelemetryFlags=(1u<<7)-1;
// Existing four members remain first. Optional fields require an explicit mask;
// a successful legacy readTelemetry() publishes only its original four fields.
struct Telemetry {
    double inputPeak=0,outputPeak=0;std::uint32_t latencySamples=0;bool invalidInput=false;
    std::uint64_t validFields=legacyTelemetryFields;
    double gainReductionDb=0,effectiveRateHz=0,effectiveDelayMs=0,tailEnergy=0,duckDb=0;
    std::uint32_t flags=0;
};
// Fixed POD wire format v2. Same-build processor/controller, no raw pointers,
// boolean representation or platform-dependent size_t. Never sound/editor state.
struct RuntimeTelemetrySnapshot {
    std::uint32_t version=2,bytes=sizeof(RuntimeTelemetrySnapshot);
    Uid plugin{};
    std::uint64_t session=0,sequence=0,validFields=0;
    std::uint32_t queueContext=0,flags=0,latencySamples=0,reserved=0;
    double inputPeak=0,outputPeak=0,bpm=0,ppq=0,gainReductionDb=0,effectiveRateHz=0,effectiveDelayMs=0,tailEnergy=0,duckDb=0;
    std::uint64_t sourceNanoseconds=0; // monotonic processor publication time
    bool valid() const noexcept {
        if(version!=2 || bytes!=sizeof(*this) || !session || !sequence || !queueContext || !sourceNanoseconds || reserved ||
           (validFields&~allTelemetryFields) || (flags&~allTelemetryFlags))return false;
        if(((flags&telemetryFlagInvalidInput) && !(validFields&telemetryInvalidInput)) ||
           ((flags&(telemetryFlagPlaying|telemetryFlagCycle)) && !(validFields&telemetryTransport)) ||
           ((flags&telemetryFlagSyncUnavailable) && !(validFields&telemetrySync)) ||
           ((flags&telemetryFlagProtection) && !(validFields&telemetryProtection)) ||
           ((flags&(telemetryFlagSidechainMissing|telemetryFlagSidechainSilent)) && !(validFields&telemetrySidechain)))return false;
        const double values[]={inputPeak,outputPeak,bpm,ppq,gainReductionDb,effectiveRateHz,effectiveDelayMs,tailEnergy,duckDb};
        for(double v:values)if(!std::isfinite(v))return false;
        return inputPeak>=0 && outputPeak>=0 && gainReductionDb>=0 && effectiveRateHz>=0 && effectiveDelayMs>=0 && tailEnergy>=0 && duckDb>=0 &&
            (!(validFields&telemetryTempo) || bpm>0);
    }
};
static_assert(std::is_trivially_copyable<RuntimeTelemetrySnapshot>::value,"fixed telemetry only");
enum class TelemetryAvailability {unavailable,fresh,stale};
inline constexpr std::uint64_t telemetryStaleNanoseconds=500000000; // maximum source and receive age
}
