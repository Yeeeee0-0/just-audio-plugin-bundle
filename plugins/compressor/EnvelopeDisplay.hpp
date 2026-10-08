#pragma once
#include "common/vst3/Module.hpp"
namespace just::compressor {
// UI-only history of real processor windows. Never estimates audio or accesses
// an engine pointer. The common reader owns session and freshness validation.
struct EnvelopeDisplay {
    std::array<AnalysisWindow,600> history{};
    std::size_t count=0,write=0;
    AnalysisCursor cursor{};
    AnalysisAvailability availability=AnalysisAvailability::unavailable;
    const AnalysisWindow& at(std::size_t i) const noexcept {return history[(write+history.size()-count+i)%history.size()];}
    const AnalysisWindow& latest() const noexcept {return at(count-1);}
    void refresh(const EditorServices& services) {
        availability=AnalysisAvailability::unavailable;
        for(unsigned attempt=0;attempt<16 && services.readAnalysis;++attempt) {
            AnalysisBatch batch;availability=services.readAnalysis(services.owner,cursor,batch);
            if(availability!=AnalysisAvailability::fresh || !batch.count)break;
            for(unsigned i=0;i<std::min<std::size_t>(batch.count,batch.windows.size());++i) {
                const auto& item=batch.windows[i];
                if(count && (latest().header.session!=item.header.session || latest().header.epoch!=item.header.epoch))count=write=0;
                history[write]=item;write=(write+1)%history.size();count=std::min(count+1,history.size());
            }
        }
    }
};
inline double levelY(double db,double top,double height) noexcept {return top+height*(1-std::clamp((db+60)/60.,0.,1.));}
}
