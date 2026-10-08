#pragma once
#include "common/vst3/Module.hpp"

namespace just::delay {
// One cursor per editor. The analysis reader already owns transport and the
// audio thread never waits for this UI-only ring.
class FeedbackHistory {
    std::array<AnalysisWindow,600> windows{};
    std::size_t count=0,write=0;
    AnalysisCursor cursor{};
    AnalysisAvailability state=AnalysisAvailability::unavailable;
public:
    void poll(const EditorServices& services) noexcept {
        if(!services.readAnalysis){state=AnalysisAvailability::unavailable;return;}
        for(unsigned attempt=0;attempt<16;++attempt) {
            AnalysisBatch batch{};
            state=services.readAnalysis(services.owner,cursor,batch);
            if(state!=AnalysisAvailability::fresh || !batch.count)return;
            for(unsigned i=0;i<batch.count;++i) {
                const auto& item=batch.windows[i];
                if(!validAnalysisHeader(item.header))continue;
                if(count) {
                    const auto& previous=windows[(write+windows.size()-1)%windows.size()].header;
                    if(previous.session!=item.header.session || previous.epoch!=item.header.epoch)count=write=0;
                }
                windows[write]=item;write=(write+1)%windows.size();count=std::min(count+1,windows.size());
            }
        }
    }
    AnalysisAvailability availability() const noexcept{return state;}
    const AnalysisWindow* latest() const noexcept{return count?&windows[(write+windows.size()-1)%windows.size()]:nullptr;}
    template<class Visit> void each(Visit&& visit) const {
        for(std::size_t i=0;i<count;++i)visit(windows[(write+windows.size()-count+i)%windows.size()]);
    }
};
}
