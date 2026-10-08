#include "common/runtime/AnalysisReader.hpp"
#include <iostream>
#include <vector>
using namespace just;
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
static SampleFrame frame(std::uint64_t start,double fs,unsigned n,double level=.2,unsigned channels=2){
    SampleFrame f;f.count=1024;f.header={1,1,start/1024+1,start,start+1024,0,fs,channels,channels,0,analysisInputAligned};
    f.header.sourceNanoseconds=1000000000ull+std::uint64_t(double(start+1024)*1e9/fs);
    for(unsigned i=0;i<f.count;++i){const float x=float(level*std::sin(2*3.14159265358979323846*64*(start+i)/n));
        f.samples[0][i]=x;f.samples[1][i]=-x;f.samples[2][i]=2*x;f.samples[3][i]=2*x;}
    return f;
}
static void numerical(){
    check(spectrumSize==2048 && spectrumBins==1025 && analysisSampleCapacity==1024,"legacy capacities unchanged");
    for(double fs:{44100.,48000.,96000.})for(unsigned n:{4096u,8192u})for(unsigned channels:{1u,2u}){
        auto transform=std::make_unique<ExtendedSpectrumTransform>();std::vector<ExtendedSpectrumSnapshot> spectra;
        const auto hop=extendedSpectrumHop(n,fs);const auto end=n+4*hop;
        for(unsigned start=0;start<end;start+=1024)transform->accept(frame(start,fs,n,.2,channels),7,3,n,36,[&](const auto& s){spectra.push_back(s);});
        check(spectra.size()==5,"full first window followed by four overlapping hops");
        for(unsigned i=0;i<spectra.size();++i){const auto& s=spectra[i];
            check(s.fftSize==n && s.binCount==n/2+1 && s.hopSize==hop && s.header.endSample==n+i*hop && s.header.startSample==i*hop,"actual FFT size/bin count/hop/source interval");
            check(s.header.sampleRate==fs && s.configurationGeneration==3 && s.presentationGeneration==7,"rate and both identities preserved");
            check(std::abs(s.amplitude[0][64]-.2)<.0001 && std::abs(s.amplitude[1][64]-.4)<.0001,"Hann coherent-gain input/output calibration including opposite-polarity stereo");
            check(s.hasEnvelope && std::abs(s.envelopeDb[0][64]-20*std::log10(.2))<.001,"calibrated per-hop envelope");
            check(bool(s.header.flags&analysisGap)==(i==0),"only first spectrum marks reset gap");
            check(s.header.sourceNanoseconds==1000000000ull+std::uint64_t(double(s.header.endSample)*1e9/fs),"actual source production time retained");
        }
    }
    // DC and Nyquist use factor 1; the doubled single-sided interior scale does not apply.
    auto transform=std::make_unique<ExtendedSpectrumTransform>();ExtendedSpectrumSnapshot s;
    for(unsigned at=0;at<4096;at+=1024){auto f=frame(at,48000,4096);for(unsigned i=0;i<1024;++i){f.samples[0][i]=f.samples[1][i]=.25f;f.samples[2][i]=f.samples[3][i]=i%2?-.125f:.125f;}transform->accept(f,1,1,4096,36,[&](const auto& v){s=v;});}
    check(std::abs(s.amplitude[0][0]-.25)<.00001 && std::abs(s.amplitude[1][2048]-.125)<.00001,"DC/Nyquist amplitude convention");
    for(double release:{18.,36.,72.}){
        auto t=std::make_unique<ExtendedSpectrumTransform>();std::vector<ExtendedSpectrumSnapshot> all;
        for(unsigned at=0;at<16384;at+=1024)t->accept(frame(at,48000,4096,at<4096?.2:0),1,1,4096,release,[&](const auto& v){all.push_back(v);});
        double expected=-180;std::uint64_t time=0;
        for(const auto& v:all){const auto a=v.amplitude[0][64];const double target=a>0?std::max(-180.,20*std::log10(double(a))):-180.;
            expected=time?std::max(target,expected-release*double(v.header.sourceNanoseconds-time)*1e-9):target;time=v.header.sourceNanoseconds;
            check(std::abs(v.envelopeDb[0][64]-expected)<.0001,"every FFT hop contributes to source-time release oracle");}
        check(all.back().amplitude[0][64]==0 && all.back().envelopeDb[0][64]>-80,"UI skipping intermediate spectra still receives decaying real peak");
        const auto old=all.size();
        for(unsigned at=0;at<4096;at+=1024)t->accept(frame(at,48000,4096,0),2,1,4096,release,[&](const auto& v){all.push_back(v);});
        check(all.size()==old+1 && all.back().envelopeDb[0][64]==-180,"presentation reset discards old peak envelope");
    }
}
static void feed(AnalysisReader& r,std::uint64_t& position,unsigned count,unsigned n=4096){
    for(unsigned i=0;i<count;++i){AnalysisMessage m;m.kind=AnalysisKind::samples;m.sampleFrame=frame(position,48000,n);m.sampleFrame.header.sourceNanoseconds=analysisNow();position+=1024;r.accept(m,position,1);}
}
static bool waitSpectrum(AnalysisReader& r,ExtendedSpectrumSnapshot& s,unsigned n,std::uint64_t end){
    for(unsigned i=0;i<180;++i){if(r.readExtendedSpectrum(1,s)==AnalysisAvailability::fresh && s.fftSize==n && s.header.endSample>=end)return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;
}
static void readerLifecycle(){
    AnalysisReader legacy;check(!legacy.configureExtendedSpectrum(4096),"other modules cannot configure extended analyzer");ExtendedSpectrumSnapshot s;
    check(legacy.readExtendedSpectrum(1,s)==AnalysisAvailability::unavailable && !s.binCount,"unsupported read is empty");
    AnalysisReader r(true);r.start();std::uint64_t position=0;feed(r,position,4);check(waitSpectrum(r,s,4096,position),"existing worker publishes first extended FFT");
    const auto old=s;SpectrumSnapshot small;check(r.readSpectrum(1,small)==AnalysisAvailability::unavailable,"opt-in uses one FFT path, not duplicate legacy processing");
    check(!r.configureExtendedSpectrum(2048) && !r.configureExtendedSpectrum(8192,35),"unapproved configurations rejected");
    check(r.configureExtendedSpectrum(8192) && r.readExtendedSpectrum(1,s)==AnalysisAvailability::unavailable,"configuration change immediately invalidates old publication");
    feed(r,position,4,8192);std::this_thread::sleep_for(std::chrono::milliseconds(20));check(r.readExtendedSpectrum(1,s)!=AnalysisAvailability::fresh,"half new window never exposes old FFT");
    feed(r,position,4,8192);check(waitSpectrum(r,s,8192,position) && s.configurationGeneration!=old.configurationGeneration,"full configured window recovers with new identity");
    auto beforePause=s;r.setPresentationPaused(true);feed(r,position,2,8192);check(r.readExtendedSpectrum(1,s)==AnalysisAvailability::unavailable,"paused samples are dropped");
    AnalysisMessage inFlight;inFlight.kind=AnalysisKind::samples;inFlight.sampleFrame=frame(position,48000,8192);inFlight.sampleFrame.header.sourceNanoseconds=analysisNow();
    r.setPresentationPaused(false);position+=1024;r.accept(inFlight,position,1);check(r.readExtendedSpectrum(1,s)==AnalysisAvailability::unavailable,"in-flight pre-resume frame cannot revive old spectrum");
    feed(r,position,8,8192);check(waitSpectrum(r,s,8192,position) && s.presentationGeneration!=beforePause.presentationGeneration,"resume waits for a complete new-generation window");
    position+=1024;feed(r,position,1,8192);check(r.readExtendedSpectrum(1,s)==AnalysisAvailability::unavailable,"sample gap invalidates old publication immediately");
    feed(r,position,7,8192);check(waitSpectrum(r,s,8192,position) && (s.header.flags&analysisGap),"new full post-gap window recovers");
    r.stop();std::this_thread::sleep_for(std::chrono::milliseconds(510));check(r.readExtendedSpectrum(1,s)==AnalysisAvailability::stale && !s.binCount,"stopped production becomes stale and clears output");
    AnalysisReader saturated(true);position=0;feed(saturated,position,32);saturated.start();std::this_thread::sleep_for(std::chrono::milliseconds(40));
    check(saturated.readExtendedSpectrum(1,s)!=AnalysisAvailability::fresh,"overflow cannot republish queued old-generation FFTs");
    feed(saturated,position,4);check(waitSpectrum(saturated,s,4096,position),"overflow recovers using only a new continuous full window");saturated.stop();
}
int main(){numerical();readerLifecycle();std::cout<<"PASS "<<checks<<" extended spectrum calibration/overlap/envelope/generation/freshness/overflow checks; no GUI\n";}
