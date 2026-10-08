#pragma once
// Windows-only native drawing. No third-party UI runtime; all dimensions are
// logical editor points, matching the accepted macOS layouts.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include "VisualAssets.hpp"
namespace just::win {
inline std::wstring wide(const char* s){if(!s || !*s)return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,nullptr,0);if(n<=1)return {};std::wstring out(std::size_t(n),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,out.data(),n);out.resize(std::size_t(n-1));return out;}
inline std::wstring wide(const std::string& s){return wide(s.c_str());}
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring windowText(HWND h){int n=GetWindowTextLengthW(h);std::wstring out(std::size_t(n+1),L'\0');GetWindowTextW(h,out.data(),n+1);out.resize(std::size_t(n));return out;}
inline constexpr const wchar_t* viewProperty=L"JUST.Editor.View.v1";
inline EditorViewState* view(HWND h){for(;h;h=GetParent(h))if(auto p=GetPropW(h,viewProperty))return reinterpret_cast<EditorViewState*>(p);return nullptr;}
inline double scale(HWND h){auto v=view(h);return v?std::clamp(v->renderScale,.75,1.5):1.;}
inline bool paused(HWND h){auto v=view(h);return v && v->visualsPaused;}
inline void place(HWND h,double x,double y,double w,double height){double s=scale(h);MoveWindow(h,int(std::lround(x*s)),int(std::lround(y*s)),std::max(1,int(std::lround(w*s))),std::max(1,int(std::lround(height*s))),TRUE);}
inline Gdiplus::PointF point(HWND h,LPARAM l){double s=scale(h);return {float(GET_X_LPARAM(l)/s),float(GET_Y_LPARAM(l)/s)};}
inline Gdiplus::SizeF size(HWND h){RECT r{};GetClientRect(h,&r);double s=scale(h);return {float(r.right/s),float(r.bottom/s)};}
inline Gdiplus::Color color(unsigned rgb,BYTE alpha=255){return {alpha,BYTE(rgb>>16),BYTE(rgb>>8),BYTE(rgb)};}
// Startup/shutdown occurs in normal UI calls, never a static destructor under
// DLL loader lock. A root Session keeps images alive; each Paint also leases
// the runtime so independently hosted controls are safe.
struct Runtime {ULONG_PTR token=0;unsigned users=0;std::mutex mutex;};
inline Runtime& runtime(){static Runtime r;return r;}
inline void startup(){auto& r=runtime();std::lock_guard<std::mutex> lock(r.mutex);if(!r.token){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&r.token,&input,nullptr);}}
class Session {
public:
    Session(){auto& r=runtime();std::lock_guard<std::mutex> lock(r.mutex);if(!r.token){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&r.token,&input,nullptr);}++r.users;}
    ~Session(){auto& r=runtime();std::lock_guard<std::mutex> lock(r.mutex);if(r.users && --r.users==0 && r.token){Gdiplus::GdiplusShutdown(r.token);r.token=0;}}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
};
// Owns BeginPaint/EndPaint and a double buffer. Converts only this window's
// finished pixels to grayscale while bypassed. Child controls use their own
// Paint; interaction stays live and no input-blocking screenshot is introduced.
class Paint {
    Session session;HWND h;PAINTSTRUCT ps{};HDC target=nullptr,memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void* pixels=nullptr;int pixelWidth=0,pixelHeight=0,memoryState=0;bool gray,printing=false,buffered=false;std::unique_ptr<Gdiplus::Graphics> drawing;
public:
    explicit Paint(HWND window,bool monochrome=true,HDC printDC=nullptr):h(window),gray(monochrome && paused(window)),printing(printDC!=nullptr){
        target=printing?printDC:BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);pixelWidth=std::max(1,int(r.right));pixelHeight=std::max(1,int(r.bottom));memory=CreateCompatibleDC(target);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=pixelWidth;info.bmiHeader.biHeight=-pixelHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        if(memory)bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(memory && bitmap && pixels){old=SelectObject(memory,bitmap);buffered=old && old!=HGDI_ERROR;}
        if(!buffered){if(bitmap)DeleteObject(bitmap);bitmap=nullptr;pixels=nullptr;if(memory)DeleteDC(memory);memory=target;}
        memoryState=memory?SaveDC(memory):0;drawing=std::make_unique<Gdiplus::Graphics>(memory);drawing->Clear(color(0xf5fafb));drawing->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);drawing->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);drawing->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);drawing->ScaleTransform(float(scale(h)),float(scale(h)));
    }
    ~Paint(){drawing->Flush(Gdiplus::FlushIntentionSync);drawing.reset();if(memoryState)RestoreDC(memory,memoryState);if(buffered){if(gray && pixels){auto p=static_cast<unsigned char*>(pixels);for(std::size_t n=0;n<std::size_t(pixelWidth)*pixelHeight;++n,p+=4){auto v=BYTE((unsigned(p[2])*54+unsigned(p[1])*183+unsigned(p[0])*19)/256);p[0]=p[1]=p[2]=v;}}BitBlt(target,0,0,pixelWidth,pixelHeight,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);}if(!printing)EndPaint(h,&ps);}
    Paint(const Paint&)=delete;Paint& operator=(const Paint&)=delete;
    Gdiplus::Graphics& graphics(){return *drawing;}
    HDC dc(){drawing->Flush(Gdiplus::FlushIntentionSync);return memory;}
    float width()const{return float(pixelWidth/scale(h));}float height()const{return float(pixelHeight/scale(h));}
};
inline void roundedPath(Gdiplus::GraphicsPath& p,Gdiplus::RectF r,float radius){float d=std::max(0.f,std::min(radius*2,std::min(r.Width,r.Height)));if(d<=0){p.AddRectangle(r);return;}p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();}
inline void fill(Gdiplus::Graphics& g,Gdiplus::RectF r,unsigned rgb,float radius=0,BYTE alpha=255){Gdiplus::SolidBrush b(color(rgb,alpha));if(radius>0){Gdiplus::GraphicsPath p;roundedPath(p,r,radius);g.FillPath(&b,&p);}else g.FillRectangle(&b,r);}
inline void stroke(Gdiplus::Graphics& g,Gdiplus::RectF r,unsigned rgb,float radius=0,float width=1){Gdiplus::Pen pen(color(rgb),width);if(radius>0){Gdiplus::GraphicsPath p;roundedPath(p,r,radius);g.DrawPath(&pen,&p);}else g.DrawRectangle(&pen,r);}
inline void line(Gdiplus::Graphics& g,float x1,float y1,float x2,float y2,unsigned rgb,float width=1){Gdiplus::Pen p(color(rgb),width);p.SetStartCap(Gdiplus::LineCapRound);p.SetEndCap(Gdiplus::LineCapRound);g.DrawLine(&p,x1,y1,x2,y2);}
inline void text(Gdiplus::Graphics& g,const std::wstring& s,Gdiplus::RectF r,float points=12,unsigned rgb=0x17333c,bool bold=false,Gdiplus::StringAlignment align=Gdiplus::StringAlignmentNear){Gdiplus::Font font(L"Segoe UI",points,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush b(color(rgb));Gdiplus::StringFormat f;f.SetAlignment(align);f.SetLineAlignment(Gdiplus::StringAlignmentCenter);f.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);g.DrawString(s.c_str(),int(s.size()),&font,r,&f,&b);}
inline void text(Gdiplus::Graphics& g,const char* s,Gdiplus::RectF r,float points=12,unsigned rgb=0x17333c,bool bold=false,Gdiplus::StringAlignment align=Gdiplus::StringAlignmentNear){text(g,wide(s),r,points,rgb,bold,align);}
// A successful lease owns one reference to a class registered by this DLL.
// Callers destroy every HWND before releasing their final lease. Existing
// unrelated classes are never adopted, and failed acquires never decrement.
class WindowClass {
    struct Entry {HINSTANCE module=nullptr;std::wstring name;WNDPROC proc=nullptr;unsigned users=0;};
    struct Registry {std::mutex mutex;std::vector<Entry> entries;};
    static Registry& registry(){static Registry value;return value;}
    HINSTANCE module=nullptr;std::wstring name;bool acquired=false;
public:
    WindowClass()=default;
    WindowClass(const WindowClass&)=delete;WindowClass& operator=(const WindowClass&)=delete;
    ~WindowClass(){reset();}
    bool acquire(const WNDCLASSW& definition){
        if(acquired || !definition.hInstance || !definition.lpfnWndProc || !definition.lpszClassName)return false;
        name=definition.lpszClassName;module=definition.hInstance;
        auto& r=registry();std::lock_guard<std::mutex> guard(r.mutex);
        for(auto& entry:r.entries)if(entry.module==module && entry.name==name){
            if(entry.proc!=definition.lpfnWndProc)return false;
            ++entry.users;acquired=true;return true;
        }
        // Allocate bookkeeping before RegisterClass so an allocation failure
        // cannot strand a registered class without an owner.
        r.entries.push_back({module,name,definition.lpfnWndProc,0});
        if(!RegisterClassW(&definition)){r.entries.pop_back();return false;}
        r.entries.back().users=1;acquired=true;return true;
    }
    void reset(){
        if(!acquired)return;
        auto& r=registry();std::lock_guard<std::mutex> guard(r.mutex);
        for(auto it=r.entries.begin();it!=r.entries.end();++it)if(it->module==module && it->name==name){
            if(it->users)--it->users;
            if(!it->users && UnregisterClassW(it->name.c_str(),it->module))r.entries.erase(it);
            // If Win32 reports a still-live HWND, retain the owned zero-user
            // record; a later lease can retry without adopting an alien class.
            break;
        }
        acquired=false;
    }
    explicit operator bool()const noexcept{return acquired;}
};
inline HMODULE moduleAt(const void* address){HMODULE m=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&m);return m;}
inline std::filesystem::path resourceRoot(HMODULE module){std::wstring p(32768,L'\0');DWORD n=GetModuleFileNameW(module,p.data(),DWORD(p.size()));if(!n || n>=p.size())return {};p.resize(n);auto folder=std::filesystem::path(p).parent_path();return folder.parent_path()/L"Resources"/L"JustUI";}
inline std::unique_ptr<Gdiplus::Image> icon(HMODULE module,const char* slug){startup();if(!slug || !safeAssetRelativePath(slug))return {};auto path=resourceRoot(module)/L"icons"/(wide(slug)+L".png");std::error_code e;auto bytes=std::filesystem::file_size(path,e);if(e || bytes>8*1024*1024)return {};auto image=std::make_unique<Gdiplus::Image>(path.c_str());if(image->GetLastStatus()!=Gdiplus::Ok || image->GetWidth()>4096 || image->GetHeight()>4096)return {};return image;}
} // namespace just::win
