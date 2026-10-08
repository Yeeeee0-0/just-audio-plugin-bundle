#pragma once
#include "common/ui/AnalysisView.hpp"
namespace just::limiter {
struct HistoryModel {
 std::array<AnalysisWindow,600> windows{};
 std::size_t count=0,write=0;
 AnalysisCursor cursor{};
 AnalysisAvailability availability=AnalysisAvailability::unavailable;
 just::MeterHold outputHold,grHold;
 bool outputHeld=false;
 void refresh(const EditorServices& services) {
  availability=AnalysisAvailability::unavailable;
  for(unsigned attempt=0;attempt<128 && services.readAnalysis;++attempt){AnalysisBatch batch;availability=services.readAnalysis(services.owner,cursor,batch);if(availability!=AnalysisAvailability::fresh || !batch.count)break;
   for(unsigned i=0;i<batch.count;++i){auto item=batch.windows[i];const auto& h=item.header;
    if(count){const auto& prev=latest().header;if(h.session!=prev.session || h.epoch!=prev.epoch){count=write=0;outputHold={};grHold={};outputHeld=false;}}
    windows[write]=item;write=(write+1)%windows.size();count=std::min(count+1,windows.size());
    if(!(h.flags&analysisInvalid) && h.flags&analysisInputAligned){bool newer=h.session!=outputHold.session || h.epoch!=outputHold.epoch || h.sequence>outputHold.sequence;outputHold.observe(item);grHold.observe(item);outputHeld|=newer;}
   }
  }
 }
 const AnalysisWindow& latest() const {return windows[(write+windows.size()-1)%windows.size()];}
 const AnalysisWindow& at(std::size_t i) const {return windows[(write+windows.size()-count+i)%windows.size()];}
 bool current() const {return count && availability==AnalysisAvailability::fresh && !(latest().header.flags&analysisInvalid);}
 void resetOutput(){outputHold.reset();outputHeld=false;}
 void resetGR(){grHold.reset();}
};
}
