#pragma once
#include "../state/State.hpp"
#include <limits>
#include <chrono>
namespace just {
inline std::uint64_t analysisNow() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
enum class AnalysisAvailability {unavailable,fresh,stale};
enum EffectAnalysisFields : std::uint32_t {analysisReduction=1,analysisWet=2,analysisModulation=4,analysisDelay=8,analysisGate=16,analysisSidechain=32,analysisProtection=64};
struct EffectAnalysisSample {
    std::uint32_t validFields=0,gateState=0,flags=0;
    double reductionDb=0,wet[2]{},modulation[2]{},phase[2]{},effectiveHz=0,delayMs[2]{};
};
class AnalysisTap {
public:
    virtual ~AnalysisTap()=default;
    virtual void pushSample(std::uint32_t blockOffset,const EffectAnalysisSample&) noexcept=0;
};
enum AnalysisFlags : std::uint32_t {analysisGap=1,analysisInputAligned=2,analysisInvalid=4,analysisPlaying=8,analysisTransportKnown=16,analysisBypassed=32,analysisOffline=64};
struct AnalysisHeader {
    std::uint64_t session=0,epoch=0,sequence=0,startSample=0,endSample=0,dropped=0;
    double sampleRate=0;
    std::uint32_t inputChannels=0,outputChannels=0,latencySamples=0,flags=0;
    std::uint64_t sourceNanoseconds=0; // monotonic audio production time; preserved across transport/FFT
};
struct Envelope {double peak=0,rms=0,minimum=0,maximum=0;};
struct AnalysisWindow {
    AnalysisHeader header{};
    std::array<Envelope,6> channels{}; // raw input L/R aligned by PDC, final output L/R, explicit wet contribution L/R
    std::uint32_t effectFields=0,gateState=0,effectFlags=0;
    double reductionDb=0,modulationMin[2]{},modulationMax[2]{},phase[2]{},effectiveHz=0,delayMs[2]{};
    double correlation[2]{};std::uint32_t correlationValid=0;
};
inline constexpr std::size_t analysisBatchCapacity=8,analysisSampleCapacity=1024,spectrumSize=2048,spectrumBins=spectrumSize/2+1;
struct AnalysisBatch {std::uint32_t count=0;std::array<AnalysisWindow,analysisBatchCapacity> windows{};};
struct AnalysisCursor {std::uint64_t session=0,epoch=0,sequence=0;};
struct SampleFrame {
    AnalysisHeader header{};std::uint32_t count=0;
    std::array<std::array<float,analysisSampleCapacity>,4> samples{};
};
struct SpectrumSnapshot {
    AnalysisHeader header{};std::uint32_t fftSize=spectrumSize;
    // Single-sided amplitude, coherent-gain calibrated Hann. Stereo = sqrt(mean channel power).
    std::array<std::array<float,spectrumBins>,2> amplitude{};
};
inline bool validAnalysisHeader(const AnalysisHeader& h) noexcept {
    return h.session && h.epoch && h.sequence && h.sourceNanoseconds && h.endSample>h.startSample && std::isfinite(h.sampleRate) && h.sampleRate>0 &&
        h.inputChannels>=1 && h.inputChannels<=2 && h.outputChannels>=1 && h.outputChannels<=2 && !(h.flags&~127u);
}
enum class AnalysisKind:std::uint32_t {envelope=1,samples=2};
struct AnalysisMessage {
    std::uint32_t version=3,bytes=sizeof(AnalysisMessage);Uid plugin{};
    AnalysisKind kind=AnalysisKind::envelope;
    AnalysisWindow window{};
    SampleFrame sampleFrame{};
    bool valid() const noexcept {
        if(version!=3 || bytes!=sizeof(*this))return false;
        if(kind==AnalysisKind::envelope){
            if(!validAnalysisHeader(window.header) || window.effectFields&~127u || window.correlationValid&~3u)return false;
            for(auto& c:window.channels)if(!std::isfinite(c.peak) || !std::isfinite(c.rms) || !std::isfinite(c.minimum) || !std::isfinite(c.maximum) || c.peak<0 || c.rms<0)return false;
            if(!std::isfinite(window.reductionDb) || window.reductionDb<0 || !std::isfinite(window.effectiveHz))return false;
            for(int c=0;c<2;++c)if(!std::isfinite(window.modulationMin[c]) || !std::isfinite(window.modulationMax[c]) || !std::isfinite(window.phase[c]) || !std::isfinite(window.delayMs[c]) || !std::isfinite(window.correlation[c]))return false;
            return true;
        }
        if(kind!=AnalysisKind::samples || !validAnalysisHeader(sampleFrame.header) || !sampleFrame.count || sampleFrame.count>analysisSampleCapacity || sampleFrame.header.endSample-sampleFrame.header.startSample!=sampleFrame.count)return false;
        for(auto& channel:sampleFrame.samples)for(unsigned i=0;i<sampleFrame.count;++i)if(!std::isfinite(channel[i]))return false;
        return true;
    }
};
}
