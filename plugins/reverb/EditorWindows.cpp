#include "Editor.hpp"
#include "Presets.hpp"
#include "FeedbackHistory.hpp"
#include "SimpleLayout.hpp"
#include "UiText.hpp"
#include "common/ui/Controls.hpp"
#include <windows.h>
#include <windowsx.h>
#include <string>
namespace just::reverb {
namespace {
std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n);return s;}
SoundState readTargets(const EditorServices& s){SoundState state;for(unsigned i=0;i<registry.count;++i)state.targets[i]=s.readTarget(s.owner,parameters[i].id);return state;}
struct MacroAdapter {
    EditorServices services{};SimpleMacro macro=SimpleMacro::mix;MacroGesture gesture;
    RotaryBinding binding(){RotaryBinding b;b.owner=this;b.view=services.view;
        b.read=[](void* p){auto& a=*static_cast<MacroAdapter*>(p);return macroNormalized(a.macro,readTargets(a.services));};
        b.begin=[](void* p){if(!macroWiringApproved)return false;auto& a=*static_cast<MacroAdapter*>(p);return a.gesture.begin(a.services,a.macro,readTargets(a.services));};
        b.write=[](void* p,double v){return static_cast<MacroAdapter*>(p)->gesture.update(v);};
        b.end=[](void* p){static_cast<MacroAdapter*>(p)->gesture.end();};return b;}
};
class WindowsContent final:public EditorContent {
    HWND window=nullptr;HMODULE module=nullptr;EditorServices services{};
    bool advanced=false;int width=1120,height=460,offset=0;
    FeedbackHistory feedback;
    std::array<MacroAdapter,simpleMacroCount> adapters;
    std::array<std::unique_ptr<RotaryControl>,simpleMacroCount> simple;
    std::array<std::unique_ptr<RotaryControl>,registry.count-1> detail;
    std::array<ParamID,registry.count-1> detailIDs{};
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(msg==WM_PAINT){self->paint();return 0;}
            if(msg==WM_VSCROLL || msg==WM_MOUSEWHEEL){
                int delta=msg==WM_MOUSEWHEEL?-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*44:LOWORD(wp)==SB_LINEDOWN?44:LOWORD(wp)==SB_LINEUP?-44:LOWORD(wp)==SB_PAGEDOWN?self->height:LOWORD(wp)==SB_PAGEUP?-self->height:0;
                if(msg==WM_VSCROLL && (LOWORD(wp)==SB_THUMBTRACK || LOWORD(wp)==SB_THUMBPOSITION))self->offset=HIWORD(wp);else self->offset+=delta;
                self->offset=std::clamp(self->offset,0,std::max(0,self->totalHeight()-self->height));self->layout();return 0;
            }
        }return DefWindowProcW(h,msg,wp,lp);
    }
    int totalHeight() const {return SimpleLayout::fit(width).height+(advanced?20+220+6*156:0);}
    void layout(){
        const auto layout=SimpleLayout::fit(width);
        for(unsigned n=0;n<simple.size();++n){const auto& r=layout.controls[n];simple[n]->resize(r.x,r.y-offset,r.width,r.height);}
        const int cell=(width-72)/6,start=layout.height+20;
        for(unsigned n=0;n<detail.size();++n){ShowWindow(static_cast<HWND>(detail[n]->nativeHandle()),advanced?SW_SHOW:SW_HIDE);
            int x=n<4?36+(width-72-4*cell)/2+int(n)*cell:36+int((n-4)%6)*cell;
            int y=n<4?start+30:start+220+int((n-4)/6)*156;
            detail[n]->resize(x+std::max(0,(cell-112)/2),y-offset,std::min(112,cell),148);
        }
        ShowScrollBar(window,SB_VERT,totalHeight()>height);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,totalHeight(),UINT(std::max(1,height)),offset,0};SetScrollInfo(window,SB_VERT,&info,TRUE);InvalidateRect(window,nullptr,TRUE);
    }
    void paint(){
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(110,143,153));
        const auto g=SimpleLayout::fit(width).graph;int top=g.y-offset,center=top+30+(g.height-58)/2,bottom=top+g.height-28;
        TextOutW(dc,g.x,top,L"DRY INPUT → REVERB TAIL",22);
        HPEN grid=CreatePen(PS_SOLID,1,RGB(214,231,235));auto originalPen=SelectObject(dc,grid);
        for(int r=0;r<4;++r){int y=top+30+r*(g.height-58)/3;MoveToEx(dc,g.x,y,nullptr);LineTo(dc,g.x+g.width,y);}SelectObject(dc,originalPen);DeleteObject(grid);
        auto* latest=feedback.latest();const wchar_t* hint=L"Waiting for audio";
        const bool fresh=feedback.availability()==AnalysisAvailability::fresh && latest;
        bool valid=fresh && (latest->effectFields&analysisWet) && (latest->header.flags&analysisInputAligned);
        if(feedback.availability()==AnalysisAvailability::stale)hint=L"No new audio";
        else if(fresh){
            if(latest->header.flags&analysisGap){hint=L"Audio data gap";valid=false;}
            else if(!(latest->effectFields&analysisWet))hint=L"Wet measurement unavailable";
            else if(!(latest->header.flags&analysisInputAligned))hint=L"Waiting for aligned input";
            else if(latest->header.flags&analysisBypassed)hint=L"Bypass";
            else if((latest->header.flags&analysisTransportKnown) && !(latest->header.flags&analysisPlaying))hint=L"Host stopped";
            else if(std::max(latest->channels[0].peak,latest->channels[1].peak)==0)
                hint=std::max(latest->channels[4].peak,latest->channels[5].peak)>0?L"Dry silent · wet tail active":L"Silence";
            else hint=L"Measured dry / wet contribution · peak −96…0 dBFS";
        }
        RECT caption{g.x,bottom+10,g.x+g.width,bottom+28};DrawTextW(dc,hint,-1,&caption,DT_SINGLELINE|DT_END_ELLIPSIS);
        if(valid){
            int saved=SaveDC(dc);IntersectClipRect(dc,g.x,top+30,g.x+g.width,bottom);
            const double end=double(latest->header.endSample),span=latest->header.sampleRate*6.;
            feedback.each([&](const AnalysisWindow& item){if(!(item.header.flags&analysisInputAligned))return;
                const double x=g.x+g.width-(end-double(item.header.endSample))/span*g.width;if(x<g.x || x>g.x+g.width)return;
                for(unsigned series=0;series<3;++series){if(series && (!(item.effectFields&analysisWet) || item.header.flags&analysisGap))continue;
                    double peak=series?item.channels[3+series].peak:std::max(item.channels[0].peak,item.channels[1].peak);if(peak<=0)continue;
                    double a=std::clamp((20*std::log10(std::max(1e-8,peak))+96)/96.,0.,1.)*(g.height-58)*.48;
                    HPEN pen=CreatePen(PS_SOLID,2,series==0?RGB(87,138,153):series==1?RGB(20,171,191):RGB(112,207,217));auto old=SelectObject(dc,pen);
                    int xx=int(x)+(series==1?-1:series==2?1:0);MoveToEx(dc,xx,center-int(a),nullptr);LineTo(dc,xx,center+int(a));SelectObject(dc,old);DeleteObject(pen);
                }
            });RestoreDC(dc,saved);
        }
        if(advanced){RECT label{36,SimpleLayout::fit(width).height+20-offset,width-36,SimpleLayout::fit(width).height+42-offset};DrawTextW(dc,L"ADDITIONAL CONTROLS",-1,&label,DT_SINGLELINE);}
        EndPaint(window,&ps);
    }
public:
    ~WindowsContent() override{for(auto& c:detail)c.reset();for(auto& c:simple)c.reset();if(window)DestroyWindow(window);}
    bool attach(void* parent,const EditorServices& s) override{
        if(!parent || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        WNDCLASSW klass{};klass.lpfnWndProc=proc;klass.hInstance=module;klass.lpszClassName=L"JUST.Reverb.Content.v03";klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
        window=CreateWindowExW(0,klass.lpszClassName,L"Just Reverb",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        constexpr const char* zh[]={"明亮度","质感","距离","空间","衰减速率","立体声宽度","混合"};
        for(unsigned n=0;n<simple.size();++n){auto macro=SimpleMacro(n);adapters[n].services=s;adapters[n].macro=macro;DisplayPolicy policy;policy.labelZh=zh[n];simple[n]=RotaryControl::create(window,adapters[n].binding(),macroDisplaySpec(macro),policy);if(!simple[n])return false;}
        unsigned index=0;for(auto id:{Predelay,Diffusion,WetHP,HighCut})detailIDs[index++]=id;
        for(unsigned i=1;i<registry.count;++i){auto id=parameters[i].id;if(id!=Predelay && id!=Diffusion && id!=WetHP && id!=HighCut)detailIDs[index++]=id;}
        for(unsigned n=0;n<detail.size();++n){DisplayPolicy policy;policy.labelZh=detailLabelZh(detailIDs[n]);detail[n]=RotaryControl::create(window,s,parameters[registry.index(detailIDs[n])],policy);if(!detail[n])return false;}
        layout();return true;
    }
    void resize(int w,int h) override{width=w;height=h;MoveWindow(window,0,0,w,h,TRUE);offset=std::min(offset,std::max(0,totalHeight()-height));layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override{
        if(advanced!=view.advanced){advanced=view.advanced;offset=0;layout();}
        for(auto& c:simple)c->refresh(macroWiringApproved);const auto state=readTargets(services);
        for(unsigned n=0;n<detail.size();++n)detail[n]->refresh(!(detailIDs[n]==Predelay && physical(state,Sync)>=.5));
        feedback.poll(services);InvalidateRect(window,nullptr,TRUE);
    }
};
}
EditorContent* createEditor(){return new WindowsContent;}
}
