#pragma once
#include "UIModel.hpp"
#include <array>

namespace just::gate {
inline constexpr const char* envelopeViewIdentifier="JUST.gate.envelope";
inline constexpr const char* iconResourceKey="just.gate.icon";
inline constexpr const char* backgroundResourceKey="just.ambient.background";
// UI-owned bounded history. Every point is an unchanged processor measurement.
// Raw input/output peaks are not the filtered/RMS/External SC detector signal.
struct EnvelopeHistory {
    std::array<AnalysisWindow,512> points{};
    std::size_t write=0,count=0;
    AnalysisCursor cursor{};
    AnalysisAvailability availability=AnalysisAvailability::unavailable;
    void accept(const AnalysisWindow& window) noexcept {
        if(!validAnalysisHeader(window.header))return;
        if(count){const auto& last=latest().header;
            if(last.session!=window.header.session || last.epoch!=window.header.epoch){count=write=0;}
            else if(window.header.sequence<=last.sequence)return;
        }
        points[write]=window;write=(write+1)%points.size();count=std::min(count+1,points.size());
    }
    void refresh(const EditorServices& services) {
        if(!services.readAnalysis){availability=AnalysisAvailability::unavailable;return;}
        AnalysisBatch batch;
        // Drain at most the common reader's bounded history; no timer or audio work.
        for(unsigned i=0;i<128;++i){
            availability=services.readAnalysis(services.owner,cursor,batch);
            if(availability!=AnalysisAvailability::fresh || !batch.count)break;
            for(unsigned j=0;j<batch.count;++j)accept(batch.windows[j]);
        }
    }
    const AnalysisWindow& at(std::size_t index) const noexcept{return points[(write+points.size()-count+index)%points.size()];}
    const AnalysisWindow& latest() const noexcept{return at(count-1);}
    static bool joins(const AnalysisWindow& a,const AnalysisWindow& b) noexcept {
        return !(b.header.flags&analysisGap) && a.header.session==b.header.session && a.header.epoch==b.header.epoch &&
            a.header.sequence+1==b.header.sequence && a.header.endSample==b.header.startSample;
    }
    static double inputPeak(const AnalysisWindow& w) noexcept{return std::max(w.channels[0].peak,w.channels[1].peak);}
    static double outputPeak(const AnalysisWindow& w) noexcept{return std::max(w.channels[2].peak,w.channels[3].peak);}
    static double peakDb(double peak) noexcept{return 20*std::log10(std::max(peak,1e-15));}
};
// Fits all four whole shared control cells and a measured chart at the legacy
// 310-point usable body minimum, without assuming a common-shell offset.
struct PreviewLayout {
    int margin=12,chartY=12,chartHeight=128,controlY=150,controlWidth=112,cell=170;
    static PreviewLayout fit(int width,int height) noexcept {
        PreviewLayout r;r.margin=std::max(12,int(width*36./1120.));r.cell=std::min(177,width/4);
        r.controlWidth=112;
        r.chartHeight=std::max(96,height-186);r.controlY=r.chartY+r.chartHeight+10;
        return r;
    }
};
}
