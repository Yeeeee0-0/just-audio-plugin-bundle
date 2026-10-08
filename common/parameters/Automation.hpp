#pragma once
#include "../state/State.hpp"
namespace just {
// Host-owned queues must be ordered by sample offset. Invalid points are ignored.
// Continuous curves interpolate from the preceding block value at offset -1.
// Discrete parameters switch at the point offset without intermediate enum values.
class EventSource {
public:
    virtual ~EventSource()=default;
    virtual std::int32_t queueCount() const noexcept=0;
    virtual ParamID parameterID(std::int32_t queue) const noexcept=0;
    virtual std::int32_t pointCount(std::int32_t queue) const noexcept=0;
    virtual bool point(std::int32_t queue,std::int32_t index,ParamEvent&) const noexcept=0;
};
class Automation {
    struct Cursor {
        std::int32_t queue=-1,index=0,count=0,previousOffset=-1;
        double previousValue=0;ParamEvent next{};bool hasNext=false;
    };
    std::array<Cursor,maxParameters> cursors{};
    ParameterRegistry registry;const EventSource* source=nullptr;std::int32_t samples=0;
    void advance(Cursor& c) noexcept {
        c.hasNext=false;
        while(c.index<c.count) {
            ParamEvent e{};
            if(source->point(c.queue,c.index++,e) && e.sampleOffset>=0 &&
               e.sampleOffset<=std::max(0,samples-1) && e.sampleOffset>c.previousOffset &&
               std::isfinite(e.normalizedValue) && e.normalizedValue>=0 && e.normalizedValue<=1) {
                c.next=e;c.hasNext=true;return;
            }
        }
    }
public:
    Automation(ParameterRegistry r):registry(r){}
    void begin(const EventSource* events,const SoundState& current,std::int32_t numSamples) noexcept {
        source=events;samples=numSamples;
        for(std::size_t i=0;i<registry.count;++i){cursors[i]={};cursors[i].previousValue=current.targets[i];}
        if(!source)return;
        for(std::int32_t q=0;q<source->queueCount();++q) {
            auto i=registry.index(source->parameterID(q));
            if(i==registry.count || cursors[i].queue>=0)continue;
            auto& c=cursors[i];c.queue=q;c.count=std::max(0,source->pointCount(q));advance(c);
        }
    }
    // All parameters are evaluated before a single target snapshot is applied.
    void evaluate(std::int32_t sampleOffset,SoundState& current) noexcept {
        for(std::size_t i=0;i<registry.count;++i) {
            auto& c=cursors[i];if(c.queue<0)continue;
            while(c.hasNext && c.next.sampleOffset<=sampleOffset) {
                c.previousOffset=c.next.sampleOffset;c.previousValue=c.next.normalizedValue;advance(c);
            }
            double v=c.previousValue;
            if(c.hasNext && registry.specs[i].transition==Transition::continuous && !registry.specs[i].stepCount) {
                const double fraction=double(sampleOffset-c.previousOffset)/double(c.next.sampleOffset-c.previousOffset);
                v=c.previousValue+(c.next.normalizedValue-c.previousValue)*fraction;
            }
            current.targets[i]=std::clamp(v,0.0,1.0);
        }
    }
};
}
