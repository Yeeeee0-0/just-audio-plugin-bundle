#include "EnvelopeModel.hpp"
#include "common/ui/Controls.hpp"
#include <windows.h>
#include <string>
#include <vector>
namespace just::gate {
namespace {
constexpr wchar_t className[]=L"JUST.Gate.Preview03.Content";
unsigned instances=0;
std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n);if(!s.empty())s.pop_back();return s;}
struct WindowsContent final:EditorContent {
    HWND window=nullptr,chart=nullptr,controlHost=nullptr;HMODULE module=nullptr;HFONT font=nullptr;
    EditorServices services{};EnvelopeHistory history;
    std::vector<std::unique_ptr<RotaryControl>> primary;
    std::vector<std::pair<ParamID,std::unique_ptr<RotaryControl>>> extra;
    std::array<HWND,parameterCount> menus{},labels{};
    HWND notice=nullptr;int width=680,height=310,offset=0,documentHeight=154;bool advanced=false;
    ~WindowsContent() override {
        primary.clear();extra.clear();if(window)DestroyWindow(window);if(font)DeleteObject(font);
        if(module && instances && !--instances)UnregisterClassW(className,module);
    }
    void layout(){
        auto l=PreviewLayout::fit(width,height);MoveWindow(chart,l.margin,l.chartY,width-2*l.margin,l.chartHeight,TRUE);
        MoveWindow(controlHost,0,l.controlY,width,std::max(148,height-l.controlY),TRUE);
        RECT body;GetClientRect(controlHost,&body);const int dw=body.right,columns=std::max(4,dw/144),cell=dw/columns;
        const int rows=(std::size(additionalIDs)+columns-1)/columns;documentHeight=advanced?154+rows*154+90:154;
        SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;si.nMax=documentHeight-1;si.nPage=body.bottom;si.nPos=offset;
        SetScrollInfo(controlHost,SB_VERT,&si,TRUE);ShowScrollBar(controlHost,SB_VERT,advanced);offset=GetScrollPos(controlHost,SB_VERT);
        for(unsigned i=0;i<primary.size();++i){int pc=std::min(177,dw/4);primary[i]->resize((dw-4*pc)/2+pc*i+(pc-l.controlWidth)/2,-offset,l.controlWidth,148);}
        unsigned index=0,rotary=0;
        for(auto id:additionalIDs){int x=(index%columns)*cell,y=154+(index/columns)*154-offset;auto n=registry.index(id);
            if(menus[n]){MoveWindow(labels[n],x,y,cell,22,TRUE);MoveWindow(menus[n],x+6,y+45,cell-12,180,TRUE);ShowWindow(labels[n],advanced?SW_SHOW:SW_HIDE);ShowWindow(menus[n],advanced?SW_SHOW:SW_HIDE);}
            else{extra[rotary].second->resize(x+(cell-112)/2,y,112,148);ShowWindow(static_cast<HWND>(extra[rotary++].second->nativeHandle()),advanced?SW_SHOW:SW_HIDE);}++index;
        }
        MoveWindow(notice,16,154+rows*154+8-offset,dw-32,80,TRUE);ShowWindow(notice,advanced?SW_SHOW:SW_HIDE);InvalidateRect(chart,nullptr,TRUE);
    }
    void drawChart(HDC dc){
        RECT rect;GetClientRect(chart,&rect);HBRUSH fill=CreateSolidBrush(RGB(245,251,252));FillRect(dc,&rect,fill);DeleteObject(fill);SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);
        auto text=[&](int x,int y,const std::string& s){auto value=wide(s.c_str());TextOutW(dc,x,y,value.c_str(),int(value.size()));};
        const int left=26,right=rect.right-26,top=34,bottom=std::max(top+18,int(rect.bottom*.54)),gainTop=bottom+16,gainBottom=std::max(gainTop+6,rect.bottom-28);
        SetTextColor(dc,RGB(110,142,153));text(20,10,"GATE ENVELOPE");text(std::max(20,rect.right-155),10,"Input / output peaks");
        HPEN grid=CreatePen(PS_SOLID,1,RGB(220,236,241));auto old=SelectObject(dc,grid);
        for(unsigned i=0;i<=12;++i){int x=left+(right-left)*i/12;MoveToEx(dc,x,top,nullptr);LineTo(dc,x,gainBottom);}for(unsigned i=0;i<=3;++i){int y=top+(bottom-top)*i/3;MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);}SelectObject(dc,old);DeleteObject(grid);
        auto levelY=[&](double db){return bottom-int(std::clamp((db+96)/102.,0.,1.)*(bottom-top));};
        double threshold=spec(gate::threshold).toPhysical(read(services,gate::threshold));int ty=levelY(threshold);HPEN dash=CreatePen(PS_DASH,1,RGB(124,156,168));old=SelectObject(dc,dash);MoveToEx(dc,left,ty,nullptr);LineTo(dc,right,ty);SelectObject(dc,old);DeleteObject(dash);
        char value[100];std::snprintf(value,sizeof(value),"Threshold setting %.1f dBFS",threshold);text(std::max(left,right-200),std::max(top,ty-14),value);
        std::string status="Measurements unavailable";
        if(history.availability==AnalysisAvailability::stale)status="Measurements stale - last audio held";
        else if(history.count){const auto& last=history.latest();const unsigned m=last.gateState>>8,phase=last.gateState&255;static const char* phases[]={"Closed","Opening","Open","Holding","Closing"};status=(last.effectFields&analysisGate)?(m==1?"Expand":std::string(m==2?"Duck":"Gate")+" / "+(phase<5?phases[phase]:"Unknown")):"Gate state unavailable";
            if(last.effectFlags&8)status+=" / transitioning";if(last.effectFlags&1)status+=(last.effectFlags&2)?((last.effectFlags&4)?" / SC connected":" / SC silent"):" / SC missing";
            if(last.header.flags&analysisBypassed)status+=" / Bypassed";else if((last.header.flags&analysisTransportKnown) && !(last.header.flags&analysisPlaying))status+=" / Stopped";
        }
        text(20,rect.bottom-20,status);if(!history.count)return;const auto& last=history.latest();const double span=last.header.sampleRate*2.;
        for(unsigned kind=0;kind<3;++kind){HPEN pen=CreatePen(PS_SOLID,kind==0?1:2,history.availability==AnalysisAvailability::fresh?(kind==0?RGB(177,201,211):RGB(18,184,198)):RGB(159,191,201));old=SelectObject(dc,pen);bool continuous=false;
            for(std::size_t i=0;i<history.count;++i){const auto& item=history.at(i);int x=right-int((double(last.header.endSample)-double(item.header.endSample))/span*(right-left));
                if(x<left || item.header.flags&analysisInvalid || !(item.header.flags&analysisInputAligned) || (kind==2 && !(item.effectFields&analysisReduction))){continuous=false;continue;}
                int y=kind==2?gainTop+int(std::clamp(item.reductionDb/90.,0.,1.)*(gainBottom-gainTop)):levelY(EnvelopeHistory::peakDb(kind==0?EnvelopeHistory::inputPeak(item):EnvelopeHistory::outputPeak(item)));
                if(continuous && i && EnvelopeHistory::joins(history.at(i-1),item))LineTo(dc,x,y);else MoveToEx(dc,x,y,nullptr);continuous=true;
            }SelectObject(dc,old);DeleteObject(pen);
        }
        if(last.effectFields&analysisReduction)std::snprintf(value,sizeof(value),"Applied gain -%.1f dB / measured 2 s",last.reductionDb);else std::snprintf(value,sizeof(value),"Applied gain unavailable");text(left,gainTop-14,value);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        if(message==WM_PAINT && hwnd==self->chart){PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);self->drawChart(dc);EndPaint(hwnd,&p);return 0;}
        if(message==WM_COMMAND && HIWORD(wp)==CBN_SELCHANGE){auto i=unsigned(LOWORD(wp)-500);if(i<parameterCount && self->menus[i]){const auto& p=parameters[i];auto n=SendMessageW(self->menus[i],CB_GETCURSEL,0,0);if(n>=0)writeOne(self->services,p.id,double(n)/p.stepCount);self->refresh({},{});}return 0;}
        if(hwnd==self->controlHost && (message==WM_VSCROLL || message==WM_MOUSEWHEEL)){
            if(!self->advanced)return 0;int next=self->offset;RECT r;GetClientRect(hwnd,&r);
            if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;
            else switch(LOWORD(wp)){case SB_LINEUP:next-=30;break;case SB_LINEDOWN:next+=30;break;case SB_PAGEUP:next-=r.bottom;break;case SB_PAGEDOWN:next+=r.bottom;break;case SB_THUMBTRACK:{SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_TRACKPOS;GetScrollInfo(hwnd,SB_VERT,&si);next=si.nTrackPos;break;}}
            self->offset=std::clamp(next,0,std::max(0,self->documentHeight-r.bottom));self->layout();return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    bool attach(void* parent,const EditorServices& s) override {
        if(window || !parent || !s.readTarget)return false;services=s;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        if(!instances){WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=className;c.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!RegisterClassW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;}
        ++instances;window=CreateWindowExW(0,className,L"Gate",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        chart=CreateWindowExW(0,className,L"Measured Gate IO peaks and applied gain; threshold is a setting, not the SC detector",WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,module,this);
        controlHost=CreateWindowExW(0,className,L"Gate controls",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,window,nullptr,module,this);
        for(auto id:simpleIDs){auto c=RotaryControl::create(controlHost,s,spec(id),displayPolicy(id));if(!c)return false;primary.push_back(std::move(c));}
        for(auto id:additionalIDs){auto i=registry.index(id);const auto& p=parameters[i];if(p.enumLabels){labels[i]=CreateWindowExW(0,L"STATIC",wide(p.title).c_str(),WS_CHILD|SS_CENTER,0,0,1,1,controlHost,nullptr,module,nullptr);menus[i]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,1,1,controlHost,reinterpret_cast<HMENU>(500+i),module,nullptr);
                for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(menus[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.enumLabels[n]).c_str()));SendMessageW(labels[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(menus[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            }else{auto c=RotaryControl::create(controlHost,s,p,displayPolicy(id));if(!c)return false;extra.emplace_back(id,std::move(c));}}
        notice=CreateWindowExW(0,L"STATIC",L"RMS: 5 ms / Stereo Link: 100%\nLookahead saved target only; actual maximum/effective/PDC 0 ms.\nSC Listen unavailable. Raw IO peaks are not the SC detector.",WS_CHILD,0,0,1,1,controlHost,nullptr,module,nullptr);SendMessageW(notice,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        refresh({},{});layout();return true;
    }
    void resize(int w,int h) override{width=w;height=h;if(window){MoveWindow(window,0,0,w,h,TRUE);layout();}}
    void refresh(const EditorViewState&,const StatusSnapshot&) override {
        bool next=services.view && services.view->advanced;if(next!=advanced){advanced=next;offset=0;layout();}
        for(unsigned i=0;i<primary.size();++i)primary[i]->refresh(availability(simpleIDs[i],services).enabled);
        for(auto& c:extra)c.second->refresh(availability(c.first,services).enabled);
        for(auto id:additionalIDs){auto i=registry.index(id);if(menus[i]){SetWindowTextW(labels[i],wide(uiText(services,chineseLabel(id),parameters[i].title)).c_str());EnableWindow(menus[i],availability(id,services).enabled);SendMessageW(menus[i],CB_SETCURSEL,WPARAM(std::round(read(services,id)*parameters[i].stepCount)),0);}}
        history.refresh(services);if(chart)InvalidateRect(chart,nullptr,TRUE);
    }
};
}
EditorContent* createEditorContent(){return new WindowsContent;}
}
