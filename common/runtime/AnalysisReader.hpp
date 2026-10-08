#pragma once
#include "Analysis.hpp"
#include "ExtendedSpectrumTransform.hpp"
#include "../dsp/Realtime.hpp"
#include <complex>
#include <thread>
#include <chrono>
#include <memory>
namespace just {
// Single main-thread producer/UI readers. FFT owns a separate bounded worker queue.
class AnalysisReader {
    std::array<AnalysisWindow,1024> history{};std::size_t write=0,size=0;
    std::uint64_t envelopeReceived=0,samplesReceived=0,sourceHead=0,headEpoch=0;
    SampleFrame latestSamples{};
    struct QueuedSamples {SampleFrame frame{};std::uint64_t generation=0,configuration=0;std::uint32_t fftSize=0;double releaseRate=36;};
    struct PublishedSpectrum {SpectrumSnapshot frame{};std::uint64_t generation=0;};
    SpscQueue<QueuedSamples,16> fftQueue;
    AtomicSnapshot<PublishedSpectrum> spectrum;
    // Only the opted-in EQ allocates the large publication/workspace. The
    // processor wire message, raw frame and legacy SpectrumSnapshot stay fixed.
    struct ExtendedPublication {
        AtomicSnapshot<ExtendedSpectrumSnapshot> spectrum;
        std::atomic<std::uint64_t> generation{1},configuration{1};
    };
    std::unique_ptr<ExtendedPublication> extended;
    std::uint32_t extendedSize=4096;
    double extendedReleaseRate=36;
    std::uint64_t configurationGeneration=1,configuredAt=0;
    bool presentationPaused=false;
    std::uint64_t presentationGeneration=1,resumedAt=0;
    std::atomic<bool> running{false};std::thread worker;
    void invalidatePresentation() noexcept {
        ++presentationGeneration;if(extended)extended->generation.store(presentationGeneration,std::memory_order_release);
    }
    void work() {
        if(extended){
            auto transform=std::make_unique<ExtendedSpectrumTransform>();
            while(running.load(std::memory_order_acquire)){
                QueuedSamples queued;if(!fftQueue.pop(queued)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
                if(queued.generation!=extended->generation.load(std::memory_order_acquire) || queued.configuration!=extended->configuration.load(std::memory_order_acquire))continue;
                transform->accept(queued.frame,queued.generation,queued.configuration,queued.fftSize,queued.releaseRate,
                    [this](const ExtendedSpectrumSnapshot& s){
                        if(s.presentationGeneration==extended->generation.load(std::memory_order_acquire) && s.configurationGeneration==extended->configuration.load(std::memory_order_acquire))extended->spectrum.publish(s);
                    });
            }
            return;
        }
        std::array<std::array<double,spectrumSize>,4> ring{};
        std::array<std::complex<double>,spectrumSize> bins{};
        std::uint32_t used=0;std::uint64_t session=0,epoch=0,next=0,generation=0;
        auto transform=[&](){
            for(std::size_t i=1,j=0;i<spectrumSize;++i){std::size_t bit=spectrumSize>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(bins[i],bins[j]);}
            for(std::size_t length=2;length<=spectrumSize;length<<=1){auto angle=-2*3.14159265358979323846/length;std::complex<double> step(std::cos(angle),std::sin(angle));
                for(std::size_t start=0;start<spectrumSize;start+=length){std::complex<double> w(1,0);for(std::size_t j=0;j<length/2;++j){auto a=bins[start+j],b=bins[start+j+length/2]*w;bins[start+j]=a+b;bins[start+j+length/2]=a-b;w*=step;}}
            }
        };
        while(running.load(std::memory_order_acquire)){
            QueuedSamples queued;if(!fftQueue.pop(queued)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            const auto& frame=queued.frame;
            if(queued.generation!=generation || frame.header.session!=session || frame.header.epoch!=epoch || frame.header.startSample!=next){used=0;session=frame.header.session;epoch=frame.header.epoch;generation=queued.generation;}
            next=frame.header.endSample;
            if(!(frame.header.flags&analysisInputAligned) || frame.header.flags&analysisInvalid){used=0;continue;}
            for(unsigned i=0;i<frame.count;++i){
                for(unsigned ch=0;ch<4;++ch)ring[ch][used]=frame.samples[ch][i];
                if(++used!=spectrumSize)continue;
                SpectrumSnapshot result;result.header=frame.header;result.header.endSample=frame.header.startSample+i+1;result.header.startSample=result.header.endSample-spectrumSize;
                for(unsigned tap=0;tap<2;++tap){unsigned channels=tap?frame.header.outputChannels:frame.header.inputChannels;
                    for(unsigned ch=0;ch<channels;++ch){
                        double sum=0;for(unsigned j=0;j<spectrumSize;++j){double w=.5-.5*std::cos(2*3.14159265358979323846*j/(spectrumSize-1));sum+=w;bins[j]={ring[tap*2+ch][j]*w,0};}transform();
                        for(unsigned k=0;k<spectrumBins;++k){double factor=(k==0 || k==spectrumSize/2)?1:2;double amplitude=std::abs(bins[k])*factor/sum;result.amplitude[tap][k]+=float(amplitude*amplitude/channels);}
                    }
                    for(auto& v:result.amplitude[tap])v=std::sqrt(v);
                }
                spectrum.publish({result,generation});used=0;
            }
        }
    }
public:
    explicit AnalysisReader(bool extendedSpectrum=false){if(extendedSpectrum)extended=std::make_unique<ExtendedPublication>();}
    ~AnalysisReader(){stop();}
    // Main/UI thread only; no allocation, processor message or worker restart.
    bool configureExtendedSpectrum(std::uint32_t size,double releaseRate=36) noexcept {
        if(!extended || (size!=4096 && size!=8192) || (releaseRate!=18 && releaseRate!=36 && releaseRate!=72))return false;
        if(extendedSize!=size || extendedReleaseRate!=releaseRate){extendedSize=size;extendedReleaseRate=releaseRate;++configurationGeneration;configuredAt=analysisNow();extended->configuration.store(configurationGeneration,std::memory_order_release);}
        return true;
    }
    void start(){if(running.exchange(true))return;worker=std::thread([this]{work();});}
    void stop(){running.store(false,std::memory_order_release);if(worker.joinable())worker.join();}
    void clear(){write=size=0;envelopeReceived=samplesReceived=sourceHead=headEpoch=0;latestSamples={};invalidatePresentation();}
    // Main/UI thread only. No processor subscription/audio clock changes. Old
    // worker results cannot reappear even when session/epoch/sample positions match.
    void setPresentationPaused(bool paused) {
        if(presentationPaused==paused)return;
        presentationPaused=paused;clear();
        if(!paused)resumedAt=analysisNow();
    }
    void accept(const AnalysisMessage& m,std::uint64_t latestSource,std::uint64_t latestEpoch) {
        auto& h=m.kind==AnalysisKind::envelope?m.window.header:m.sampleFrame.header;
        if(h.epoch!=latestEpoch || h.endSample>latestSource)return;
        if(latestSource-h.endSample>h.sampleRate*.5)return; // queued stale data cannot renew UI freshness
        const auto now=analysisNow();
        if(!h.sourceNanoseconds || now<h.sourceNanoseconds || now-h.sourceNanoseconds>500000000ull)return;
        sourceHead=latestSource;headEpoch=latestEpoch;
        if(presentationPaused || h.sourceNanoseconds<=resumedAt)return;
        if(m.kind==AnalysisKind::samples){
            if(latestSamples.header.session==h.session && latestSamples.header.epoch==h.epoch && latestSamples.header.sequence>=h.sequence)return;
            if(extended){
                const auto& previous=latestSamples.header;
                if((previous.session && (previous.session!=h.session || previous.epoch!=h.epoch || previous.sequence+1!=h.sequence ||
                    previous.endSample!=h.startSample || previous.sampleRate!=h.sampleRate || previous.inputChannels!=h.inputChannels ||
                    previous.outputChannels!=h.outputChannels || previous.latencySamples!=h.latencySamples)) ||
                    (h.flags&(analysisInvalid|analysisGap)) || !(h.flags&analysisInputAligned))invalidatePresentation();
            }
            latestSamples=m.sampleFrame;samplesReceived=now;
            if(!extended){fftQueue.push({m.sampleFrame,presentationGeneration});return;}
            if(h.sourceNanoseconds<=configuredAt || (h.flags&analysisInvalid) || !(h.flags&analysisInputAligned))return;
            if(!fftQueue.push({m.sampleFrame,presentationGeneration,configurationGeneration,extendedSize,extendedReleaseRate}))invalidatePresentation();
            return;
        }
        if(size){auto& last=history[(write+history.size()-1)%history.size()].header;
            if(last.session!=h.session || last.epoch!=h.epoch){size=0;}
            else if(h.sequence<=last.sequence)return;
        }
        history[write]=m.window;write=(write+1)%history.size();size=std::min(size+1,history.size());envelopeReceived=now;
    }
    AnalysisAvailability availability(const AnalysisHeader& h,std::uint64_t received,std::uint64_t session) const noexcept {
        if(!received)return AnalysisAvailability::unavailable;
        if(h.session!=session || h.epoch!=headEpoch)return AnalysisAvailability::unavailable;
        const auto now=analysisNow();
        if(!h.sourceNanoseconds || now<h.sourceNanoseconds || now-h.sourceNanoseconds>500000000ull || now-received>500000000ull || sourceHead<h.endSample || sourceHead-h.endSample>h.sampleRate*.5)return AnalysisAvailability::stale;
        return AnalysisAvailability::fresh;
    }
    AnalysisAvailability read(std::uint64_t session,AnalysisCursor& cursor,AnalysisBatch& batch) const noexcept {
        batch={};if(!size)return AnalysisAvailability::unavailable;auto status=availability(history[(write+history.size()-1)%history.size()].header,envelopeReceived,session);if(status!=AnalysisAvailability::fresh)return status;
        for(std::size_t i=0;i<size && batch.count<analysisBatchCapacity;++i){
            auto item=history[(write+history.size()-size+i)%history.size()];auto& h=item.header;
            if(cursor.session==h.session && cursor.epoch==h.epoch && h.sequence<=cursor.sequence)continue;
            if(cursor.session!=h.session || cursor.epoch!=h.epoch || h.sequence!=cursor.sequence+1)h.flags|=analysisGap;
            batch.windows[batch.count++]=item;cursor={h.session,h.epoch,h.sequence};
        }return status;
    }
    AnalysisAvailability readSamples(std::uint64_t session,SampleFrame& out) const noexcept {
        out={};auto status=availability(latestSamples.header,samplesReceived,session);if(status==AnalysisAvailability::fresh)out=latestSamples;return status;
    }
    AnalysisAvailability readSpectrum(std::uint64_t session,SpectrumSnapshot& out) const noexcept {
        out={};if(extended)return AnalysisAvailability::unavailable;
        auto status=availability(latestSamples.header,samplesReceived,session);if(status!=AnalysisAvailability::fresh)return status;
        PublishedSpectrum published;if(!spectrum.read(published) || published.generation!=presentationGeneration)return AnalysisAvailability::unavailable;
        const auto& s=published.frame;if(s.header.session!=session || s.header.epoch!=headEpoch)return AnalysisAvailability::unavailable;
        if(availability(s.header,samplesReceived,session)!=AnalysisAvailability::fresh)return AnalysisAvailability::stale;
        out=s;return status;
    }
    AnalysisAvailability readExtendedSpectrum(std::uint64_t session,ExtendedSpectrumSnapshot& out) const noexcept {
        out={};if(!extended)return AnalysisAvailability::unavailable;
        auto status=availability(latestSamples.header,samplesReceived,session);if(status!=AnalysisAvailability::fresh)return status;
        ExtendedSpectrumSnapshot s;if(!extended->spectrum.read(s) || s.configurationGeneration!=configurationGeneration ||
            s.presentationGeneration!=presentationGeneration || s.header.session!=session || s.header.epoch!=headEpoch)return AnalysisAvailability::unavailable;
        if(availability(s.header,samplesReceived,session)!=AnalysisAvailability::fresh)return AnalysisAvailability::stale;
        out=s;return AnalysisAvailability::fresh;
    }
};
}
