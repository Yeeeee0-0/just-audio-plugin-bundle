#pragma once
#include "ExtendedSpectrum.hpp"
#include <complex>
namespace just {
// Used only by AnalysisReader's existing non-RT worker. Allocate once on that
// worker's heap, not its small platform thread stack or the audio thread.
class ExtendedSpectrumTransform {
    std::array<std::array<double,maximumExtendedSpectrumSize>,4> ring{};
    std::array<std::complex<double>,maximumExtendedSpectrumSize> bins{};
    std::array<double,maximumExtendedSpectrumSize> window{};
    std::array<std::array<float,maximumExtendedSpectrumBins>,2> envelope{};
    AnalysisHeader previous{};
    std::uint64_t generation=0,configuration=0,sequence=0;
    std::uint32_t length=0,hop=0,position=0,filled=0,sinceSpectrum=0;
    double windowSum=0;
    std::uint64_t envelopeTime=0;
    bool gap=true;
    void transform() noexcept {
        for(std::size_t i=1,j=0;i<length;++i){std::size_t bit=length>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(bins[i],bins[j]);}
        for(std::size_t n=2;n<=length;n<<=1){const double angle=-2*3.14159265358979323846/n;const std::complex<double> step(std::cos(angle),std::sin(angle));
            for(std::size_t start=0;start<length;start+=n){std::complex<double> w(1,0);for(std::size_t j=0;j<n/2;++j){const auto a=bins[start+j],b=bins[start+j+n/2]*w;bins[start+j]=a+b;bins[start+j+n/2]=a-b;w*=step;}}
        }
    }
public:
    template<class Publish> void accept(const SampleFrame& frame,std::uint64_t presentation,
                                       std::uint64_t config,std::uint32_t size,double releaseRate,Publish&& publish) {
        const auto& h=frame.header;
        if(length!=size){length=size;windowSum=0;for(unsigned i=0;i<length;++i){window[i]=.5-.5*std::cos(2*3.14159265358979323846*i/(length-1));windowSum+=window[i];}}
        if(generation!=presentation || configuration!=config || previous.session!=h.session || previous.epoch!=h.epoch ||
           previous.endSample!=h.startSample || previous.sampleRate!=h.sampleRate || previous.inputChannels!=h.inputChannels ||
           previous.outputChannels!=h.outputChannels || previous.latencySamples!=h.latencySamples || (h.flags&analysisGap)){
            position=filled=sinceSpectrum=0;envelopeTime=0;gap=true;
        }
        generation=presentation;configuration=config;previous=h;hop=extendedSpectrumHop(length,h.sampleRate);
        if(!(h.flags&analysisInputAligned) || (h.flags&analysisInvalid)){position=filled=sinceSpectrum=0;envelopeTime=0;gap=true;return;}
        for(unsigned i=0;i<frame.count;++i){
            for(unsigned ch=0;ch<4;++ch)ring[ch][position]=frame.samples[ch][i];
            position=(position+1)%length;
            if(filled<length){if(++filled<length)continue;}
            else if(++sinceSpectrum<hop)continue;
            sinceSpectrum=0;ExtendedSpectrumSnapshot result;
            result.header=h;result.header.sequence=++sequence;result.header.endSample=h.startSample+i+1;
            result.header.startSample=result.header.endSample-length;
            const auto remaining=std::uint64_t(double(frame.count-i-1)*1e9/h.sampleRate);
            result.header.sourceNanoseconds=h.sourceNanoseconds>remaining?h.sourceNanoseconds-remaining:1;
            if(gap)result.header.flags|=analysisGap;
            result.fftSize=length;result.binCount=length/2+1;result.hopSize=hop;
            result.configurationGeneration=configuration;result.presentationGeneration=generation;
            for(unsigned tap=0;tap<2;++tap){const unsigned channels=tap?h.outputChannels:h.inputChannels;
                for(unsigned ch=0;ch<channels;++ch){
                    for(unsigned j=0;j<length;++j)bins[j]={ring[tap*2+ch][(position+j)%length]*window[j],0};
                    transform();
                    for(unsigned k=0;k<result.binCount;++k){const double factor=(k==0 || k==length/2)?1:2;
                        const double amplitude=std::abs(bins[k])*factor/windowSum;result.amplitude[tap][k]+=float(amplitude*amplitude/channels);
                    }
                }
                for(unsigned k=0;k<result.binCount;++k)result.amplitude[tap][k]=std::sqrt(result.amplitude[tap][k]);
            }
            const bool reset=!envelopeTime || result.header.sourceNanoseconds<envelopeTime;
            const double fall=reset?0:releaseRate*double(result.header.sourceNanoseconds-envelopeTime)*1e-9;
            for(unsigned tap=0;tap<2;++tap)for(unsigned k=0;k<result.binCount;++k){
                const auto amplitude=result.amplitude[tap][k];const double target=amplitude>0?std::max(-180.,20*std::log10(double(amplitude))):-180.;
                result.envelopeDb[tap][k]=envelope[tap][k]=float(reset?target:std::max(target,double(envelope[tap][k])-fall));
            }
            envelopeTime=result.header.sourceNanoseconds;result.hasEnvelope=true;publish(result);gap=false;
        }
    }
};
}
