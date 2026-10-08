#include "common/runtime/AnalysisCollector.hpp"
#include "common/runtime/AnalysisReader.hpp"
#include <iostream>
#include <vector>
#include <cstdlib>
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
struct Capture {
    std::vector<just::AnalysisMessage> messages;std::size_t count=0;bool drop=false;
    Capture():messages(512){}
    static bool send(void* owner,const just::AnalysisMessage& m){auto& c=*static_cast<Capture*>(owner);if(c.drop)return false;if(c.count>=c.messages.size())return false;c.messages[c.count++]=m;return true;}
};
static void exercise(double fs,unsigned block,unsigned pdc){
    just::AnalysisCollector collector;Capture capture;
    check(collector.prepare(fs,block,pdc),"prepare bounded collector");collector.setSender({},&capture,Capture::send);collector.setEnabled(true);
    std::vector<double> l(block),r(block),original(block);std::vector<double> delay(pdc+1);unsigned delayAt=0;
    const double* inputs[]={l.data(),r.data()};double* outputs[]={l.data(),r.data()};
    std::uint64_t position=0;
    for(unsigned pass=0;pass<24;++pass){
        for(unsigned i=0;i<block;++i){double x=(position+i==5)?1.:.2*std::sin((position+i)*.13);l[i]=original[i]=x;r[i]=-x;}
        collector.capture(inputs,2,0,block);
        for(unsigned i=0;i<block;++i){delay[delayAt]=original[i];delayAt=(delayAt+1)%delay.size();double x=position+i>=pdc?delay[delayAt]:0;l[i]=x;r[i]=-x;
            just::EffectAnalysisSample e;e.validFields=just::analysisWet|just::analysisReduction;e.wet[0]=.3*x;e.wet[1]=-.3*x;e.reductionDb=6;collector.pushSample(i,e);}
        collector.finish(outputs,2,2,block,1,just::analysisPlaying|just::analysisTransportKnown);position+=block;
    }
    bool saw=false;for(std::size_t i=0;i<capture.count;++i){const auto& m=capture.messages[i];check(m.valid(),"collector emits finite valid frame");
        if(m.kind==just::AnalysisKind::envelope){auto& w=m.window;check(w.header.flags&just::analysisInputAligned,"only paired PDC windows");check(w.channels[0].peak==w.channels[2].peak && w.channels[0].rms==w.channels[2].rms,"PDC aligns exact envelopes");saw=true;}
        else for(unsigned j=0;j<m.sampleFrame.count;++j)check(m.sampleFrame.samples[0][j]==m.sampleFrame.samples[2][j],"PDC aligns raw sample, including inplace host buffers");
    }check(saw,"windows produced");
    // Saturation records missing sequences without blocking or fabricating continuity.
    capture.drop=true;collector.capture(inputs,2,0,block);collector.finish(outputs,2,2,block,1,0);capture.drop=false;
    just::AnalysisReader reader;for(std::size_t i=0;i<capture.count;++i){const auto& m=capture.messages[i];const auto& h=m.kind==just::AnalysisKind::envelope?m.window.header:m.sampleFrame.header;reader.accept(m,position,h.epoch);}
    just::AnalysisCursor cursor;just::AnalysisBatch batch;auto available=reader.read(1,cursor,batch);check(available==just::AnalysisAvailability::fresh && batch.count,"fresh history batch");
    just::AnalysisCursor other;just::AnalysisBatch second;reader.read(1,other,second);check(second.count==batch.count && other.sequence==cursor.sequence,"independent readers do not consume each other");
}
static void freshness(){
    using namespace just;AnalysisReader reader;const auto now=analysisNow();
    auto envelope=[&](std::uint64_t start,std::uint64_t end,std::uint64_t seq,std::uint64_t time){AnalysisMessage m;m.window.header={1,1,seq,start,end,0,48000,2,2,0,analysisInputAligned};m.window.header.sourceNanoseconds=time;return m;};
    auto first=envelope(0,480,1,now-600000000ull);reader.accept(first,480,1);AnalysisCursor cursor;AnalysisBatch batch;
    check(reader.read(1,cursor,batch)!=AnalysisAvailability::fresh,"old production time is never revived on first receive");
    auto buffered=envelope(480,960,2,now-600000000ull);reader.accept(buffered,960,1);
    check(reader.read(1,cursor,batch)!=AnalysisAvailability::fresh,"stopped audio plus delayed final queue frame stays stale");
    AnalysisMessage raw;raw.kind=AnalysisKind::samples;raw.sampleFrame.count=1024;raw.sampleFrame.header={1,1,1,0,1024,0,48000,2,2,0,analysisInputAligned};raw.sampleFrame.header.sourceNanoseconds=analysisNow();
    reader.accept(raw,1024,1);SampleFrame samples;check(reader.readSamples(1,samples)==AnalysisAvailability::fresh,"new raw stream is fresh");
    auto freshEnvelope=envelope(47520,48000,100,analysisNow());reader.accept(freshEnvelope,48000,1);
    check(reader.read(1,cursor,batch)==AnalysisAvailability::fresh,"envelope may continue independently");
    check(reader.readSamples(1,samples)==AnalysisAvailability::stale && samples.count==0 && !samples.header.session,"0.978667s old samples cannot borrow fresh envelope; output cleared");
    SpectrumSnapshot spectrum;check(reader.readSpectrum(1,spectrum)!=AnalysisAvailability::fresh,"FFT cannot borrow another stream's freshness");
}
static void presentationPause(){
    using namespace just;AnalysisReader reader;reader.start();
    auto envelope=[](unsigned seq){AnalysisMessage m;m.window.header={1,1,seq,(seq-1)*480ull,seq*480ull,0,48000,2,2,0,analysisInputAligned};m.window.header.sourceNanoseconds=analysisNow();return m;};
    auto first=envelope(1);reader.accept(first,480,1);AnalysisCursor cursor;AnalysisBatch batch;
    check(reader.read(1,cursor,batch)==AnalysisAvailability::fresh && batch.count==1,"pre-pause actual envelope readable");
    reader.setPresentationPaused(true);
    for(unsigned i=2;i<100;++i){auto queued=envelope(i);reader.accept(queued,i*480,1);}
    check(reader.read(1,cursor,batch)==AnalysisAvailability::unavailable && batch.count==0,"paused input does not accumulate display history");
    AnalysisMessage raw;raw.kind=AnalysisKind::samples;raw.sampleFrame.count=1024;raw.sampleFrame.header={1,1,1,0,1024,0,48000,2,2,0,analysisInputAligned};raw.sampleFrame.header.sourceNanoseconds=analysisNow();reader.accept(raw,1024,1);
    SampleFrame samples;check(reader.readSamples(1,samples)==AnalysisAvailability::unavailable,"paused raw data is dropped");
    const auto queuedBeforeResume=envelope(100);reader.setPresentationPaused(false);reader.accept(queuedBeforeResume,48000,1);
    check(reader.read(1,cursor,batch)==AnalysisAvailability::unavailable,"in-flight pre-resume envelope cannot be replayed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto resumed=envelope(101);reader.accept(resumed,48480,1);
    check(reader.read(1,cursor,batch)==AnalysisAvailability::fresh && batch.count==1 && batch.windows[0].header.sequence==101 && (batch.windows[0].header.flags&analysisGap),"resume starts with only new real data and explicit gap");
    // Publish one full real FFT, pause, then supply an incomplete new window.
    raw.sampleFrame.header.sourceNanoseconds=analysisNow();reader.accept(raw,1024,1);
    raw.sampleFrame.header.sequence=2;raw.sampleFrame.header.startSample=1024;raw.sampleFrame.header.endSample=2048;raw.sampleFrame.header.sourceNanoseconds=analysisNow();reader.accept(raw,2048,1);
    SpectrumSnapshot spectrum;bool ready=false;for(unsigned n=0;n<100 && !ready;++n){std::this_thread::sleep_for(std::chrono::milliseconds(2));ready=reader.readSpectrum(1,spectrum)==AnalysisAvailability::fresh;}
    check(ready,"real worker produces spectrum before second pause");
    reader.setPresentationPaused(true);reader.setPresentationPaused(false);
    // Same-tick production timestamps are deliberately rejected by the reader.
    // A real next audio frame occurs after resume; give the fixture the same ordering.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    raw.sampleFrame.header.sequence=3;raw.sampleFrame.header.startSample=2048;raw.sampleFrame.header.endSample=2304;raw.sampleFrame.count=256;raw.sampleFrame.header.sourceNanoseconds=analysisNow();reader.accept(raw,2304,1);
    check(reader.readSamples(1,samples)==AnalysisAvailability::fresh && samples.header.sequence==3,"new raw samples resume immediately");
    check(reader.readSpectrum(1,spectrum)!=AnalysisAvailability::fresh,"old FFT cannot borrow new samples freshness after pause");reader.stop();
}
int main(){presentationPause();freshness();for(double fs:{44100.,48000.,96000.})for(unsigned pdc:{0u,15u,288u})for(unsigned block:{511u,4096u})exercise(fs,block,pdc);std::cout<<"PASS "<<checks<<" analysis timing/PDC/inplace/core checks\n";}
