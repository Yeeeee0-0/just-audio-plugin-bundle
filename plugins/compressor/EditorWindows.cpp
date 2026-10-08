#include "Engine.hpp"
#include <windows.h>
#include <array>
#include <string>
namespace just::compressor {
namespace {
std::wstring wide(const char* text) {
    const int size=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),size);return result;
}
class WindowsEditor final:public EditorContent {
    HWND body=nullptr,caption=nullptr;
    std::array<HWND,20> labels{},bars{},values{};
    EditorServices services{};HMODULE image=nullptr;
    int width=700,height=300,scroll=0;bool advanced=false;
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProc(w,m,wp,lp);
        if(m==WM_HSCROLL) {
            HWND control=reinterpret_cast<HWND>(lp);
            for(std::size_t i=0;i<self->bars.size();++i)if(control==self->bars[i]) {
                const ParamID id=module::parameters[i+1].id;if(id==119)return 0;
                int position=GetScrollPos(control,SB_CTL);
                switch(LOWORD(wp)) {
                    case SB_LINELEFT:position-=100;break;case SB_LINERIGHT:position+=100;break;
                    case SB_PAGELEFT:position-=1000;break;case SB_PAGERIGHT:position+=1000;break;
                    case SB_THUMBTRACK:case SB_THUMBPOSITION:position=HIWORD(wp);break;
                    default:return 0;
                }
                double value=std::clamp(position,0,10000)/10000.0;
                if(module::parameters[i+1].stepCount)value=std::round(value*module::parameters[i+1].stepCount)/module::parameters[i+1].stepCount;
                if(self->services.beginEdit(self->services.owner,id)) {
                    self->services.performEdit(self->services.owner,id,value);self->services.endEdit(self->services.owner,id);
                }
                self->refresh(*self->services.view,{});return 0;
            }
        }
        if(m==WM_VSCROLL && self->advanced) {
            const int code=LOWORD(wp);if(code==SB_LINEUP)self->scroll-=31;else if(code==SB_LINEDOWN)self->scroll+=31;
            else if(code==SB_PAGEUP)self->scroll-=180;else if(code==SB_PAGEDOWN)self->scroll+=180;
            else if(code==SB_THUMBTRACK || code==SB_THUMBPOSITION)self->scroll=HIWORD(wp);
            self->scroll=std::clamp(self->scroll,0,std::max(0,700-self->height));self->layout();return 0;
        }
        if(m==WM_PAINT) {
            PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT bounds;GetClientRect(w,&bounds);FillRect(dc,&bounds,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            HBRUSH fill=CreateSolidBrush(RGB(12,110,125));HPEN outline=CreatePen(PS_SOLID,1,RGB(12,110,125));
            auto oldBrush=SelectObject(dc,fill);auto oldPen=SelectObject(dc,outline);RoundRect(dc,14,12,62,60,12,12);
            HPEN wave=CreatePen(PS_SOLID,3,RGB(174,245,250));SelectObject(dc,wave);
            // Temporary native interpretation of the inspected Compressor waveform tile.
            POINT points[]={{18,39},{25,32},{32,37},{39,42},{47,36},{55,36}};Polyline(dc,points,6);
            SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(fill);DeleteObject(outline);DeleteObject(wave);EndPaint(w,&ps);return 0;
        }
        return DefWindowProc(w,m,wp,lp);
    }
    HWND child(const wchar_t* type,const wchar_t* text,DWORD style,int id) {
        HWND h=CreateWindowEx(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,body,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),image,nullptr);
        SendMessage(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;
    }
    void layout() {
        MoveWindow(caption,80,14,std::max(1,width-95),advanced?55:45,TRUE);
        SetScrollRange(body,SB_VERT,0,std::max(0,700-height),FALSE);SetScrollPos(body,SB_VERT,scroll,TRUE);ShowScrollBar(body,SB_VERT,advanced);
        for(std::size_t i=0;i<bars.size();++i) {
            const bool visible=advanced || i<4;ShowWindow(labels[i],visible?SW_SHOW:SW_HIDE);ShowWindow(bars[i],visible?SW_SHOW:SW_HIDE);ShowWindow(values[i],visible?SW_SHOW:SW_HIDE);
            if(advanced) {
                int y=85+static_cast<int>(i)*30-scroll;bool inside=y>=80 && y+25<height;
                for(HWND h:{labels[i],bars[i],values[i]})ShowWindow(h,inside?SW_SHOW:SW_HIDE);
                MoveWindow(labels[i],14,y,width*36/100,24,TRUE);MoveWindow(bars[i],width*39/100,y,width*32/100,20,TRUE);MoveWindow(values[i],width*74/100,y,width*22/100,24,TRUE);
            } else if(i<4) {
                int cell=(width-24)/4,x=12+cell*static_cast<int>(i),y=std::max(85,height/2);
                MoveWindow(labels[i],x,y,cell-10,24,TRUE);MoveWindow(bars[i],x+10,y+32,std::max(1,cell-28),20,TRUE);MoveWindow(values[i],x,y+65,cell-10,24,TRUE);
            }
        }
    }
public:
    ~WindowsEditor() override {if(body)DestroyWindow(body);}
    bool attach(void* parent,const EditorServices& s) override {
        if(!parent || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&image);
        WNDCLASS wc{};wc.lpfnWndProc=procedure;wc.hInstance=image;wc.lpszClassName=L"JUST.Compressor.Content";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClass(&wc);
        body=CreateWindowEx(0,wc.lpszClassName,L"Just Compressor",WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,width,height,static_cast<HWND>(parent),nullptr,image,this);
        if(!body)return false;caption=child(L"STATIC",L"GR —\r\nGain reduction display unavailable in this build",SS_CENTER,0);
        for(std::size_t i=0;i<bars.size();++i) {
            const auto& spec=module::parameters[i+1];labels[i]=child(L"STATIC",wide(spec.title).c_str(),SS_CENTER,0);
            bars[i]=child(L"SCROLLBAR",L"",SBS_HORZ,static_cast<int>(spec.id));SetScrollRange(bars[i],SB_CTL,0,10000,FALSE);EnableWindow(bars[i],spec.id!=119);
            values[i]=child(L"STATIC",L"",SS_CENTER,0);
        }
        refresh(*s.view,{});return true;
    }
    void resize(int w,int h) override {width=std::max(1,w);height=std::max(1,h);MoveWindow(body,0,0,width,height,TRUE);layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {
        advanced=view.advanced;
        SetWindowText(caption,advanced?L"GR unavailable · actual PDC 0 samples\r\nMaximum Lookahead / SC Listen controls unavailable":L"GR —\r\nGain reduction display unavailable in this build");
        for(std::size_t i=0;i<bars.size();++i) {
            const auto& spec=module::parameters[i+1];const double target=services.readTarget(services.owner,spec.id);
            SetScrollPos(bars[i],SB_CTL,static_cast<int>(std::round(target*10000)),TRUE);
            char text[128];spec.format(target,text,sizeof(text));std::string formatted=text;
            if(*spec.unit && !spec.stepCount){formatted+=spec.id==101?"":" ";formatted+=spec.unit;}
            SetWindowText(values[i],wide(formatted.c_str()).c_str());
            if(spec.id==119)SetWindowText(labels[i],L"Lookahead request (not applied)");
        }
        layout();
    }
};
}
EditorContent* createCompressorEditor(){return new WindowsEditor;}
}
