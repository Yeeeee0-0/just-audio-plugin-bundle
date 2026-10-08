#include "Editor.hpp"
#include "Presets.hpp"
#include "FeedbackHistory.hpp"
#include "SimpleLayout.hpp"
#include "UiText.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <mutex>
#include <cwchar>
namespace just::reverb {
namespace {
// One registration per loaded module; instances may close in any order.
// Do not leave a WNDPROC pointing into an unloaded VST3 DLL.
struct ContentClass {
    std::mutex mutex;
    unsigned references=0;
    bool acquire(HMODULE module,WNDPROC procedure,const wchar_t* name) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!references){
            WNDCLASSW definition{};definition.lpfnWndProc=procedure;definition.hInstance=module;
            definition.lpszClassName=name;definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            if(!RegisterClassW(&definition)){
                WNDCLASSW existing{};
                if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS || !GetClassInfoW(module,name,&existing) || existing.lpfnWndProc!=procedure)return false;
            }
        }
        ++references;return true;
    }
    void release(HMODULE module,const wchar_t* name) {
        std::lock_guard<std::mutex> lock(mutex);
        if(references && --references==0)UnregisterClassW(name,module);
    }
};
ContentClass contentClass;
constexpr const wchar_t* contentClassName=L"JUST.Reverb.Content.v010";
std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n);s.resize(std::max(0,n-1));return s;}
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
    HWND window=nullptr,tooltips=nullptr;HMODULE module=nullptr;EditorServices services{};
    bool classAcquired=false,advanced=false;int width=1120,height=460,offset=0;
    FeedbackHistory feedback;
    std::uint64_t visualResumeGeneration=0;
    std::array<std::wstring,simpleMacroCount> help;
    std::wstring text(const char* zh,const char* en) const {return wide(services.view?localized(*services.view,zh,en):en);}
    std::array<MacroAdapter,simpleMacroCount> adapters;
    std::array<std::unique_ptr<RotaryControl>,simpleMacroCount> simple;
    std::array<std::unique_ptr<RotaryControl>,registry.count-1> detail;
    std::array<ParamID,registry.count-1> detailIDs{};
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(msg==WM_PAINT){self->paint();return 0;}
            if(msg==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(wp));return 0;}
            if(msg==WM_VSCROLL || msg==WM_MOUSEWHEEL){
                int delta=msg==WM_MOUSEWHEEL?-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*44:LOWORD(wp)==SB_LINEDOWN?44:LOWORD(wp)==SB_LINEUP?-44:LOWORD(wp)==SB_PAGEDOWN?self->height:LOWORD(wp)==SB_PAGEUP?-self->height:0;
                if(msg==WM_VSCROLL && (LOWORD(wp)==SB_THUMBTRACK || LOWORD(wp)==SB_THUMBPOSITION)){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(h,SB_VERT,&info);self->offset=info.nTrackPos;}else self->offset+=delta;
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
    void paint(HDC printDC=nullptr){
        win::Paint paint(window,true,printDC);auto& dc=paint.graphics();
        const auto bounds=SimpleLayout::fit(width).graph;
        const float x=float(bounds.x),top=float(bounds.y-offset),w=float(bounds.width),plotHeight=float(std::max(10,bounds.height-58));
        const float bottom=top+30+plotHeight,center=top+30+plotHeight/2;
        win::text(dc,text("干声输入 → 混响尾音","DRY INPUT → REVERB TAIL"),{x,top,w,16},10,0x6e8f99);
        for(unsigned row=0;row<4;++row){const float y=top+30+row*plotHeight/3;win::line(dc,x,y,x+w,y,0xd6e6eb);}
        const auto* latest=feedback.latest();auto hint=text("等待音频数据","Waiting for audio");
        const bool fresh=feedback.availability()==AnalysisAvailability::fresh && latest;
        bool valid=fresh && (latest->effectFields&analysisWet) && (latest->header.flags&analysisInputAligned);
        if(feedback.availability()==AnalysisAvailability::stale)hint=text("暂无新音频","No new audio");
        else if(fresh){
            if(latest->header.flags&analysisGap){hint=text("音频数据间断","Audio data gap");valid=false;}
            else if(!(latest->effectFields&analysisWet))hint=text("湿声测量不可用","Wet measurement unavailable");
            else if(!(latest->header.flags&analysisInputAligned))hint=text("等待对齐输入","Waiting for aligned input");
            else if(latest->header.flags&analysisBypassed)hint=text("旁路","Bypass");
            else if((latest->header.flags&analysisTransportKnown) && !(latest->header.flags&analysisPlaying))hint=text("宿主已停止","Host stopped");
            else if(std::max(latest->channels[0].peak,latest->channels[1].peak)==0)
                hint=std::max(latest->channels[4].peak,latest->channels[5].peak)>0?text("干声静音 · 尾音持续","Dry silent · wet tail active"):text("静音","Silence");
            else hint=text("实时干声 / 湿声贡献","Measured dry / wet contribution");
        }
        win::text(dc,hint,{x,bottom+10,w*.6f,18},10,0x6e8f99);
        win::text(dc,text("过去 6 s · 峰值 −96…0 dBFS","Past 6 s · peak −96…0 dBFS"),{x+w*.6f,bottom+10,w*.4f,18},10,0x6e8f99,false,Gdiplus::StringAlignmentFar);
        if(valid){
            const auto saved=dc.Save();dc.SetClip(Gdiplus::RectF{x,top+30,w,plotHeight});
            const double end=double(latest->header.endSample),span=latest->header.sampleRate*6.;
            feedback.each([&](const AnalysisWindow& item){if(!(item.header.flags&analysisInputAligned))return;
                const float at=float(x+w-(end-double(item.header.endSample))/span*w);if(at<x || at>x+w)return;
                for(unsigned series=0;series<3;++series){if(series && (!(item.effectFields&analysisWet) || item.header.flags&analysisGap))continue;
                    const double peak=series?item.channels[3+series].peak:std::max(item.channels[0].peak,item.channels[1].peak);if(peak<=0)continue;
                    const float a=float(std::clamp((20*std::log10(std::max(1e-8,peak))+96)/96.,0.,1.)*plotHeight*.48);
                    const float xx=at+(series==1?-1:series==2?1:0);win::line(dc,xx,center-a,xx,center+a,series==0?0x578a99:series==1?0x14abbf:0x70cfd9,series?2.2f:2.f);
                }
            });dc.Restore(saved);
        }
        if(advanced){const float start=float(SimpleLayout::fit(width).height+20-offset);
            win::text(dc,text("进阶控制","ADDITIONAL CONTROLS"),{36,start,float(width-72),24},10,0x6e8f99);
            win::text(dc,text("完整声音参数","COMPLETE SOUND PARAMETERS"),{36,start+190,float(width-72),24},10,0x6e8f99);
        }
    }
    void updateHelp(unsigned index,std::wstring next){
        if(help[index]==next)return;help[index]=std::move(next);TOOLINFOW info{sizeof(info)};
        info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(simple[index]->nativeHandle());info.lpszText=help[index].data();
        SendMessageW(tooltips,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
    }

    void close() {
        for(auto& c:detail)c.reset();for(auto& c:simple)c.reset();
        if(tooltips)DestroyWindow(tooltips);tooltips=nullptr;
        if(window)DestroyWindow(window);window=nullptr;
        if(classAcquired){contentClass.release(module,contentClassName);classAcquired=false;}
        for(auto& text:help)text.clear();
    }
    bool failAttach(){close();return false;}
public:
    ~WindowsContent() override{close();}
    bool attach(void* parent,const EditorServices& s) override{
        if(!parent || window || classAcquired || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        if(!module || !contentClass.acquire(module,proc,contentClassName))return false;classAcquired=true;
        window=CreateWindowExW(0,contentClassName,L"Just Reverb",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return failAttach();
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
        tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,window,nullptr,module,nullptr);if(!tooltips)return failAttach();SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,460);
        constexpr const char* zh[]={"明亮度","质感","距离","空间","衰减速率","立体声宽度","混合"};
        for(unsigned n=0;n<simple.size();++n){auto macro=SimpleMacro(n);adapters[n].services=s;adapters[n].macro=macro;DisplayPolicy policy;policy.labelZh=zh[n];simple[n]=RotaryControl::create(window,adapters[n].binding(),macroDisplaySpec(macro),policy);if(!simple[n])return failAttach();}
        unsigned index=0;for(auto id:{Predelay,Diffusion,WetHP,HighCut})detailIDs[index++]=id;
        for(unsigned i=1;i<registry.count;++i){auto id=parameters[i].id;if(id!=Predelay && id!=Diffusion && id!=WetHP && id!=HighCut)detailIDs[index++]=id;}
        for(unsigned n=0;n<detail.size();++n){DisplayPolicy policy;policy.labelZh=detailLabelZh(detailIDs[n]);detail[n]=RotaryControl::create(window,s,parameters[registry.index(detailIDs[n])],policy);if(!detail[n])return failAttach();}
        layout();return true;
    }
    void resize(int w,int h) override{width=w;height=h;win::place(window,0,0,w,h);offset=std::min(offset,std::max(0,totalHeight()-height));layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override{
        if(advanced!=view.advanced){advanced=view.advanced;offset=0;layout();}
        for(unsigned n=0;n<simple.size();++n){simple[n]->refresh(macroWiringApproved);updateHelp(n,wide(macroHelp(SimpleMacro(n),view.language==UiLanguage::chinese)));}
        const auto state=readTargets(services);const auto range=decayRateBounds(state);wchar_t rangeHelp[256];
        std::swprintf(rangeHelp,std::size(rangeHelp),view.language==UiLanguage::chinese?L"当前三频比例允许 %.1f–%.1f%%；中频时间 = 空间 × 实际速率 / 100，共同边界保持三段比例":L"Current spectral ratios allow %.1f–%.1f%%; mid time = Space × actual Rate / 100; joint limits preserve all ratios",range[0],range[1]);updateHelp(4,rangeHelp);
        for(unsigned n=0;n<detail.size();++n)detail[n]->refresh(!(detailIDs[n]==Predelay && physical(state,Sync)>=.5));
        if(visualResumeGeneration!=view.visualResumeGeneration){feedback={};visualResumeGeneration=view.visualResumeGeneration;}
        if(!view.visualsPaused)feedback.poll(services);InvalidateRect(window,nullptr,FALSE);
    }
};
}
EditorContent* createEditor(){return new WindowsContent;}
}
