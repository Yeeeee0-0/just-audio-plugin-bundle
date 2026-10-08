#include "AnalysisView.hpp"
#include <windows.h>
namespace just {
namespace {
class WinAnalysis final:public AnalysisView {
    HWND window=nullptr;HMODULE module=nullptr;EditorServices services;AnalysisViewMode mode;
    AnalysisCursor cursor{};AnalysisAvailability availability=AnalysisAvailability::unavailable;
    std::uint64_t resumeGeneration=0;
    std::array<AnalysisWindow,600> history{};unsigned count=0,write=0;SampleFrame samples{};SpectrumSnapshot spectrum{};
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* p=reinterpret_cast<WinAnalysis*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){p=static_cast<WinAnalysis*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p && m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));p->paint(dc,r);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,m,w,l);
    }
    void paint(HDC dc,RECT r){
        SetBkMode(dc,TRANSPARENT);const wchar_t* label=L"Input (gray) / output (teal) — same time and scale";
        if(availability!=AnalysisAvailability::fresh){label=availability==AnalysisAvailability::stale?L"Feedback stale — no new audio":L"Feedback unavailable";TextOutW(dc,8,5,label,lstrlenW(label));return;}
        TextOutW(dc,8,5,label,lstrlenW(label));double width=std::max(1L,r.right-16),height=std::max(1L,r.bottom-36);
        auto dbY=[&](double a){return 28+height*(1-std::clamp((20*std::log10(std::max(a,1e-8))+96)/102.,0.,1.));};
        for(unsigned tap=0;tap<2;++tap){HPEN pen=CreatePen(PS_SOLID,2,tap?RGB(23,143,153):RGB(140,145,150));auto old=SelectObject(dc,pen);bool started=false;std::uint64_t next=0;
            auto point=[&](double x,double y,bool gap=false){if(!started || gap)MoveToEx(dc,int(x),int(y),nullptr);else LineTo(dc,int(x),int(y));started=true;};
            if(mode==AnalysisViewMode::spectrum){for(unsigned k=1;k<spectrumBins;++k){double hz=k*spectrum.header.sampleRate/spectrum.fftSize;if(hz<20)continue;point(8+std::log(hz/20)/std::log(spectrum.header.sampleRate/40)*width,dbY(spectrum.amplitude[tap][k]));}}
            else if(mode==AnalysisViewMode::waveform || mode==AnalysisViewMode::stereoField){if(samples.header.flags&analysisInputAligned)for(unsigned i=0;i<samples.count;++i){double l=samples.samples[tap*2][i],rr=(tap?samples.header.outputChannels:samples.header.inputChannels)==2?samples.samples[tap*2+1][i]:l;
                    if(mode==AnalysisViewMode::waveform)point(8+double(i)*width/std::max(1u,samples.count-1),28+height*.5*(1-std::clamp(l,-1.,1.)));
                    else point(8+width*.5*(1+std::clamp((l-rr)*.5,-1.,1.)),28+height*.5*(1-std::clamp((l+rr)*.5,-1.,1.)));}}
            else if(count){auto& last=history[(write+599)%600];double span=last.header.sampleRate*6;for(unsigned i=0;i<count;++i){const auto& item=history[(write+600-count+i)%600];const bool wet=mode==AnalysisViewMode::wetStereo;
                    if(!(item.header.flags&analysisInputAligned) || (wet && !(item.effectFields&analysisWet))){started=false;continue;}
                    double x=8+width-(last.header.endSample-item.header.endSample)/span*width;if(x<8){started=false;continue;}
                    double a=wet?item.channels[4+tap].peak:std::max(item.channels[2*tap].peak,item.channels[2*tap+1].peak),y=dbY(a);if(wet)y=28+tap*height*.5+(y-28)*.5;
                    point(x,y,next!=item.header.startSample || item.header.flags&analysisGap);next=item.header.endSample;}}
            SelectObject(dc,old);DeleteObject(pen);
        }
    }
public:
    WinAnalysis(HWND parent,const EditorServices& s,AnalysisViewMode m):services(s),mode(m){GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=L"JUST.Shared.Analysis.v2";RegisterClassW(&c);window=CreateWindowExW(0,c.lpszClassName,L"Audio analysis",WS_CHILD|WS_VISIBLE,0,0,600,180,parent,nullptr,module,this);}
    ~WinAnalysis() override{DestroyWindow(window);}
    void resize(int x,int y,int width,int height) override{MoveWindow(window,x,y,std::max(1,width),std::max(1,height),TRUE);}
    void refresh() override{
        if(services.view && services.view->visualsPaused)return;
        if(services.view && resumeGeneration!=services.view->visualResumeGeneration){resumeGeneration=services.view->visualResumeGeneration;cursor={};count=write=0;spectrum={};samples={};}
        if(mode==AnalysisViewMode::spectrum)availability=services.readSpectrum?services.readSpectrum(services.owner,spectrum):AnalysisAvailability::unavailable;
        else if(mode==AnalysisViewMode::waveform || mode==AnalysisViewMode::stereoField)availability=services.readSamples?services.readSamples(services.owner,samples):AnalysisAvailability::unavailable;
        else {availability=AnalysisAvailability::unavailable;for(unsigned n=0;n<16 && services.readAnalysis;++n){AnalysisBatch b;availability=services.readAnalysis(services.owner,cursor,b);if(availability!=AnalysisAvailability::fresh || !b.count)break;
                for(unsigned i=0;i<b.count;++i){const auto& item=b.windows[i];if(count){const auto& previous=history[(write+599)%600].header;if(previous.session!=item.header.session || previous.epoch!=item.header.epoch)count=write=0;}history[write]=item;write=(write+1)%600;count=std::min(600u,count+1);}}}
        InvalidateRect(window,nullptr,FALSE);
    }
    void* nativeHandle() const noexcept override{return window;}
};
}
std::unique_ptr<AnalysisView> AnalysisView::create(void* parent,const EditorServices& services,AnalysisViewMode mode){return parent?std::make_unique<WinAnalysis>(static_cast<HWND>(parent),services,mode):nullptr;}
}
