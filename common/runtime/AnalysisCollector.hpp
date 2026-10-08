#pragma once
#include "Analysis.hpp"
#include <vector>
namespace just {
// Owned by Processor. prepare() is stopped/non-RT. All subsequent calls are bounded audio work.
class AnalysisCollector final:public AnalysisTap {
    std::vector<std::array<double,2>> inputCopy,delay;
    std::vector<EffectAnalysisSample> effect;
    std::vector<std::uint8_t> bypass;
    AnalysisWindow window{};SampleFrame raw{};
    std::array<double,6> squares{};double products[2]{};
    std::uint32_t count=0,target=480,blockSize=0,delayPos=0,latency=0,effectCount[7]{};
    std::uint64_t clock=0,epoch=0,sequence=0,rawSequence=0,delayFilled=0,dropped=0;
    double rate=0;bool configured=false,collecting=false;
    bool (*send)(void*,const AnalysisMessage&)=nullptr;void* sender=nullptr;Uid plugin{};
    void clearWindow(){window={};squares={};products[0]=products[1]=0;count=0;for(auto& n:effectCount)n=0;}
public:
    bool prepare(double fs,std::uint32_t maximum,std::uint32_t pdc) {
        configured=false;if(!std::isfinite(fs) || fs<=0 || fs>768000 || maximum>1048576 || pdc>131072)return false;
        inputCopy.resize(maximum);effect.resize(maximum);bypass.resize(maximum);delay.resize(pdc+1);rate=fs;target=std::max(1u,std::uint32_t(std::round(fs*.01)));latency=pdc;
        configured=true;restart();return true;
    }
    void setSender(Uid id,void* p,bool (*callback)(void*,const AnalysisMessage&)){plugin=id;sender=p;send=callback;}
    void restart() noexcept {++epoch;clock=0;delayFilled=0;delayPos=0;clearWindow();raw={};collecting=false;}
    void setEnabled(bool active) noexcept{if(active!=collecting){restart();collecting=active;}}
    std::uint64_t sourceSample() const noexcept{return clock;}
    std::uint64_t currentEpoch() const noexcept{return epoch;}
    bool ready() const noexcept{return configured && collecting;}
    void captureBypass(std::uint32_t offset,std::uint32_t length,bool value) noexcept{for(unsigned i=offset;i<offset+length && i<blockSize;++i)bypass[i]=value;}
    template<class S> void capture(const S* const* in,std::uint32_t channels,std::uint64_t silence,std::uint32_t n) noexcept {
        blockSize=n;for(unsigned i=0;i<n;++i){
            effect[i]={};std::array<double,2> sample{};
            for(unsigned c=0;c<channels;++c)sample[c]=in && in[c] && !(silence&(1ull<<c))?double(in[c][i]):0;
            delay[delayPos]=sample;auto at=(delayPos+1)%delay.size();
            inputCopy[i]=delayFilled>=latency?delay[at]:std::array<double,2>{};
            ++delayFilled;delayPos=at;
        }
    }
    void pushSample(std::uint32_t offset,const EffectAnalysisSample& s) noexcept override {
        if(!ready() || offset>=blockSize || s.validFields&~127u)return;
        effect[offset]=s; // one fixed slot; no queue, ownership transfer or allocation
    }
    template<class S> void finish(S* const* out,std::uint32_t inputChannels,std::uint32_t outputChannels,std::uint32_t n,
                                 std::uint64_t session,std::uint32_t flags) noexcept {
        const auto captured=analysisNow();
        for(unsigned i=0;i<n;++i,++clock){
            if(clock<latency)continue; // no corresponding input yet; start the first paired window at PDC
            const auto sampleFlags=(flags&~std::uint32_t(analysisBypassed))|(bypass[i]?analysisBypassed:0);
            if(!count){window.header={session,epoch,++sequence,clock,clock, dropped,rate,inputChannels,outputChannels,latency,sampleFlags};
                if(clock>=latency)window.header.flags|=analysisInputAligned;
            }
            if(!raw.count){raw.header={session,epoch,++rawSequence,clock,clock,dropped,rate,inputChannels,outputChannels,latency,sampleFlags};if(clock>=latency)raw.header.flags|=analysisInputAligned;}
            auto e=effect[i];double values[6]={inputCopy[i][0],inputCopy[i][1],out && out[0]?double(out[0][i]):0,outputChannels==2 && out && out[1]?double(out[1][i]):0,e.wet[0],e.wet[1]};
            for(unsigned c=0;c<6;++c){
                if(!std::isfinite(values[c])){values[c]=0;window.header.flags|=analysisInvalid;raw.header.flags|=analysisInvalid;}
                auto& env=window.channels[c];env.peak=std::max(env.peak,std::abs(values[c]));squares[c]+=values[c]*values[c];
                if(!count)env.minimum=env.maximum=values[c];else{env.minimum=std::min(env.minimum,values[c]);env.maximum=std::max(env.maximum,values[c]);}
                if(c<4)raw.samples[c][raw.count]=float(std::clamp(values[c],-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));
            }
            products[0]+=values[0]*values[1];products[1]+=values[2]*values[3];
            for(unsigned field=0;field<7;++field)if(e.validFields&(1u<<field))++effectCount[field];
            if(e.validFields&analysisReduction){if(std::isfinite(e.reductionDb) && e.reductionDb>=0)window.reductionDb=std::max(window.reductionDb,e.reductionDb);else window.header.flags|=analysisInvalid;}
            if(e.validFields&analysisModulation)for(unsigned c=0;c<2;++c){
                if(!effectCount[2] || effectCount[2]==1)window.modulationMin[c]=window.modulationMax[c]=e.modulation[c];
                else {window.modulationMin[c]=std::min(window.modulationMin[c],e.modulation[c]);window.modulationMax[c]=std::max(window.modulationMax[c],e.modulation[c]);}
                window.phase[c]=e.phase[c];window.effectiveHz=e.effectiveHz;
            }
            if(e.validFields&analysisDelay){window.delayMs[0]=e.delayMs[0];window.delayMs[1]=e.delayMs[1];}
            window.gateState=e.gateState;window.effectFlags|=e.flags;
            ++count;++raw.count;window.header.endSample=raw.header.endSample=clock+1;
            const auto elapsed=flags&analysisOffline?0ull:std::uint64_t(double(n-i-1)*1e9/rate);
            window.header.sourceNanoseconds=raw.header.sourceNanoseconds=captured>elapsed?captured-elapsed:1;
            if(count==target){
                for(unsigned c=0;c<6;++c)window.channels[c].rms=std::sqrt(squares[c]/count);
                for(unsigned field=0;field<7;++field)if(effectCount[field]==count)window.effectFields|=1u<<field;
                for(unsigned c=0;c<2;++c)if((c?outputChannels:inputChannels)==2 && squares[2*c]>1e-20 && squares[2*c+1]>1e-20){window.correlationValid|=1u<<c;window.correlation[c]=std::clamp(products[c]/std::sqrt(squares[2*c]*squares[2*c+1]),-1.,1.);}
                AnalysisMessage m;m.plugin=plugin;m.window=window;
                if(!send || !send(sender,m))++dropped;clearWindow();
            }
            if(raw.count==analysisSampleCapacity){AnalysisMessage m;m.plugin=plugin;m.kind=AnalysisKind::samples;m.sampleFrame=raw;if(!send || !send(sender,m))++dropped;raw={};}
        }
    }
};
}
