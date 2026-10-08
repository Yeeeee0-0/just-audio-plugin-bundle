#include "Engine.hpp"
#include "EnvelopeDisplay.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <array>
#include <cstdio>
#include <string>

namespace just::compressor {
namespace {
constexpr wchar_t className[]=L"JUST.Compressor.Content.010";
unsigned instances=0;
std::wstring wide(const char* value) {
    const int size=MultiByteToWideChar(CP_UTF8,0,value,-1,nullptr,0);
    if(size<=0)return {};std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,value,-1,result.data(),size);result.pop_back();return result;
}
DisplayPolicy policyFor(std::size_t i) {
    static constexpr const char* labels[]={"阈值","压缩比","启动","释放","风格","检测器","峰值/RMS 混合","拐点","衰减范围","输入增益","补偿增益","混合","立体声联动","侧链来源","侧链增益","侧链高通开关","侧链高通","侧链低通开关","侧链低通","前瞻（待支持）"};
    DisplayPolicy p;p.labelZh=labels[i];return p;
}
class WindowsEditor final:public EditorContent {
    HWND body=nullptr,envelope=nullptr,reduction=nullptr,additional=nullptr,tooltip=nullptr;
    HMODULE image=nullptr;HFONT font=nullptr;HBRUSH background=nullptr;
    EditorServices services{};std::array<std::unique_ptr<RotaryControl>,20> controls;
    EnvelopeDisplay display;std::uint64_t visualResumeGeneration=0;
    int width=700,height=300,scroll=0,documentHeight=300,chartHeight=120,lastY=0;
    bool advanced=false,dragging=false,classAcquired=false;double threshold=1.,fontScale=0;
    const char* tr(const char* zh,const char* en)const{return services.view?localized(*services.view,zh,en):en;}
    COLORREF color(COLORREF value)const {
        if(!services.view || !services.view->visualsPaused)return value;
        const auto gray=BYTE((GetRValue(value)*299+GetGValue(value)*587+GetBValue(value)*114)/1000);return RGB(gray,gray,gray);
    }
    void text(HDC dc,const char* value,RECT area,int size=11,bool center=false,COLORREF ink=RGB(117,148,158))const {
        auto f=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto old=SelectObject(dc,f);SetTextColor(dc,color(ink));SetBkMode(dc,TRANSPARENT);auto label=wide(value);
        DrawTextW(dc,label.c_str(),int(label.size()),&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(center?DT_CENTER:DT_LEFT));SelectObject(dc,old);DeleteObject(f);
    }
    void card(HDC dc,RECT r)const {
        auto b=CreateSolidBrush(color(RGB(245,251,252)));auto p=CreatePen(PS_SOLID,1,color(RGB(212,230,237)));
        auto ob=SelectObject(dc,b),op=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,24,24);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);
    }
    RECT plot()const {auto r=win::size(envelope);return {26,36,std::max(27L,LONG(r.Width)-26),std::max(37L,LONG(r.Height)-30)};}
    RECT thresholdLabel()const {auto r=plot();const LONG y=LONG(levelY(module::parameters[1].toPhysical(threshold),r.top,r.bottom-r.top));const LONG top=std::clamp(y-10,r.top,r.bottom-20);return {r.left,top,r.left+98,top+20};}
    void finishGesture(){if(dragging){dragging=false;services.endEdit(services.owner,100);}if(GetCapture()==envelope)ReleaseCapture();}
    void drawEnvelope(HDC dc,RECT bounds) {
        card(dc,bounds);const auto r=plot();
        text(dc,tr("输入 / 输出包络","INPUT / OUTPUT ENVELOPE"),{18,10,bounds.right-18,30});
        auto p=CreatePen(PS_SOLID,1,color(RGB(225,238,242)));auto old=SelectObject(dc,p);
        for(int i=0;i<=4;++i){int y=r.top+(r.bottom-r.top)*i/4;MoveToEx(dc,r.left,y,nullptr);LineTo(dc,r.right,y);}
        for(int i=0;i<=8;++i){int x=r.left+(r.right-r.left)*i/8;MoveToEx(dc,x,r.top,nullptr);LineTo(dc,x,r.bottom);}
        SelectObject(dc,old);DeleteObject(p);
        if(display.availability==AnalysisAvailability::fresh && display.count && display.latest().header.sampleRate>0) {
            const auto& newest=display.latest();const double span=newest.header.sampleRate*6.;int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
            for(unsigned tap=0;tap<2;++tap){p=CreatePen(PS_SOLID,tap?2:1,color(tap?RGB(18,184,199):RGB(171,201,214)));old=SelectObject(dc,p);bool started=false;std::uint64_t next=0;
                for(std::size_t i=0;i<display.count;++i){const auto& item=display.at(i);const auto& h=item.header;
                    if(!(h.flags&analysisInputAligned) || h.flags&analysisInvalid){started=false;continue;}
                    const int x=int(r.right-(double(newest.header.endSample)-double(h.endSample))/span*(r.right-r.left));if(x<r.left){started=false;continue;}
                    const double peak=std::max(item.channels[tap*2].peak,item.channels[tap*2+1].peak);
                    const int y=int(levelY(20*std::log10(std::max(peak,1e-8)),r.top,r.bottom-r.top));
                    if(!started || next!=h.startSample || h.flags&analysisGap)MoveToEx(dc,x,y,nullptr);else LineTo(dc,x,y);started=true;next=h.endSample;
                }SelectObject(dc,old);DeleteObject(p);
            }RestoreDC(dc,saved);
        }else text(dc,display.availability==AnalysisAvailability::stale?tr("反馈已过期 · 暂无新音频","Feedback stale · no new audio"):tr("反馈不可用","Feedback unavailable"),r,12,true);
        const double db=module::parameters[1].toPhysical(threshold);const int y=int(levelY(db,r.top,r.bottom-r.top));
        p=CreatePen(PS_DASH,1,color(RGB(20,150,171)));old=SelectObject(dc,p);MoveToEx(dc,r.left,y,nullptr);LineTo(dc,r.right,y);SelectObject(dc,old);DeleteObject(p);
        auto label=thresholdLabel();auto b=CreateSolidBrush(color(RGB(224,247,250)));p=CreatePen(PS_SOLID,1,color(RGB(20,150,171)));auto ob=SelectObject(dc,b);old=SelectObject(dc,p);RoundRect(dc,label.left,label.top,label.right,label.bottom,10,10);SelectObject(dc,ob);SelectObject(dc,old);DeleteObject(b);DeleteObject(p);
        char value[96];std::snprintf(value,sizeof(value),"%.1f dBFS",db);text(dc,value,label,10,true,RGB(0,138,158));
        std::string note=tr("灰色输入 / 青色输出 · −60…0 dBFS · 6 s","Gray IN / cyan OUT · −60…0 dBFS · 6 s");
        if(display.availability==AnalysisAvailability::fresh && display.count){const auto flags=display.latest().header.flags;if(flags&analysisBypassed)note+=tr(" · 旁路"," · Bypass");else if(flags&analysisTransportKnown && !(flags&analysisPlaying))note+=tr(" · 停止"," · Stopped");}
        text(dc,note.c_str(),{18,bounds.bottom-22,bounds.right-18,bounds.bottom-6},9);
    }
    void drawReduction(HDC dc,RECT r) {
        card(dc,r);const int w=r.right,h=r.bottom;text(dc,tr("增益衰减","GAIN REDUCTION"),{18,10,w-18,30});
        const auto* latest=display.availability==AnalysisAvailability::fresh && display.count?&display.latest():nullptr;
        const bool valid=latest && latest->effectFields&analysisReduction && !(latest->header.flags&analysisInvalid);
        const int size=h<160?25:40,y=h<160?33:int(h*.30);char value[80];if(valid)std::snprintf(value,sizeof(value),"%.1f",latest->reductionDb);else std::snprintf(value,sizeof(value),"—");
        text(dc,value,{8,y,w-8,y+size+8},size,true,RGB(18,158,176));
        text(dc,valid?tr("dB · 压缩级","dB · compression stage"):display.availability==AnalysisAvailability::stale?tr("已过期","Stale"):tr("不可用","Unavailable"),{8,y+size+6,w-8,y+size+20},9,true);
        const int railY=h>=180?int(h*.70):h-38,railH=h>=180?18:8;const double railW=w-36.;
        for(unsigned i=0;i<30;++i){auto b=CreateSolidBrush(color(valid && i<latest->reductionDb?RGB(18,184,199):RGB(214,232,240)));auto old=SelectObject(dc,b);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));int x=18+int(i*railW/30);RoundRect(dc,x,railY,x+std::max(2,int(railW/30)-3),railY+railH,4,4);SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(b);}
        if(h>=180){text(dc,"0",{18,railY+23,48,railY+37},9);text(dc,"30+",{w-50,railY+23,w-18,railY+37},9);}
        const char* sc=tr("侧链不可用","SC unavailable");if(latest && latest->effectFields&analysisSidechain){auto f=latest->effectFlags;sc=!(f&1)?tr("内部侧链","SC Internal"):!(f&2)?tr("外部侧链 · 未连接","SC External · missing"):!(f&4)?tr("外部侧链 · 静音","SC External · silent"):tr("外部侧链 · 活动","SC External · active");}
        text(dc,sc,{8,h-19,w-8,h-5},9,true);
    }
    void layout() {
        if(!body)return;ShowScrollBar(body,SB_VERT,advanced);const int w=int(win::size(body).Width);
        const int margin=w>=900?34:16,gap=w>=900?22:14,side=std::clamp(int(w*.29),196,305);chartHeight=std::clamp(height-172,120,270);
        const int columns=std::max(2,(w-36)/132);documentHeight=std::max(height,advanced?chartHeight+28+146+24+((16+columns-1)/columns)*146+12:chartHeight+172);
        scroll=std::clamp(scroll,0,std::max(0,documentHeight-height));SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;si.nMax=documentHeight-1;si.nPage=height;si.nPos=scroll;SetScrollInfo(body,SB_VERT,&si,TRUE);
        win::place(envelope,margin,16-scroll,std::max(1,w-2*margin-gap-side),chartHeight);win::place(reduction,w-margin-side,16-scroll,side,chartHeight);
        int y=chartHeight+28-scroll,cell=std::min(148,(w-32)/4),start=(w-cell*4)/2;for(unsigned i=0;i<4;++i)controls[i]->resize(start+cell*i,y,cell,132);
        y+=146;win::place(additional,18,y,w-36,16);ShowWindow(additional,advanced?SW_SHOW:SW_HIDE);y+=24;
        const int advancedCell=(w-36)/columns;for(unsigned i=4;i<controls.size();++i){auto n=i-4;controls[i]->resize(18+n%columns*advancedCell,y+n/columns*146,advancedCell,136);ShowWindow(static_cast<HWND>(controls[i]->nativeHandle()),advanced?SW_SHOW:SW_HIDE);}
    }
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(w,m,wp,lp);
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_CTLCOLORSTATIC){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,self->color(RGB(117,148,158)));SetBkColor(dc,self->color(RGB(250,252,253)));return reinterpret_cast<LRESULT>(self->background);}
        if(m==WM_PAINT || m==WM_PRINTCLIENT){win::Paint paint(w,true,m==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):nullptr);paint.graphics().Flush(Gdiplus::FlushIntentionSync);auto dc=paint.dc();int saved=SaveDC(dc);const int scale=int(std::lround(1000*win::scale(w)));SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,1000,nullptr);SetViewportExtEx(dc,scale,scale,nullptr);RECT r{0,0,LONG(paint.width()),LONG(paint.height())};FillRect(dc,&r,self->background);
            if(w==self->envelope)self->drawEnvelope(dc,r);else if(w==self->reduction)self->drawReduction(dc,r);RestoreDC(dc,saved);return 0;}
        if(w==self->envelope){
            if(m==WM_LBUTTONDOWN){auto pointer=win::point(w,lp);POINT p{LONG(pointer.X),LONG(pointer.Y)};auto plot=self->plot(),label=self->thresholdLabel();const double y=levelY(module::parameters[1].toPhysical(self->threshold),plot.top,plot.bottom-plot.top);
                if(p.x>=plot.left && p.x<=plot.right && (std::abs(p.y-y)<=12 || PtInRect(&label,p))){self->finishGesture();SetFocus(w);self->threshold=self->services.readTarget(self->services.owner,100);self->lastY=p.y;self->dragging=self->services.beginEdit(self->services.owner,100);if(self->dragging)SetCapture(w);}return 0;}
            if(m==WM_MOUSEMOVE && self->dragging){const int y=int(win::point(w,lp).Y);auto r=self->plot();const double next=std::clamp(self->threshold+(self->lastY-y)*((GetKeyState(VK_SHIFT)&0x8000)?.1:1.)/double(r.bottom-r.top),0.,1.);self->lastY=y;
                if(next!=self->threshold && self->services.performEdit(self->services.owner,100,next))self->threshold=self->services.readTarget(self->services.owner,100);InvalidateRect(w,nullptr,FALSE);return 0;}
            if(m==WM_LBUTTONUP || m==WM_CAPTURECHANGED || m==WM_KILLFOCUS || m==WM_CANCELMODE || (m==WM_KEYDOWN && wp==VK_ESCAPE)){self->finishGesture();return 0;}
            if(m==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_SIZENS));return TRUE;}
        }
        if((m==WM_VSCROLL || m==WM_MOUSEWHEEL) && self->advanced){int next=self->scroll;
            if(m==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;
            else switch(LOWORD(wp)){case SB_LINEUP:next-=30;break;case SB_LINEDOWN:next+=30;break;case SB_PAGEUP:next-=self->height;break;case SB_PAGEDOWN:next+=self->height;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_TRACKPOS;GetScrollInfo(self->body,SB_VERT,&si);next=si.nTrackPos;break;}}
            self->scroll=std::clamp(next,0,std::max(0,self->documentHeight-self->height));self->layout();return 0;
        }
        return DefWindowProcW(w,m,wp,lp);
    }
public:
    ~WindowsEditor() override {finishGesture();for(auto& c:controls)c.reset();if(tooltip)DestroyWindow(tooltip);if(body)DestroyWindow(body);if(font)DeleteObject(font);if(background)DeleteObject(background);if(classAcquired && !--instances)UnregisterClassW(className,image);}
    bool attach(void* parent,const EditorServices& s) override {
        if(body || !parent || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;threshold=s.readTarget(s.owner,100);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&image);
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=image;wc.lpszClassName=className;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!instances && !RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;++instances;classAcquired=true;
        background=CreateSolidBrush(color(RGB(250,252,253)));font=CreateFontW(-10,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        body=CreateWindowExW(0,className,L"JUST Compressor controls",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,width,height,static_cast<HWND>(parent),nullptr,image,this);if(!body)return false;
        envelope=CreateWindowExW(0,className,L"Input/output envelope; drag threshold line",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,body,nullptr,image,this);
        reduction=CreateWindowExW(0,className,L"Measured gain reduction and sidechain state",WS_CHILD|WS_VISIBLE,0,0,1,1,body,nullptr,image,this);
        additional=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT,0,0,1,1,body,nullptr,image,nullptr);SendMessageW(additional,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        for(std::size_t i=0;i<controls.size();++i){controls[i]=RotaryControl::create(body,s,module::parameters[i+1],policyFor(i));if(!controls[i])return false;}
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_WIN95_CLASSES};InitCommonControlsEx(&init);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,body,nullptr,image,nullptr);
        if(tooltip){TOOLINFOW info{};info.cbSize=sizeof(info);info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=body;info.uId=reinterpret_cast<UINT_PTR>(controls[19]->nativeHandle());info.lpszText=const_cast<wchar_t*>(L"Saved Lookahead request retained. Actual lookahead and PDC are 0 samples; activation unavailable.");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
        refresh(*s.view,{});return true;
    }
    void resize(int w,int h) override {width=std::max(1,w);height=std::max(1,h);if(body){win::place(body,0,0,width,height);layout();}}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {
        if(advanced!=view.advanced){finishGesture();for(auto& c:controls)c->refresh(false);advanced=view.advanced;scroll=0;}
        if(!dragging)threshold=services.readTarget(services.owner,100);
        if(visualResumeGeneration!=view.visualResumeGeneration){display={};visualResumeGeneration=view.visualResumeGeneration;}
        if(!view.visualsPaused)display.refresh(services);
        if(background)DeleteObject(background);background=CreateSolidBrush(color(RGB(250,252,253)));
        if(fontScale!=win::scale(body)){fontScale=win::scale(body);auto nextFont=CreateFontW(-int(std::lround(10*win::scale(body))),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessageW(additional,WM_SETFONT,reinterpret_cast<WPARAM>(nextFont),TRUE);if(font)DeleteObject(font);font=nextFont;}
        SetWindowTextW(additional,wide(tr("进阶控制 · 实际 PDC 0 样本 · 侧链监听不可用","ADDITIONAL CONTROLS · Actual PDC 0 samples · SC Listen unavailable")).c_str());
        for(std::size_t i=0;i<controls.size();++i)controls[i]->refresh(i!=19 && (i<4 || advanced));layout();InvalidateRect(body,nullptr,FALSE);InvalidateRect(envelope,nullptr,FALSE);InvalidateRect(reduction,nullptr,FALSE);
    }
};
}
EditorContent* createCompressorEditor(){return new WindowsEditor;}
}
