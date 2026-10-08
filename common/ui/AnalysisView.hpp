#pragma once
#include "../vst3/Module.hpp"
namespace just {
enum class AnalysisViewMode {envelope,spectrum,waveform,stereoField,wetStereo};
struct MeterHold {
    std::uint64_t session=0,epoch=0,sequence=0;
    double inputPeak=0,outputPeak=0,reductionDb=0;
    bool reductionValid=false;
    void reset() noexcept{inputPeak=outputPeak=reductionDb=0;reductionValid=false;}
    void observe(const AnalysisWindow& w) noexcept{
        auto& h=w.header;if(h.session!=session || h.epoch!=epoch){reset();sequence=0;session=h.session;epoch=h.epoch;}
        if(h.sequence<=sequence)return;sequence=h.sequence;
        inputPeak=std::max(inputPeak,std::max(w.channels[0].peak,w.channels[1].peak));
        outputPeak=std::max(outputPeak,std::max(w.channels[2].peak,w.channels[3].peak));
        if(w.effectFields&analysisReduction){reductionValid=true;reductionDb=std::max(reductionDb,w.reductionDb);}
    }
};
class AnalysisView {
public:
    virtual ~AnalysisView()=default;
    virtual void resize(int x,int y,int width,int height)=0;
    virtual void refresh()=0;
    virtual void* nativeHandle() const noexcept=0;
    static std::unique_ptr<AnalysisView> create(void* parent,const EditorServices&,AnalysisViewMode);
};
}
