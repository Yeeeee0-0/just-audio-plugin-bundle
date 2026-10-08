#include "Controls.hpp"
#include "VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <objbase.h>
// oleacc.h supplies these annotation GUIDs via DEFINE_GUID. Instantiate them
// here, before UIAutomation.h can include oleacc.h with declarations only.
#include <initguid.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <atomic>
#include <cstdio>
#include <new>
namespace just {
namespace {
// Custom HWND provider: UIA calls may arrive on a non-UI thread. Every read
// and write is marshalled synchronously to the owning window; no EditorServices
// callback or WinRotary pointer escapes to an accessibility client.
constexpr UINT rotaryAccessRequest=WM_APP+0x4a1,rotaryAccessNotify=WM_APP+0x4a2;
class RotaryProvider;
struct RotaryAccessState {
    std::wstring name,text,help,id;
    double value=0,minimum=0,maximum=1,smallChange=.01,largeChange=.1;
    bool enabled=false,focused=false;
    OrientationType orientation=OrientationType_None;
};
struct RotaryAccessRequest {
    enum Kind {read,setNumber,setText} kind=read;
    RotaryProvider* provider=nullptr;
    RotaryAccessState state;
    double number=0;
    const wchar_t* text=nullptr;
    HRESULT result=UIA_E_ELEMENTNOTAVAILABLE;
};
class RotaryProvider final:public IRawElementProviderSimple,public IRangeValueProvider,public IValueProvider {
    std::atomic<ULONG> references{1};
    std::atomic<HWND> window;
    const HWND identityWindow;
    HMODULE codeModule=nullptr;
    HRESULT request(RotaryAccessRequest& r) {
        auto h=window.load(std::memory_order_acquire);if(!h)return UIA_E_ELEMENTNOTAVAILABLE;
        r.provider=this;
        // Do not use SendMessageTimeout with a stack-owned request: a timed-out
        // receiver could still access it after the caller has returned.
        SendMessageW(h,rotaryAccessRequest,0,reinterpret_cast<LPARAM>(&r));return r.result;
    }
    HRESULT read(RotaryAccessState& state){RotaryAccessRequest r;auto hr=request(r);if(SUCCEEDED(hr))state=std::move(r.state);return hr;}
    HRESULT numeric(double* out,double RotaryAccessState::*member){if(!out)return E_POINTER;*out=0;RotaryAccessState s;auto hr=read(s);if(SUCCEEDED(hr))*out=s.*member;return hr;}
    static HRESULT string(VARIANT* out,const std::wstring& text){out->vt=VT_BSTR;out->bstrVal=SysAllocString(text.c_str());return out->bstrVal?S_OK:E_OUTOFMEMORY;}
    void diagnostic(const char* operation,HRESULT result)const {
        wchar_t enabled[2]{};
        if(GetEnvironmentVariableW(L"JUST_UIA_DIAGNOSTICS",enabled,2)!=1 || enabled[0]!=L'1')return;
        std::fprintf(stderr,"JUST UIA %s hwnd=%p thread=%lu HRESULT=0x%08lX\n",operation,
            static_cast<void*>(identityWindow),GetCurrentThreadId(),static_cast<unsigned long>(result));
        std::fflush(stderr);
    }
    static void CALLBACK disconnect(PTP_CALLBACK_INSTANCE instance,void* context) {
        auto* p=static_cast<RotaryProvider*>(context);auto module=p->codeModule;
        const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        p->diagnostic("CoInitializeEx(MTA)",hr);
        const auto disconnected=SUCCEEDED(hr)?UiaDisconnectProvider(p):hr;
        p->diagnostic("UiaDisconnectProvider",disconnected);
        if(SUCCEEDED(hr))CoUninitialize();
        // A failed disconnect must not unmap code while UIA still holds it.
        if(FAILED(disconnected))return;
        p->Release();
        // The VST3 may already have been unloaded by its host. Keep its provider
        // code mapped until this callback has returned, never FreeLibrary in it.
        FreeLibraryWhenCallbackReturns(instance,module);
    }
    RotaryProvider(HWND h,HMODULE module):window(h),identityWindow(h),codeModule(module){}
public:
    static RotaryProvider* create(HWND h) {
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&disconnect),&module))return nullptr;
        auto* p=new(std::nothrow) RotaryProvider(h,module);if(!p)FreeLibrary(module);return p;
    }
    void invalidate(){window.store(nullptr,std::memory_order_release);}
    void dispose() {
        invalidate();
        // UiaDisconnectProvider forbids outbound COM in an input-synchronous
        // SendMessage handler. A module-pinned worker also covers parent-led
        // HWND destruction and a host unloading the VST3 immediately afterward.
        if(!TrySubmitThreadpoolCallback(disconnect,this,nullptr)) {
            diagnostic("TrySubmitThreadpoolCallback",HRESULT_FROM_WIN32(GetLastError()));
            // On resource exhaustion retain this inert provider/module rather
            // than leave a UIA client pointing into unmapped plugin code.
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRangeValueProvider))*out=static_cast<IRangeValueProvider*>(this);
        else if(iid==__uuidof(IValueProvider))*out=static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override{if(!out)return E_POINTER;*out=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(!window.load())return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_RangeValuePatternId)*out=static_cast<IRangeValueProvider*>(this);
        else if(id==UIA_ValuePatternId)*out=static_cast<IValueProvider*>(this);
        if(*out)AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* out) override {
        if(!out)return E_POINTER;VariantInit(out);RotaryAccessState s;auto hr=read(s);if(FAILED(hr))return hr;
        switch(id){
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=UIA_SliderControlTypeId;break;
        case UIA_NamePropertyId:return string(out,s.name);
        case UIA_AutomationIdPropertyId:return string(out,s.id);
        case UIA_HelpTextPropertyId:return string(out,s.help);
        case UIA_FrameworkIdPropertyId:return string(out,L"Win32");
        case UIA_IsControlElementPropertyId:case UIA_IsContentElementPropertyId:out->vt=VT_BOOL;out->boolVal=VARIANT_TRUE;break;
        case UIA_IsEnabledPropertyId:case UIA_IsKeyboardFocusablePropertyId:out->vt=VT_BOOL;out->boolVal=s.enabled?VARIANT_TRUE:VARIANT_FALSE;break;
        case UIA_HasKeyboardFocusPropertyId:out->vt=VT_BOOL;out->boolVal=s.focused?VARIANT_TRUE:VARIANT_FALSE;break;
        case UIA_OrientationPropertyId:out->vt=VT_I4;out->lVal=s.orientation;break;
        }return S_OK; // VT_EMPTY delegates geometry/native-window properties to the HWND host.
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        // This identifies the UIA Runtime ID even after native destruction.
        // Microsoft's UIAutomationCleanShutdown sample explicitly preserves the
        // original HWND here: checking its validity (or clearing it) prevents
        // UiaDisconnectProvider from finding the old client proxies. Never use
        // this identity handle to dispatch a control read/write; request() uses
        // the separately invalidated live window and verifies provider identity.
        return UiaHostProviderFromHwnd(identityWindow,out);
    }
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {if(!std::isfinite(value))return E_INVALIDARG;RotaryAccessRequest r;r.kind=RotaryAccessRequest::setNumber;r.number=value;return request(r);}
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {if(!value)return E_INVALIDARG;std::size_t length=0;while(length<256 && value[length])++length;if(length==256)return E_INVALIDARG;RotaryAccessRequest r;r.kind=RotaryAccessRequest::setText;r.text=value;return request(r);}
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override{return numeric(out,&RotaryAccessState::value);}
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override{return numeric(out,&RotaryAccessState::minimum);}
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override{return numeric(out,&RotaryAccessState::maximum);}
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override{return numeric(out,&RotaryAccessState::smallChange);}
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override{return numeric(out,&RotaryAccessState::largeChange);}
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override{if(!out)return E_POINTER;*out=TRUE;RotaryAccessState s;auto hr=read(s);if(SUCCEEDED(hr))*out=!s.enabled;return hr;}
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override{if(!out)return E_POINTER;*out=nullptr;RotaryAccessState s;auto hr=read(s);if(FAILED(hr))return hr;*out=SysAllocString(s.text.c_str());return *out?S_OK:E_OUTOFMEMORY;}
};
class WinRotary final:public RotaryControl {
    win::WindowClass windowClass;
    HWND window=nullptr,value=nullptr,unitTooltip=nullptr;std::wstring unitHelp;HMODULE module=nullptr;HFONT valueFont=nullptr;HBRUSH background=nullptr;
    EditorServices services;ParameterSpec spec;DisplayPolicy policy;TextEditSession edit;RotaryDrag drag;
    int fontPixels=0;double normalized=0;bool ready=false;bool dragging=false,typing=false,enabled=true,cancelled=false;
    RotaryProvider* automation=nullptr;RotaryAccessState lastAccess;bool accessNotifyPending=false;
    IAccPropServices* accessibleProperties=nullptr;bool initializedCom=false;std::wstring accessibleValueName;
    bool interactive()const{if(!enabled || !window)return false;for(auto h=window;h;h=GetParent(h))if(!IsWindowEnabled(h))return false;return true;}
    const char* label()const{return services.view && services.view->language==UiLanguage::chinese && policy.labelZh?policy.labelZh:spec.title;}
    double smallStep()const{return spec.stepCount?1./spec.stepCount:.01;}
    double largeStep()const{return spec.stepCount?std::min(1.,10./spec.stepCount):.1;}
    RotaryAccessState accessState()const {
        RotaryAccessState s;s.name=win::wide(label());s.value=spec.toPhysical(normalized);s.minimum=spec.minimum;s.maximum=spec.maximum;
        auto delta=[&](double step){auto other=normalized+step<=1?normalized+step:std::max(0.,normalized-step);return std::abs(spec.toPhysical(other)-s.value);};
        s.smallChange=delta(smallStep());s.largeChange=delta(largeStep());s.enabled=interactive();s.focused=GetFocus()==window;
        s.orientation=policy.style==ControlStyle::horizontal?OrientationType_Horizontal:policy.style==ControlStyle::vertical?OrientationType_Vertical:OrientationType_None;
        s.id=L"JUST.Rotary."+win::wide(spec.stableKey)+L"."+std::to_wstring(reinterpret_cast<UINT_PTR>(window));
        // RangeValue uses the real ParameterSpec physical units. Value preserves
        // enum/custom display semantics without exposing a synthetic ParamID.
        char text[128];formatDisplay(spec,normalized,services.view && services.view->advanced?DisplayContext::advanced:DisplayContext::simple,policy,text,sizeof(text));
        s.text=win::wide(text);if(*spec.unit && !(spec.enumLabels && spec.stepCount)){s.text+=L" ";s.text+=win::wide(spec.unit);}
        s.help=win::wide(spec.unit);return s;
    }
    void annotateValue(){if(!accessibleProperties || !value)return;auto name=win::wide(label())+(services.view && services.view->language==UiLanguage::chinese?L" 数值":L" value");if(name==accessibleValueName)return;
        if(SUCCEEDED(accessibleProperties->SetHwndPropStr(value,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,name.c_str()))){accessibleValueName=std::move(name);NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,value,OBJID_CLIENT,CHILDID_SELF);}}
    void clearValueAnnotation(HWND h){if(accessibleProperties && h){const MSAAPROPID ids[]{PROPID_ACC_NAME};accessibleProperties->ClearHwndProps(h,OBJID_CLIENT,CHILDID_SELF,ids,1);}accessibleValueName.clear();}
    void queueAccessibility(){if(automation && !accessNotifyPending){accessNotifyPending=PostMessageW(window,rotaryAccessNotify,0,0)!=FALSE;}}
    void notifyAccessibility(){accessNotifyPending=false;if(!automation)return;auto current=accessState();auto previous=std::move(lastAccess);lastAccess=current;if(!UiaClientsAreListening())return;
        auto number=[&](PROPERTYID id,double a,double b){if(a==b)return;VARIANT before{},after{};before.vt=after.vt=VT_R8;before.dblVal=a;after.dblVal=b;UiaRaiseAutomationPropertyChangedEvent(automation,id,before,after);};
        auto boolean=[&](PROPERTYID id,bool a,bool b){if(a==b)return;VARIANT before{},after{};before.vt=after.vt=VT_BOOL;before.boolVal=a?VARIANT_TRUE:VARIANT_FALSE;after.boolVal=b?VARIANT_TRUE:VARIANT_FALSE;UiaRaiseAutomationPropertyChangedEvent(automation,id,before,after);};
        auto text=[&](PROPERTYID id,const std::wstring& a,const std::wstring& b){if(a==b)return;VARIANT before{},after{};before.vt=after.vt=VT_BSTR;before.bstrVal=SysAllocString(a.c_str());after.bstrVal=SysAllocString(b.c_str());if(before.bstrVal && after.bstrVal)UiaRaiseAutomationPropertyChangedEvent(automation,id,before,after);VariantClear(&before);VariantClear(&after);};
        number(UIA_RangeValueValuePropertyId,previous.value,current.value);number(UIA_RangeValueSmallChangePropertyId,previous.smallChange,current.smallChange);number(UIA_RangeValueLargeChangePropertyId,previous.largeChange,current.largeChange);
        text(UIA_ValueValuePropertyId,previous.text,current.text);text(UIA_NamePropertyId,previous.name,current.name);
        boolean(UIA_IsEnabledPropertyId,previous.enabled,current.enabled);boolean(UIA_IsKeyboardFocusablePropertyId,previous.enabled,current.enabled);
        boolean(UIA_RangeValueIsReadOnlyPropertyId,!previous.enabled,!current.enabled);boolean(UIA_ValueIsReadOnlyPropertyId,!previous.enabled,!current.enabled);
        boolean(UIA_HasKeyboardFocusPropertyId,previous.focused,current.focused);if(current.focused && !previous.focused)UiaRaiseAutomationEvent(automation,UIA_AutomationFocusChangedEventId);
    }
    void access(RotaryAccessRequest& r){if(r.provider!=automation || !ready)return;
        if(r.kind==RotaryAccessRequest::read){r.state=accessState();r.result=S_OK;return;}
        if(!interactive()){r.result=UIA_E_ELEMENTNOTENABLED;return;}
        double n=0;if(r.kind==RotaryAccessRequest::setNumber){if(!std::isfinite(r.number) || r.number<spec.minimum || r.number>spec.maximum){r.result=E_INVALIDARG;return;}n=spec.toNormalized(r.number);}
        else {auto text=win::utf8(r.text?r.text:L"");if(!parseDisplay(spec,policy,text.c_str(),n)){r.result=E_INVALIDARG;return;}}
        // A screen-reader write is a complete edit, not a continuation of an
        // unrelated mouse/text gesture. Refresh does not itself perform an edit.
        end();if(GetCapture()==window)ReleaseCapture();cancelled=true;typing=false;refresh(enabled);
        r.result=once(n)?S_OK:E_FAIL;refresh(enabled);
    }
    unsigned backgroundColor()const{return policy.dark?0x245566:0xf5fafb;}
    double coordinate(LPARAM l)const{auto p=win::point(window,l);return policy.style==ControlStyle::horizontal?-p.X:p.Y;}
    bool hit(LPARAM l)const{auto p=win::point(window,l);auto size=win::size(window);auto g=ControlGeometry::layout(size.Width,size.Height,policy);return policy.style==ControlStyle::horizontal?p.X>=80 && p.X<=size.Width-112:p.Y>=20 && p.Y<(policy.style==ControlStyle::rotary?g.valueTop:size.Height-24);}
    void clearTooltip(){if(unitTooltip && IsWindow(unitTooltip))DestroyWindow(unitTooltip);unitTooltip=nullptr;}
    void end(){if(dragging){dragging=false;services.endEdit(services.owner,spec.id);}}
    bool apply(double n){n=std::clamp(n,0.,1.);if(spec.stepCount)n=std::round(n*spec.stepCount)/spec.stepCount;
        if(n==normalized)return true;if(services.performEdit(services.owner,spec.id,n)){double actual=services.readTarget(services.owner,spec.id);normalized=std::isfinite(actual)?std::clamp(actual,0.,1.):n;if(std::abs(normalized-n)>1e-12)drag.accumulator=normalized;refresh(enabled);return true;}return false;}
    bool once(double n){if(!interactive())return false;if(n==normalized)return true;if(dragging)return apply(n);if(!services.beginEdit(services.owner,spec.id))return false;dragging=true;bool ok=apply(n);end();return ok;}
    void prepare(){if(typing || !interactive())return;end();refresh(enabled);typing=true;cancelled=false;auto s=edit.begin(spec,policy,normalized);SetWindowTextW(value,win::wide(s).c_str());SendMessageW(value,EM_SETSEL,0,-1);layout();}
    void layout(){if(!window || !value)return;auto s=win::size(window);auto g=ControlGeometry::layout(s.Width,s.Height,policy);double unitWidth=*controlDisplayUnit(spec,normalized,typing,policy)?30:0;
        if(policy.style==ControlStyle::horizontal)win::place(value,s.Width-105,(s.Height-22)/2,75,22);
        else win::place(value,0,policy.style==ControlStyle::rotary?g.valueTop:s.Height-24,s.Width-unitWidth,g.valueHeight);
        int pixels=int(std::lround(ControlGeometry::valuePointSize(g.diameter,policy)*win::scale(window)));if(pixels==fontPixels)return;fontPixels=pixels;auto font=CreateFontW(-pixels,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");SendMessageW(value,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(unitTooltip)SendMessageW(unitTooltip,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(valueFont)DeleteObject(valueFont);valueFont=font;
    }
    void paint(HDC dc=nullptr){win::Paint p(window,true,dc);auto& g=p.graphics();float w=p.width(),h=p.height();win::fill(g,{0,0,w,h},backgroundColor());
        unsigned ink=policy.dark?0xb9dce4:0x17333c,muted=policy.dark?0x9ec2cd:0x7a939d;float opacity=enabled?1.f:.45f;
        auto label=services.view && services.view->language==UiLanguage::chinese && policy.labelZh?policy.labelZh:spec.title;
        auto geo=ControlGeometry::layout(w,h,policy);float labelHeight=float(ControlGeometry::labelFieldHeight(policy));
        if(policy.style==ControlStyle::horizontal)win::text(g,label,{0,(h-20)/2,76,20},float(ControlGeometry::labelPointSize(policy)),ink,true);
        else win::text(g,label,{0,0,w,labelHeight},float(ControlGeometry::labelPointSize(policy)),ink,true,Gdiplus::StringAlignmentCenter);
        const char* unit=controlDisplayUnit(spec,normalized,typing,policy);
        if(policy.style==ControlStyle::horizontal)win::text(g,unit,{w-27,(h-16)/2,27,16},10,muted);
        else if(*unit)win::text(g,unit,{w-27,float((policy.style==ControlStyle::rotary?geo.valueTop:h-24)+geo.valueHeight-18),27,18},10,muted);
        auto c=[&](unsigned rgb){return win::color(rgb,BYTE(255*opacity));};
        if(policy.style!=ControlStyle::rotary){bool horizontal=policy.style==ControlStyle::horizontal;Gdiplus::PointF a{horizontal?88.f:w/2,horizontal?h/2:h-36},b{horizontal?w-120:w/2,horizontal?h/2:30};Gdiplus::PointF thumb{a.X+(b.X-a.X)*float(normalized),a.Y+(b.Y-a.Y)*float(normalized)};
            Gdiplus::Pen track(c(0xccdee4),6),active(c(0x3ec5d0),6);track.SetStartCap(Gdiplus::LineCapRound);track.SetEndCap(Gdiplus::LineCapRound);active.SetStartCap(Gdiplus::LineCapRound);active.SetEndCap(Gdiplus::LineCapRound);g.DrawLine(&track,a,b);g.DrawLine(&active,a,thumb);
            Gdiplus::RectF r{thumb.X-(horizontal?10:18),thumb.Y-(horizontal?16:12),horizontal?20.f:36.f,horizontal?32.f:24.f};win::fill(g,r,0xf6fbfc,5);win::stroke(g,r,0xabcbd3,5);win::line(g,thumb.X-7,thumb.Y,thumb.X+7,thumb.Y,0x18b6c4,2);return;}
        double d=geo.diameter,cx=w/2,cy=geo.top+d/2;auto arc=[&](double amount,unsigned rgb){std::array<Gdiplus::PointF,101> points{};for(unsigned i=0;i<=100;++i){double a=rotaryAngle(amount*i/100.)*3.141592653589793/180;points[i]={float(cx+std::sin(a)*d*.43),float(cy-std::cos(a)*d*.43)};}Gdiplus::Pen pen(c(rgb),3);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);g.DrawLines(&pen,points.data(),int(points.size()));};
        arc(1,policy.dark?0x438392:0xd3e6eb);arc(normalized,enabled?(policy.dark?0x68e5ed:0x18b5c3):0x99adb4);
        Gdiplus::RectF disc{float(cx-d*.345),float(cy-d*.345),float(d*.69),float(d*.69)};Gdiplus::SolidBrush outer(c(policy.dark?0x245566:0xf6fbfc)),inner(c(policy.dark?0x245566:0xffffff));Gdiplus::Pen edge(c(policy.dark?0x438392:0xc1dbe3),1);g.FillEllipse(&outer,disc);g.DrawEllipse(&edge,disc);disc.Inflate(float(-d*.05),float(-d*.05));g.FillEllipse(&inner,disc);
        double a=rotaryAngle(normalized)*3.141592653589793/180;win::line(g,float(cx+std::sin(a)*d*.16),float(cy-std::cos(a)*d*.16),float(cx+std::sin(a)*d*.27),float(cy-std::cos(a)*d*.27),policy.dark?0xadf6fc:0x008b9e,3);
    }
    static LRESULT CALLBACK editProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){auto* p=reinterpret_cast<WinRotary*>(data);
        if(m==WM_NCDESTROY){p->clearValueAnnotation(h);if(p->value==h)p->value=nullptr;RemoveWindowSubclass(h,editProc,1);return DefSubclassProc(h,m,w,l);}
        if(m==WM_SETFOCUS)p->prepare();
        if(m==WM_KEYDOWN && (w==VK_RETURN || w==VK_ESCAPE)){p->cancelled=w==VK_ESCAPE;SetFocus(p->window);return 0;}
        // Native field remains editable under bypass, including text selection;
        // grayscale its actual painted pixels without changing its edit buffer.
        if(m==WM_PAINT && win::paused(h)){win::Paint paint(h);paint.graphics().Flush(Gdiplus::FlushIntentionSync);DefSubclassProc(h,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(paint.dc()),PRF_CLIENT);return 0;}
        return DefSubclassProc(h,m,w,l);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto* p=reinterpret_cast<WinRotary*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){p=static_cast<WinRotary*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(h,m,w,l);
        switch(m){
        case WM_DESTROY:p->ready=false;p->end();p->clearTooltip();if(p->automation)p->automation->invalidate();UiaReturnRawElementProvider(h,0,0,nullptr);break;
        case WM_NCDESTROY:p->ready=false;p->window=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);return DefWindowProcW(h,m,w,l);
        case WM_GETOBJECT:if(static_cast<LONG>(l)==UiaRootObjectId && p->ready){if(!p->automation){p->automation=RotaryProvider::create(h);p->lastAccess=p->accessState();}if(p->automation)return UiaReturnRawElementProvider(h,w,l,p->automation);}break;
        case rotaryAccessRequest:if(l)p->access(*reinterpret_cast<RotaryAccessRequest*>(l));return 0;
        case rotaryAccessNotify:p->notifyAccessibility();return 0;
        case WM_SETFOCUS:p->queueAccessibility();return 0;
        case WM_ENABLE:if(!w){p->end();if(GetCapture()==h)ReleaseCapture();}p->queueAccessibility();break;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:p->paint();return 0;
        case WM_PRINTCLIENT:p->paint(reinterpret_cast<HDC>(w));return 0;
        case WM_SIZE:p->layout();return 0;
        case WM_GETDLGCODE:{auto* message=reinterpret_cast<MSG*>(l);auto key=message?message->wParam:w;bool own=key==VK_RETURN || key==VK_F2 || key==VK_HOME || key==VK_END || key==VK_PRIOR || key==VK_NEXT || key==VK_ESCAPE;return DLGC_WANTARROWS|(own?DLGC_WANTMESSAGE:0);}
        case WM_LBUTTONDBLCLK:if(p->enabled && p->hit(l)){p->end();p->once(p->spec.toNormalized(p->spec.initial));}return 0;
        case WM_LBUTTONDOWN:if(p->enabled && p->hit(l)){p->end();SetFocus(h);p->refresh(p->enabled);p->drag.begin(p->normalized,p->coordinate(l));p->dragging=p->services.beginEdit(p->services.owner,p->spec.id);if(p->dragging)SetCapture(h);}return 0;
        case WM_MOUSEMOVE:if(p->dragging)p->apply(p->drag.move(p->coordinate(l),(GetKeyState(VK_SHIFT)&0x8000)!=0));return 0;
        case WM_LBUTTONUP:p->end();if(GetCapture()==h)ReleaseCapture();return 0;
        case WM_CAPTURECHANGED:case WM_KILLFOCUS:case WM_CANCELMODE:p->end();p->queueAccessibility();return 0;
        case WM_RBUTTONDOWN:p->once(p->spec.toNormalized(p->spec.initial));return 0;
        case WM_MOUSEWHEEL:if(GetFocus()==h){double step=p->spec.stepCount?1./p->spec.stepCount:((GetKeyState(VK_SHIFT)&0x8000)?.001:.01);p->once(std::clamp(p->normalized+GET_WHEEL_DELTA_WPARAM(w)/double(WHEEL_DELTA)*step,0.,1.));return 0;}break;
        case WM_KEYDOWN:
            if(w==VK_ESCAPE){p->end();if(GetCapture()==h)ReleaseCapture();return 0;}
            if(!p->interactive())break;
            if(w==VK_RETURN || w==VK_F2){SetFocus(p->value);p->prepare();return 0;}
            if(w==VK_HOME || w==VK_END || w==VK_PRIOR || w==VK_NEXT || w==VK_LEFT || w==VK_RIGHT || w==VK_UP || w==VK_DOWN){
                p->refresh(p->enabled);double step=p->smallStep();if(!p->spec.stepCount && (GetKeyState(VK_SHIFT)&0x8000))step=.001;
                if(w==VK_PRIOR || w==VK_NEXT)step=p->largeStep();double next=w==VK_HOME?0:w==VK_END?1:p->normalized+((w==VK_RIGHT || w==VK_UP || w==VK_PRIOR)?step:-step);
                p->once(std::clamp(next,0.,1.));return 0;}break;
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:if(reinterpret_cast<HWND>(l)==p->value){auto dc=reinterpret_cast<HDC>(w);unsigned fg=p->policy.dark?0xeafcff:0x17333c,bg=p->backgroundColor();auto rgb=[&](unsigned c){if(win::paused(h)){unsigned v=((c>>16)*54+((c>>8)&255)*183+(c&255)*19)/256;return RGB(v,v,v);}return RGB(c>>16,(c>>8)&255,c&255);};SetTextColor(dc,rgb(fg));SetBkColor(dc,rgb(bg));if(p->background)DeleteObject(p->background);p->background=CreateSolidBrush(rgb(bg));return reinterpret_cast<LRESULT>(p->background);}break;
        case WM_COMMAND:if(reinterpret_cast<HWND>(l)==p->value){if(HIWORD(w)==EN_SETFOCUS)p->prepare();if(HIWORD(w)==EN_KILLFOCUS && p->typing){auto text=win::utf8(win::windowText(p->value));double next=0;bool changed=!p->cancelled && p->edit.changed(p->spec,p->policy,text.c_str(),next);p->typing=false;if(changed)p->once(next);p->refresh(p->enabled);}return 0;}break;
        }return DefWindowProcW(h,m,w,l);
    }
public:
    WinRotary(HWND parent,const EditorServices& s,const ParameterSpec& p,DisplayPolicy d):services(s),spec(p),policy(d){module=win::moduleAt(reinterpret_cast<const void*>(&proc));WNDCLASSW c{};c.style=CS_DBLCLKS;c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=L"JUST.Shared.Rotary.v3";c.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!windowClass.acquire(c))return;
        window=CreateWindowExW(0,c.lpszClassName,win::wide(spec.title).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN,0,0,112,148,parent,nullptr,module,this);if(!window)return;
        value=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_RIGHT|ES_AUTOHSCROLL,0,108,112,24,window,reinterpret_cast<HMENU>(1),module,nullptr);if(!value || !SetWindowSubclass(value,editProc,1,reinterpret_cast<DWORD_PTR>(this)))return;SendMessageW(value,EM_SETLIMITTEXT,127,0);
        // Match the Mac value field's unit tooltip; never attach this to the
        // EQ graph or rotary title. Keep the UTF-16 text alive with the HWND.
        unitHelp=win::wide(spec.unit);if(!unitHelp.empty()){
            unitTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,module,nullptr);
            if(unitTooltip){TOOLINFOW info{};info.cbSize=sizeof(info);info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(value);info.lpszText=unitHelp.data();if(!SendMessageW(unitTooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info)))clearTooltip();}
        }const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);initializedCom=SUCCEEDED(com);
        CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IAccPropServices),reinterpret_cast<void**>(&accessibleProperties));
        ready=true;layout();annotateValue();}
    ~WinRotary() override{cancelled=true;typing=false;end();clearTooltip();if(window)DestroyWindow(window);if(valueFont)DeleteObject(valueFont);if(background)DeleteObject(background);if(automation){automation->dispose();automation=nullptr;}if(accessibleProperties)accessibleProperties->Release();if(initializedCom)CoUninitialize();}
    void resize(int x,int y,int width,int height) override{int minimumHeight=policy.style==ControlStyle::horizontal?32:80;if(ControlGeometry::customTypography(policy))minimumHeight=std::max(minimumHeight,int(ControlGeometry::labelFieldHeight(policy)+28+ControlGeometry::readoutHeight(policy)));win::place(window,x,y,std::max(width,policy.style==ControlStyle::horizontal?240:54),std::max(height,minimumHeight));layout();}
    void refresh(bool e) override{if(!window || !value)return;enabled=e;if(!e){end();cancelled=true;typing=false;if(GetCapture()==window)ReleaseCapture();}if(!dragging){double n=services.readTarget(services.owner,spec.id);if(std::isfinite(n))normalized=std::clamp(n,0.,1.);}if(!typing){char text[128];formatControlDisplay(spec,normalized,services.view && services.view->advanced?DisplayContext::advanced:DisplayContext::simple,policy,text,sizeof(text));auto s=win::wide(text);if(win::windowText(value)!=s)SetWindowTextW(value,s.c_str());}EnableWindow(window,e);EnableWindow(value,e);annotateValue();layout();InvalidateRect(window,nullptr,FALSE);InvalidateRect(value,nullptr,FALSE);queueAccessibility();}
    void* nativeHandle()const noexcept override{return ready?window:nullptr;}
};
}
std::unique_ptr<RotaryControl> RotaryControl::create(void* parent,const EditorServices& services,const ParameterSpec& spec,DisplayPolicy policy){if(!parent || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return {};auto result=std::make_unique<WinRotary>(static_cast<HWND>(parent),services,spec,policy);if(!result->nativeHandle())return {};result->refresh(true);return result;}
}
