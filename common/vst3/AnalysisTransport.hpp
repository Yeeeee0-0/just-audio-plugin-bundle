#pragma once
#include "../runtime/Analysis.hpp"
#include "../dsp/Realtime.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "base/source/timer.h"
#include "pluginterfaces/vst/ivstattributes.h"
namespace just {
inline constexpr const char* analysisMessageID="JUST.Analysis.v2";
inline constexpr const char* analysisSubscriptionID="JUST.Analysis.Subscribe.v2";
struct AnalysisSource {std::uint64_t session=0,epoch=0,sample=0;};
class AnalysisTransport final:public Steinberg::ITimerCallback {
    Steinberg::Vst::AudioEffect& owner;
    SpscQueue<AnalysisMessage,64> queue;
    AtomicSnapshot<AnalysisSource> source;
    Steinberg::IPtr<Steinberg::Timer> timer;
public:
    explicit AnalysisTransport(Steinberg::Vst::AudioEffect& p):owner(p){}
    ~AnalysisTransport() override{stop();}
    void start(){stop();timer=Steinberg::owned(Steinberg::Timer::create(this,10));}
    void stop(){if(timer){timer->stop();timer=nullptr;}} // stopped host lifecycle
    bool send(const AnalysisMessage& message) noexcept{return queue.push(message);}
    void publishSource(AnalysisSource s) noexcept{source.publish(s);}
    void onTimer(Steinberg::Timer*) override {
        if(!owner.getPeer())return;AnalysisSource head;if(!source.read(head))return;
        for(unsigned i=0;i<16;++i){AnalysisMessage data;if(!queue.pop(data))break;
            auto& h=data.kind==AnalysisKind::envelope?data.window.header:data.sampleFrame.header;
            if(h.session!=head.session || h.epoch!=head.epoch || h.endSample>head.sample || head.sample-h.endSample>h.sampleRate*.5)continue;
            const auto now=analysisNow();if(!h.sourceNanoseconds || now<h.sourceNanoseconds || now-h.sourceNanoseconds>500000000ull)continue;
            auto m=Steinberg::owned(owner.allocateMessage());if(!m)continue;m->setMessageID(analysisMessageID);auto* a=m->getAttributes();if(!a)continue;
            a->setInt("SourceSample",head.sample);a->setInt("SourceEpoch",head.epoch);a->setBinary("Data",&data,sizeof(data));owner.sendMessage(m);
        }
    }
};
}
