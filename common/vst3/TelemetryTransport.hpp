#pragma once
#include "../dsp/Realtime.hpp"
#include "../runtime/Telemetry.hpp"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "base/source/timer.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include <algorithm>
namespace just {
// All connect/activate/deactivate/disconnect operations are host main-thread,
// stopped-process lifecycle calls. send() is the only audio-thread method.
class TelemetryTransport final:public Steinberg::ITimerCallback {
    Steinberg::Vst::AudioEffect& owner;
    Steinberg::Vst::DataExchangeHandler direct;
    SpscQueue<RuntimeTelemetrySnapshot,8> fallback;
    Steinberg::IPtr<Steinberg::Timer> timer;
    std::uint32_t context=0;
    bool useDirect=false,opened=false;
    void message(const char* id,const RuntimeTelemetrySnapshot* frame=nullptr) {
        auto m=Steinberg::owned(owner.allocateMessage());if(!m)return;
        m->setMessageID(id);auto* a=m->getAttributes();if(!a)return;
        a->setInt("UserContextID",context);
        if(frame)a->setBinary("Data",frame,sizeof(*frame));
        else a->setInt("BlockSize",sizeof(RuntimeTelemetrySnapshot));
        owner.sendMessage(m);
    }
public:
    explicit TelemetryTransport(Steinberg::Vst::AudioEffect& p):owner(p),direct(&p,[this](auto& config,const auto&){
        config.blockSize=sizeof(RuntimeTelemetrySnapshot);config.numBlocks=4;config.alignment=32;config.userContextID=context;return context!=0;
    }){}
    ~TelemetryTransport() override {deactivate();}
    void connect(Steinberg::Vst::IConnectionPoint* peer,Steinberg::FUnknown* host) {
        useDirect=bool(Steinberg::FUnknownPtr<Steinberg::Vst::IDataExchangeHandler>(host));
        if(useDirect)direct.onConnect(peer,host);
    }
    void activate(const Steinberg::Vst::ProcessSetup& setup) {
        deactivate();if(!owner.getPeer())return;
        static std::atomic<std::uint32_t> nextContext{0};context=++nextContext;if(!context)return;
        if(useDirect){direct.onActivate(setup);opened=true;return;}
        // Deliberately bounded fallback instead of SDK's while(pop) timer drain.
        // All storage already exists; at most four pops and one message per tick.
        timer=Steinberg::owned(Steinberg::Timer::create(this,33));if(!timer){context=0;return;}
        opened=true;message("DataExchangeQueueOpened");
    }
    void deactivate() {
        if(timer){timer->stop();timer=nullptr;}
        if(opened){if(useDirect)direct.onDeactivate();else message("DataExchangeQueueClosed");}
        opened=false;context=0;RuntimeTelemetrySnapshot ignored;
        for(unsigned i=0;i<8 && fallback.pop(ignored);++i){} // process is stopped
    }
    void disconnect(Steinberg::Vst::IConnectionPoint* peer) {
        deactivate();if(useDirect)direct.onDisconnect(peer);useDirect=false;
    }
    std::uint32_t queueContext() const noexcept{return context;}
    bool send(const RuntimeTelemetrySnapshot& frame) noexcept {
        if(!opened)return false;
        if(!useDirect)return fallback.push(frame); // saturation drops; no retry/spin
        auto block=direct.getCurrentOrNewBlock();
        if(block.blockID==Steinberg::Vst::InvalidDataExchangeBlockID)return false;
        if(!block.data || block.size!=sizeof(frame)){direct.discardCurrentBlock();return false;}
        std::memcpy(block.data,&frame,sizeof(frame));return direct.sendCurrentBlock();
    }
    void onTimer(Steinberg::Timer*) override {
        if(!opened || useDirect || !owner.getPeer())return;
        RuntimeTelemetrySnapshot latest,staged;bool have=false;
        for(unsigned i=0;i<4;++i){if(!fallback.pop(staged))break;latest=staged;have=true;}
        if(have)message("DataExchange",&latest);
    }
};
}
