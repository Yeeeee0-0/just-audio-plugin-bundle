#include "Controls.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
namespace just {
namespace {
std::wstring controlWide(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);return w;}
class WinRotary final:public RotaryControl {
    HWND window=nullptr,label=nullptr,value=nullptr;HMODULE module=nullptr;
    EditorServices services;ParameterSpec spec;DisplayPolicy policy;TextEditSession edit;
    double normalized=0;RotaryDrag drag;int previousY=0;bool dragging=false,typing=false,enabled=true,cancelled=false;
    void end(){if(dragging){services.endEdit(services.owner,spec.id);dragging=false;}}
    void apply(double n){n=std::clamp(n,0.,1.);if(spec.stepCount)n=std::round(n*spec.stepCount)/spec.stepCount;
        if(n!=normalized && services.performEdit(services.owner,spec.id,n)){double actual=services.readTarget(services.owner,spec.id);normalized=std::isfinite(actual)?std::clamp(actual,0.,1.):n;if(std::abs(normalized-n)>1e-12)drag.accumulator=normalized;refresh(enabled);}}
    void once(double n){if(dragging){if(enabled)apply(n);return;}if(!enabled || n==normalized || !services.beginEdit(services.owner,spec.id))return;dragging=true;apply(n);end();}
    static LRESULT CALLBACK editProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){auto* p=reinterpret_cast<WinRotary*>(data);
        if(m==WM_KEYDOWN && (w==VK_RETURN || w==VK_ESCAPE)){p->cancelled=w==VK_ESCAPE;SetFocus(p->window);return 0;}return DefSubclassProc(h,m,w,l);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* p=reinterpret_cast<WinRotary*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<WinRotary*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(!p)return DefWindowProcW(h,m,w,l);
        if(m==WM_LBUTTONDOWN && p->enabled && GET_Y_LPARAM(l)>=22 && GET_Y_LPARAM(l)<108){p->end();SetFocus(h);SetCapture(h);p->previousY=GET_Y_LPARAM(l);p->drag.begin(p->normalized,p->previousY);p->dragging=p->services.beginEdit(p->services.owner,p->spec.id);return 0;}
        if(m==WM_MOUSEMOVE && p->dragging){int y=GET_Y_LPARAM(l);p->apply(p->drag.move(y,(GetKeyState(VK_SHIFT)&0x8000)!=0));p->previousY=y;return 0;}
        if(m==WM_LBUTTONUP){p->end();if(GetCapture()==h)ReleaseCapture();return 0;}
        if(m==WM_CAPTURECHANGED || m==WM_KILLFOCUS || m==WM_CANCELMODE)p->end();
        if(m==WM_RBUTTONDOWN){p->once(p->spec.toNormalized(p->spec.initial));return 0;}
        if(m==WM_MOUSEWHEEL && GetFocus()==h){double step=p->spec.stepCount?1./p->spec.stepCount:((GetKeyState(VK_SHIFT)&0x8000)?.001:.01);p->once(std::clamp(p->normalized+GET_WHEEL_DELTA_WPARAM(w)/double(WHEEL_DELTA)*step,0.,1.));return 0;}
        if(m==WM_KEYDOWN && (w==VK_LEFT || w==VK_RIGHT || w==VK_UP || w==VK_DOWN)){double step=p->spec.stepCount?1./p->spec.stepCount:((GetKeyState(VK_SHIFT)&0x8000)?.001:.01);p->once(std::clamp(p->normalized+((w==VK_RIGHT || w==VK_UP)?step:-step),0.,1.));return 0;}
        if(m==WM_COMMAND && reinterpret_cast<HWND>(l)==p->value){
            if(HIWORD(w)==EN_SETFOCUS){p->typing=true;p->cancelled=false;auto text=p->edit.begin(p->spec,p->policy,p->normalized);SetWindowTextW(p->value,controlWide(text.c_str()).c_str());}
            if(HIWORD(w)==EN_KILLFOCUS){wchar_t wide[128]{};char text[512]{};GetWindowTextW(p->value,wide,128);WideCharToMultiByte(CP_UTF8,0,wide,-1,text,sizeof(text),nullptr,nullptr);double next=0;
                bool changed=!p->cancelled && p->edit.changed(p->spec,p->policy,text,next);p->typing=false;if(changed)p->once(next);p->refresh(p->enabled);}return 0;
        }
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT rect;GetClientRect(h,&rect);FillRect(dc,&rect,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));int x=(rect.right-84)/2,y=23;
            HBRUSH b=CreateSolidBrush(RGB(225,225,225));auto oldB=SelectObject(dc,b);Ellipse(dc,x,y,x+84,y+84);SelectObject(dc,oldB);DeleteObject(b);
            HPEN pen=CreatePen(PS_SOLID,4,p->enabled?RGB(23,143,153):RGB(160,160,160));auto oldP=SelectObject(dc,pen);double a=rotaryAngle(p->normalized)*3.141592653589793/180.;
            MoveToEx(dc,x+42+int(std::sin(a)*20),y+42-int(std::cos(a)*20),nullptr);LineTo(dc,x+42+int(std::sin(a)*34),y+42-int(std::cos(a)*34));SelectObject(dc,oldP);DeleteObject(pen);EndPaint(h,&ps);return 0;
        }return DefWindowProcW(h,m,w,l);
    }
public:
    WinRotary(HWND parent,const EditorServices& s,const ParameterSpec& p,DisplayPolicy d):services(s),spec(p),policy(d){
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=L"JUST.Shared.Rotary.v2";c.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&c);
        window=CreateWindowExW(0,c.lpszClassName,controlWide(spec.title).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,112,136,parent,nullptr,module,this);
        label=CreateWindowExW(0,L"STATIC",controlWide(spec.title).c_str(),WS_CHILD|WS_VISIBLE|SS_CENTER,0,0,112,22,window,nullptr,module,nullptr);
        value=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_CENTER|ES_AUTOHSCROLL,0,108,112,24,window,nullptr,module,nullptr);
        SetWindowSubclass(value,editProc,1,reinterpret_cast<DWORD_PTR>(this));SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);SendMessageW(value,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);refresh(true);
    }
    ~WinRotary() override{end();RemoveWindowSubclass(value,editProc,1);DestroyWindow(window);}
    void resize(int x,int y,int width,int height) override{MoveWindow(window,x,y,std::max(width,112),std::max(height,136),TRUE);MoveWindow(label,0,0,std::max(width,112),22,TRUE);MoveWindow(value,0,108,std::max(width,112),24,TRUE);}
    void refresh(bool e) override{enabled=e;if(!e){end();if(GetCapture()==window)ReleaseCapture();}if(!dragging)normalized=services.readTarget(services.owner,spec.id);if(!typing){char text[128];formatDisplay(spec,normalized,services.view && services.view->advanced?DisplayContext::advanced:DisplayContext::simple,policy,text,sizeof(text));SetWindowTextW(value,controlWide(text).c_str());}EnableWindow(value,e);InvalidateRect(window,nullptr,FALSE);}
    void* nativeHandle() const noexcept override{return window;}
};
}
std::unique_ptr<RotaryControl> RotaryControl::create(void* parent,const EditorServices& services,const ParameterSpec& spec,DisplayPolicy policy){
    if(!parent || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return {};return std::make_unique<WinRotary>(static_cast<HWND>(parent),services,spec,policy);
}
}
