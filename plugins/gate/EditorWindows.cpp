#include "EnvelopeModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <windows.h>
#include <string>
#include <vector>
namespace just::gate {
namespace {
constexpr wchar_t className[]=L"JUST.Gate.Preview03.Content";
unsigned instances=0;
std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n);if(!s.empty())s.pop_back();return s;}
struct WindowsContent final:EditorContent {
    HWND window=nullptr,chart=nullptr,controlHost=nullptr,tooltip=nullptr;HBRUSH background=nullptr;std::wstring tipText;std::uint64_t visualResumeGeneration=0;HMODULE module=nullptr;HFONT font=nullptr;
    EditorServices services{};EnvelopeHistory history;
    std::vector<std::unique_ptr<RotaryControl>> primary;
    std::vector<std::pair<ParamID,std::unique_ptr<RotaryControl>>> extra;
    std::array<HWND,parameterCount> menus{},labels{};
    HWND notice=nullptr;int width=680,height=310,offset=0,documentHeight=154;bool advanced=false,lastPaused=false;
    ~WindowsContent() override {
        primary.clear();extra.clear();if(tooltip)DestroyWindow(tooltip);if(background)DeleteObject(background);if(window)DestroyWindow(window);if(font)DeleteObject(font);
        if(module && instances && !--instances)UnregisterClassW(className,module);
    }
    const char* tr(const char* zh,const char* en)const{return uiText(services,zh,en);}
    void addTooltip(HWND target){if(!tooltip || !target)return;TOOLINFOW t{};t.cbSize=sizeof(t);t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.hwnd=window;t.uId=reinterpret_cast<UINT_PTR>(target);t.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));}
    const char* tooltipText(HWND target){if(target==chart)return tr("两秒真实输入/输出峰值与实际增益。虚线为当前阈值设置；原始输入/输出不是侧链检测器，间断处不插值。","Two seconds of real raw input/output peaks and applied gain. Dashed line is the current threshold setting. Raw IO is not the filtered/RMS/External SC detector signal. Gaps are not interpolated.");
        ParamID id=0;for(unsigned i=0;i<primary.size();++i)if(primary[i]->nativeHandle()==target)id=simpleIDs[i];for(auto& e:extra)if(e.second->nativeHandle()==target)id=e.first;for(auto p:additionalIDs)if(menus[registry.index(p)]==target)id=p;
        if(!id)return "";auto a=availability(id,services);return a.enabled?help(id,unsigned(spec(mode).toPhysical(read(services,mode)))):a.reason;
    }
    void layout(){
        auto l=PreviewLayout::fit(width,height);win::place(chart,l.margin,l.chartY,width-2*l.margin,l.chartHeight);
        win::place(controlHost,0,l.controlY,width,std::max(148,height-l.controlY));
        auto extent=win::size(controlHost);RECT body{0,0,LONG(extent.Width),LONG(extent.Height)};const int dw=body.right,columns=std::max(4,dw/144),cell=dw/columns;
        const int rows=(std::size(additionalIDs)+columns-1)/columns;documentHeight=advanced?154+rows*154+90:154;
        SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;si.nMax=documentHeight-1;si.nPage=body.bottom;si.nPos=offset;
        SetScrollInfo(controlHost,SB_VERT,&si,TRUE);ShowScrollBar(controlHost,SB_VERT,advanced);offset=GetScrollPos(controlHost,SB_VERT);
        for(unsigned i=0;i<primary.size();++i){int pc=std::min(177,dw/4);primary[i]->resize((dw-4*pc)/2+pc*i+(pc-l.controlWidth)/2,-offset,l.controlWidth,148);}
        unsigned index=0,rotary=0;
        for(auto id:additionalIDs){int x=(index%columns)*cell,y=154+(index/columns)*154-offset;auto n=registry.index(id);
            if(menus[n]){win::place(labels[n],x,y,cell,22);win::place(menus[n],x+6,y+45,cell-12,180);ShowWindow(labels[n],advanced?SW_SHOW:SW_HIDE);ShowWindow(menus[n],advanced?SW_SHOW:SW_HIDE);}
            else{extra[rotary].second->resize(x+(cell-112)/2,y,112,148);ShowWindow(static_cast<HWND>(extra[rotary++].second->nativeHandle()),advanced?SW_SHOW:SW_HIDE);}++index;
        }
        win::place(notice,16,154+rows*154+8-offset,dw-32,80);ShowWindow(notice,advanced?SW_SHOW:SW_HIDE);InvalidateRect(chart,nullptr,TRUE);
    }
    void drawChart(HDC dc){
        auto size=win::size(chart);RECT rect{0,0,LONG(size.Width),LONG(size.Height)};auto fill=CreateSolidBrush(RGB(245,251,252));auto border=CreatePen(PS_SOLID,1,RGB(214,230,235));auto ob=SelectObject(dc,fill),op=SelectObject(dc,border);RoundRect(dc,0,0,rect.right,rect.bottom,24,24);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(fill);DeleteObject(border);SetBkMode(dc,TRANSPARENT);auto chartFont=CreateFontW(-11,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");auto oldFont=SelectObject(dc,chartFont);
        auto text=[&](int x,int y,const std::string& s){auto value=wide(s.c_str());TextOutW(dc,x,y,value.c_str(),int(value.size()));};
        const int left=26,right=rect.right-26,top=34,bottom=std::max(top+18,int(rect.bottom*.54)),gainTop=bottom+16,gainBottom=std::max(gainTop+6,int(rect.bottom)-28);
        SetTextColor(dc,RGB(110,142,153));text(20,10,tr("门限包络","GATE ENVELOPE"));text(std::max(20L,rect.right-155),10,tr("输入 / 输出峰值","Input / output peaks"));
        HPEN grid=CreatePen(PS_SOLID,1,RGB(220,236,241));auto old=SelectObject(dc,grid);
        for(unsigned i=0;i<=12;++i){int x=left+(right-left)*i/12;MoveToEx(dc,x,top,nullptr);LineTo(dc,x,gainBottom);}for(unsigned i=0;i<=3;++i){int y=top+(bottom-top)*i/3;MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);}SelectObject(dc,old);DeleteObject(grid);
        auto levelY=[&](double db){return bottom-int(std::clamp((db+96)/102.,0.,1.)*(bottom-top));};
        double threshold=spec(gate::threshold).toPhysical(read(services,gate::threshold));int ty=levelY(threshold);HPEN dash=CreatePen(PS_DASH,1,RGB(124,156,168));old=SelectObject(dc,dash);MoveToEx(dc,left,ty,nullptr);LineTo(dc,right,ty);SelectObject(dc,old);DeleteObject(dash);
        char value[100];std::snprintf(value,sizeof(value),tr("阈值设置 %.1f dBFS","Threshold setting %.1f dBFS"),threshold);text(std::max(left,right-200),std::max(top,ty-14),value);
        std::string status=tr("测量不可用","Measurements unavailable");
        if(history.availability==AnalysisAvailability::stale)status=tr("数据已过期 · 保留最后音频","Measurements stale — last audio held");
        else if(history.availability==AnalysisAvailability::fresh && history.count){const auto& last=history.latest();const unsigned m=last.gateState>>8,phase=last.gateState&255;static const char* phases[]={"Closed","Opening","Open","Holding","Closing"};static const char* phasesZh[]={"关闭","打开中","已打开","保持","关闭中"};
            status=(last.effectFields&analysisGate)?(m==1?tr("扩展","Expand"):std::string(m==2?tr("闪避","Duck"):tr("门控","Gate"))+" · "+(phase<5?tr(phasesZh[phase],phases[phase]):tr("未知","Unknown"))):tr("门态不可用","Gate state unavailable");
            if(last.effectFlags&8)status+=tr(" · 模式过渡"," · transitioning");if(last.effectFlags&1)status+=(last.effectFlags&2)?((last.effectFlags&4)?tr(" · 侧链已连接"," · SC connected"):tr(" · 侧链静音"," · SC silent")):tr(" · 侧链缺失"," · SC missing");
            if(last.header.flags&analysisBypassed)status+=tr(" · 旁通"," · Bypassed");else if((last.header.flags&analysisTransportKnown) && !(last.header.flags&analysisPlaying))status+=tr(" · 已停止"," · Stopped");
        }
        text(20,rect.bottom-20,status);if(!history.count || history.latest().header.sampleRate<=0){SelectObject(dc,oldFont);DeleteObject(chartFont);return;}const auto& last=history.latest();const double span=last.header.sampleRate*2.;
        const int clip=SaveDC(dc);IntersectClipRect(dc,left,top,right,gainBottom);
        for(unsigned kind=0;kind<3;++kind){HPEN pen=CreatePen(PS_SOLID,kind==0?1:2,history.availability==AnalysisAvailability::fresh?(kind==0?RGB(177,201,211):RGB(18,184,198)):RGB(159,191,201));old=SelectObject(dc,pen);bool continuous=false;
            for(std::size_t i=0;i<history.count;++i){const auto& item=history.at(i);int x=right-int((double(last.header.endSample)-double(item.header.endSample))/span*(right-left));
                if(x<left || item.header.flags&analysisInvalid || !(item.header.flags&analysisInputAligned) || (kind==2 && !(item.effectFields&analysisReduction))){continuous=false;continue;}
                int y=kind==2?gainTop+int(std::clamp(item.reductionDb/90.,0.,1.)*(gainBottom-gainTop)):levelY(EnvelopeHistory::peakDb(kind==0?EnvelopeHistory::inputPeak(item):EnvelopeHistory::outputPeak(item)));
                if(continuous && i && EnvelopeHistory::joins(history.at(i-1),item))LineTo(dc,x,y);else MoveToEx(dc,x,y,nullptr);continuous=true;
            }SelectObject(dc,old);DeleteObject(pen);
        }
        RestoreDC(dc,clip);if(last.effectFields&analysisReduction)std::snprintf(value,sizeof(value),tr("实际增益 −%.1f dB · 2 秒测量","Applied gain −%.1f dB · measured 2 s"),last.reductionDb);else std::snprintf(value,sizeof(value),"%s",tr("实际增益不可用","Applied gain unavailable"));text(left,gainTop-14,value);SelectObject(dc,oldFont);DeleteObject(chartFont);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_CTLCOLORSTATIC){auto dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,win::paused(hwnd)?RGB(249,249,249):RGB(245,250,251));SetTextColor(dc,win::paused(hwnd)?RGB(145,145,145):RGB(110,142,153));return reinterpret_cast<LRESULT>(self->background);}
        if(message==WM_NOTIFY && reinterpret_cast<NMHDR*>(lp)->code==TTN_GETDISPINFOW){auto* info=reinterpret_cast<NMTTDISPINFOW*>(lp);self->tipText=wide(self->tooltipText(reinterpret_cast<HWND>(info->hdr.idFrom)));info->lpszText=self->tipText.data();return 0;}
        if(message==WM_PAINT || message==WM_PRINTCLIENT){win::Paint p(hwnd,true,message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):nullptr);p.graphics().Flush(Gdiplus::FlushIntentionSync);auto dc=p.dc();int saved=SaveDC(dc);int scale=int(std::lround(1000*win::scale(hwnd)));SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,1000,nullptr);SetViewportExtEx(dc,scale,scale,nullptr);if(hwnd==self->chart)self->drawChart(dc);RestoreDC(dc,saved);return 0;}
        if(message==WM_COMMAND && HIWORD(wp)==CBN_SELCHANGE){auto i=unsigned(LOWORD(wp)-500);if(i<parameterCount && self->menus[i]){const auto& p=parameters[i];auto n=SendMessageW(self->menus[i],CB_GETCURSEL,0,0);if(n>=0)writeOne(self->services,p.id,double(n)/p.stepCount);self->refresh({},{});}return 0;}
        if(hwnd==self->controlHost && (message==WM_VSCROLL || message==WM_MOUSEWHEEL)){
            if(!self->advanced)return 0;int next=self->offset;auto extent=win::size(hwnd);RECT r{0,0,LONG(extent.Width),LONG(extent.Height)};
            if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;
            else switch(LOWORD(wp)){case SB_LINEUP:next-=30;break;case SB_LINEDOWN:next+=30;break;case SB_PAGEUP:next-=r.bottom;break;case SB_PAGEDOWN:next+=r.bottom;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:{SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_TRACKPOS;GetScrollInfo(hwnd,SB_VERT,&si);next=si.nTrackPos;break;}}
            self->offset=std::clamp(next,0,std::max(0,self->documentHeight-int(r.bottom)));self->layout();return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    bool attach(void* parent,const EditorServices& s) override {
        if(window || !parent || !s.readTarget)return false;services=s;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        if(!instances){WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=className;c.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!RegisterClassW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;}
        ++instances;window=CreateWindowExW(0,className,L"Gate",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        background=CreateSolidBrush(RGB(245,250,251));font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        chart=CreateWindowExW(0,className,L"Measured Gate IO peaks and applied gain; threshold is a setting, not the SC detector",WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,module,this);
        controlHost=CreateWindowExW(0,className,L"Gate controls",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,window,nullptr,module,this);
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_WIN95_CLASSES};InitCommonControlsEx(&init);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,module,nullptr);if(tooltip)SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,460);addTooltip(chart);
        for(auto id:simpleIDs){auto c=RotaryControl::create(controlHost,s,spec(id),displayPolicy(id));if(!c)return false;addTooltip(static_cast<HWND>(c->nativeHandle()));primary.push_back(std::move(c));}
        for(auto id:additionalIDs){auto i=registry.index(id);const auto& p=parameters[i];if(p.enumLabels){labels[i]=CreateWindowExW(0,L"STATIC",wide(p.title).c_str(),WS_CHILD|SS_CENTER,0,0,1,1,controlHost,nullptr,module,nullptr);menus[i]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,1,1,controlHost,reinterpret_cast<HMENU>(500+i),module,nullptr);
                for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(menus[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.enumLabels[n]).c_str()));SendMessageW(labels[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(menus[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);addTooltip(menus[i]);
            }else{auto c=RotaryControl::create(controlHost,s,p,displayPolicy(id));if(!c)return false;addTooltip(static_cast<HWND>(c->nativeHandle()));extra.emplace_back(id,std::move(c));}}
        notice=CreateWindowExW(0,L"STATIC",L"RMS: 5 ms / Stereo Link: 100%\nLookahead saved target only; actual maximum/effective/PDC 0 ms.\nSC Listen unavailable. Raw IO peaks are not the SC detector.",WS_CHILD,0,0,1,1,controlHost,nullptr,module,nullptr);SendMessageW(notice,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        refresh({},{});layout();return true;
    }
    void resize(int w,int h) override{width=w;height=h;if(window){win::place(window,0,0,w,h);auto next=CreateFontW(-int(std::lround(11*win::scale(window))),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");for(auto x:labels)if(x)SendMessageW(x,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto x:menus)if(x)SendMessageW(x,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);SendMessageW(notice,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;layout();}}
    void refresh(const EditorViewState&,const StatusSnapshot&) override {
        bool next=services.view && services.view->advanced;if(next!=advanced){for(auto& c:primary)c->refresh(false);for(auto& c:extra)c.second->refresh(false);advanced=next;offset=0;layout();}
        const bool paused=services.view && services.view->visualsPaused;if(paused!=lastPaused){if(background)DeleteObject(background);background=CreateSolidBrush(paused?RGB(249,249,249):RGB(245,250,251));lastPaused=paused;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
        for(unsigned i=0;i<primary.size();++i)primary[i]->refresh(availability(simpleIDs[i],services).enabled);
        for(auto& c:extra)c.second->refresh(availability(c.first,services).enabled);
        for(auto id:additionalIDs){auto i=registry.index(id);if(menus[i]){SetWindowTextW(labels[i],wide(uiText(services,chineseLabel(id),parameters[i].title)).c_str());EnableWindow(menus[i],availability(id,services).enabled);SendMessageW(menus[i],CB_SETCURSEL,WPARAM(std::round(read(services,id)*parameters[i].stepCount)),0);}}
        SetWindowTextW(notice,wide(tr("RMS 积分：5 ms · 立体声联动：100%\n前瞻：仅保存目标；实际最大值 / 生效值 / PDC 均为 0 ms。\n侧链监听不可用。Range 限制相对衰减。扩展比 1 时为单位增益。","RMS integration: 5 ms · Stereo Link: 100%\nLookahead: saved target only; actual maximum / effective / PDC = 0 ms.\nSC Listen unavailable. Range limits relative attenuation. Expand Ratio 1 gives unity.")).c_str());
        if(services.view && visualResumeGeneration!=services.view->visualResumeGeneration){history={};visualResumeGeneration=services.view->visualResumeGeneration;}
        if(!services.view || !services.view->visualsPaused)history.refresh(services);if(chart)InvalidateRect(chart,nullptr,FALSE);
    }
};
}
EditorContent* createEditorContent(){return new WindowsContent;}
}
