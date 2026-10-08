#pragma once
#include "Analysis.hpp"
namespace just {
inline constexpr std::size_t maximumExtendedSpectrumSize=8192;
inline constexpr std::size_t maximumExtendedSpectrumBins=maximumExtendedSpectrumSize/2+1;
struct ExtendedSpectrumSnapshot {
    AnalysisHeader header{};
    std::uint32_t fftSize=0,binCount=0,hopSize=0;
    std::uint64_t configurationGeneration=0,presentationGeneration=0;
    // Calibrated, single-sided linear amplitude, independent Pre/Post; stereo
    // is sqrt(mean L/R power). No display tilt/range/envelope applied here.
    std::array<std::array<float,maximumExtendedSpectrumBins>,2> amplitude{};
    // Source-time peak envelope computed at EVERY FFT hop (instant attack;
    // Fast/Medium/Slow release 72/36/18 dB/s; -180 dB floor). Raw amplitude
    // above is unchanged. Tilt/range/pixel resampling remain module-owned.
    std::array<std::array<float,maximumExtendedSpectrumBins>,2> envelopeDb{};
    bool hasEnvelope=false;
};
// At least 75% overlap, with a nominal >=30 spectra/s where Fs permits it.
// Examples: 4096/48k ->1024; 8192/48k ->1024; 8192/96k ->2048.
inline std::uint32_t extendedSpectrumHop(std::uint32_t fftSize,double sampleRate) noexcept {
    auto hop=fftSize/4;while(hop>1 && double(hop)*30>sampleRate)hop/=2;return hop;
}
}
