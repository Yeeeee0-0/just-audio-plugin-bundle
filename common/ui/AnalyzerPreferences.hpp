#pragma once
#include <cstdint>
namespace just {
enum class AnalyzerResponse:std::uint32_t {fast=0,medium=1,slow=2};
enum class AnalyzerSource:std::uint32_t {pre=0,post=1,both=2};
// Module UI preferences only. Never host parameters, SoundState or DSP settings.
// Enabled only by ModuleDefinition::extendedSpectrum (the EQ opt-in).
struct AnalyzerPreferences {
    std::uint32_t rangeDb=120;
    double tiltDbPerOctave=4.5; // fixed 1 kHz pivot in the module display
    std::uint32_t fftSize=4096;
    AnalyzerResponse response=AnalyzerResponse::medium;
    AnalyzerSource source=AnalyzerSource::both;
    double releaseDbPerSecond() const noexcept {return response==AnalyzerResponse::fast?72:response==AnalyzerResponse::slow?18:36;}
    bool operator==(const AnalyzerPreferences& other) const noexcept {
        return rangeDb==other.rangeDb && tiltDbPerOctave==other.tiltDbPerOctave && fftSize==other.fftSize && response==other.response && source==other.source;
    }
    bool valid() const noexcept {
        return (rangeDb==60 || rangeDb==90 || rangeDb==120) &&
            (tiltDbPerOctave==0 || tiltDbPerOctave==4.5) &&
            (fftSize==4096 || fftSize==8192) &&
            static_cast<std::uint32_t>(response)<=2 && static_cast<std::uint32_t>(source)<=2;
    }
};
// UI chunk v3 appends one bounded, explicitly encoded module extension to the
// unchanged v2 shared fields. The other modules continue writing UI chunk v2.
inline constexpr std::uint32_t analyzerPreferenceTag=0x414E4C59; // ANLY
inline constexpr std::uint32_t analyzerPreferenceVersion=1,analyzerPreferenceBytes=24;
}
