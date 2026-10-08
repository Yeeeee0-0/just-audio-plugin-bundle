#pragma once
#include "SpectrumDisplay.hpp"
#include "common/runtime/ExtendedSpectrum.hpp"

namespace just::eq {
// Common owns FFT and every-hop peak capture. This model only transforms
// complete common snapshots for presentation; it has no audio worker.
class AnalyzerModel {
    std::uint64_t configuration=0,presentation=0,session=0,epoch=0,sequence=0,displayGeneration=0;
public:
    AnalyzerSettings settings;
    SpectrumDisplay display;
    ExtendedSpectrumSnapshot spectrum;
    AnalysisAvailability availability=AnalysisAvailability::unavailable;
    void reset() noexcept {
        display.clear();spectrum={};configuration=presentation=session=epoch=sequence=0;
        ++displayGeneration;availability=AnalysisAvailability::unavailable;
    }
    bool configure(const AnalyzerSettings& next) noexcept {
        if(!next.valid())return false;
        const bool changed=settings.fftSize!=next.fftSize || settings.response!=next.response;settings=next;
        if(changed)reset();return true;
    }
    void update(const ExtendedSpectrumSnapshot& next,AnalysisAvailability status,double seconds) noexcept {
        availability=status;
        if(status==AnalysisAvailability::unavailable){reset();return;}
        if(status==AnalysisAvailability::stale){display.noFreshData(seconds,settings);return;}
        if(!next.hasEnvelope || next.fftSize!=settings.fftSize || next.binCount!=next.fftSize/2+1 ||
           next.binCount>SpectrumDisplay::capacity || !validAnalysisHeader(next.header) || (next.header.flags&analysisInvalid)){
            reset();return;
        }
        if(configuration!=next.configurationGeneration || presentation!=next.presentationGeneration ||
           session!=next.header.session || epoch!=next.header.epoch ||
           ((next.header.flags&analysisGap) && sequence!=next.header.sequence)){
            display.clear();++displayGeneration;
        }
        configuration=next.configurationGeneration;presentation=next.presentationGeneration;
        session=next.header.session;epoch=next.header.epoch;sequence=next.header.sequence;spectrum=next;
        display.accept(next.amplitude[0].data(),next.amplitude[1].data(),next.binCount,next.fftSize,next.header.sampleRate,
                       displayGeneration,next.header.endSample,seconds,settings,next.envelopeDb[0].data(),next.envelopeDb[1].data(),
                       double(next.header.sourceNanoseconds)*1e-9);
    }
};
}
