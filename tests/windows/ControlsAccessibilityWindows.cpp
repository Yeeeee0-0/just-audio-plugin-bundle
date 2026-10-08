// Native UI Automation client tests. Hidden test HWNDs only; no REAPER or input injection.
// Microsoft's client threading guidance requires these calls on a separate MTA
// while the window-owning thread continues dispatching messages.
#include "common/ui/Controls.hpp"
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT command=WM_APP+0x650;
enum Command {key,language,enable,restore,remove,refuseBegin,refuseWrite};
struct Fixture {
    just::EditorViewState view;
    std::unique_ptr<just::RotaryControl> rotary;
    std::atomic<double> normalized{.5};
    std::atomic<unsigned> starts{0},writes{0},ends{0},wrongThread{0},wrongID{0};
    bool beginAllowed=true,writeAllowed=true;DWORD owner=GetCurrentThreadId();
    HWND knob=nullptr,field=nullptr;
    just::EditorServices services(){just::EditorServices s;s.owner=this;s.view=&view;
        s.readTarget=[](void* p,just::ParamID id){auto& f=*static_cast<Fixture*>(p);f.checkThread(id);return f.normalized.load();};
        s.beginEdit=[](void* p,just::ParamID id){auto& f=*static_cast<Fixture*>(p);f.checkThread(id);if(!f.beginAllowed)return false;++f.starts;return true;};
        s.performEdit=[](void* p,just::ParamID id,double n){auto& f=*static_cast<Fixture*>(p);f.checkThread(id);if(!f.writeAllowed)return false;++f.writes;f.normalized=n;return true;};
        s.endEdit=[](void* p,just::ParamID id){auto& f=*static_cast<Fixture*>(p);f.checkThread(id);++f.ends;};return s;}
    void checkThread(just::ParamID id){if(GetCurrentThreadId()!=owner)++wrongThread;if(id!=77)++wrongID;}
};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void ok(HRESULT hr,const char* message){require(SUCCEEDED(hr),message);}
std::string resultText(HRESULT hr){std::ostringstream out;out<<"0x"<<std::hex<<static_cast<unsigned long>(hr);return out.str();}
LRESULT CALLBACK hostProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    auto* f=reinterpret_cast<Fixture*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){f=static_cast<Fixture*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(f));}
    if(message==command && f){switch(wp){
        case key:SendMessageW(f->knob,WM_KEYDOWN,static_cast<WPARAM>(lp),0);break;
        case language:f->view.language=lp?just::UiLanguage::english:just::UiLanguage::chinese;f->rotary->refresh();break;
        case enable:f->rotary->refresh(lp!=0);break;
        case restore:f->normalized=.5;f->rotary->refresh();break;
        case remove:f->rotary.reset();break;
        case refuseBegin:f->beginAllowed=lp==0;break;
        case refuseWrite:f->writeAllowed=lp==0;break;
    }return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
std::wstring name(IUIAutomationElement* element){BSTR text=nullptr;ok(element->get_CurrentName(&text),"read accessible name");std::wstring result=text?text:L"";SysFreeString(text);return result;}
double number(IUIAutomationRangeValuePattern* range){double value=0;ok(range->get_CurrentValue(&value),"read physical range value");return value;}
std::wstring text(IUIAutomationValuePattern* value){BSTR raw=nullptr;ok(value->get_CurrentValue(&raw),"read accessible text value");std::wstring result=raw?raw:L"";SysFreeString(raw);return result;}
void client(HWND host,Fixture& f){
    ComPtr<IUIAutomation> automation;ok(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(automation.GetAddressOf())),"create actual Windows UIA client");
    ComPtr<IUIAutomationElement> knob,field;ok(automation->ElementFromHandle(f.knob,knob.GetAddressOf()),"discover knob through WM_GETOBJECT");
    ok(automation->ElementFromHandle(f.field,field.GetAddressOf()),"discover native numeric EDIT");
    CONTROLTYPEID type=0;ok(knob->get_CurrentControlType(&type),"read control type");require(type==UIA_SliderControlTypeId,"owner-drawn knob must expose Slider role");
    require(name(knob.Get())==L"Gain","knob name matches parameter label");require(name(field.Get())==L"Gain value","native EDIT has an independent accessible label");
    ComPtr<IUIAutomationRangeValuePattern> range;ComPtr<IUIAutomationValuePattern> value;
    ok(knob->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(range.GetAddressOf())),"RangeValue pattern exposed");
    ok(knob->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(value.GetAddressOf())),"Value pattern exposed");
    double minimum=0,maximum=0,smallChange=0,largeChange=0;ok(range->get_CurrentMinimum(&minimum),"range minimum");ok(range->get_CurrentMaximum(&maximum),"range maximum");
    ok(range->get_CurrentSmallChange(&smallChange),"range smallChange change");ok(range->get_CurrentLargeChange(&largeChange),"range largeChange change");
    require(minimum==-24 && maximum==24 && std::abs(smallChange-.48)<1e-10 && std::abs(largeChange-4.8)<1e-10,"physical range and keyboard increments");
    require(std::abs(number(range.Get()))<1e-12 && text(value.Get())==L"0.0 dB","accessible values describe physical units");
    auto starts=f.starts.load(),writes=f.writes.load(),ends=f.ends.load();
    ok(range->SetValue(12),"UIA physical SetValue accepted");require(std::abs(f.normalized.load()-.75)<1e-12,"UIA converts physical dB to existing normalized parameter");
    require(f.starts==starts+1 && f.writes==writes+1 && f.ends==ends+1,"UIA SetValue produces exactly one balanced original-ID gesture");
    ok(value->SetValue(L"-12 dB"),"UIA text SetValue uses existing parser");require(std::abs(number(range.Get())+12)<1e-12,"UIA text value reaches the control");
    writes=f.writes;ok(range->SetValue(-12),"setting identical value succeeds");require(f.writes==writes,"identical accessible value emits no sound edit");
    require(FAILED(range->SetValue(25)) && FAILED(range->SetValue(std::numeric_limits<double>::quiet_NaN())) && FAILED(value->SetValue(L"not a value")),"out-of-range/nonfinite/invalid text rejected");
    require(f.writes==writes,"invalid UIA writes leave audio target untouched");
    SendMessageW(host,command,refuseBegin,1);require(FAILED(range->SetValue(1)),"host beginEdit rejection is surfaced");SendMessageW(host,command,refuseBegin,0);
    starts=f.starts;ends=f.ends;SendMessageW(host,command,refuseWrite,1);require(FAILED(range->SetValue(1)),"host performEdit rejection is surfaced");SendMessageW(host,command,refuseWrite,0);
    require(f.starts==starts+1 && f.ends==ends+1 && f.writes==writes,"rejected performEdit still closes its gesture");
    SendMessageW(host,command,language,0);require(name(knob.Get())==L"增益" && name(field.Get())==L"增益 数值","language refresh updates both accessible labels");
    SendMessageW(host,command,language,1);require(name(knob.Get())==L"Gain","English accessible label restored");
    SendMessageW(host,command,enable,0);BOOL enabled=TRUE,readOnly=FALSE;
    ok(knob->get_CurrentIsEnabled(&enabled),"read disabled state");ok(range->get_CurrentIsReadOnly(&readOnly),"read disabled range writeability");
    require(!enabled && readOnly,"disabled knob is exposed as disabled/read-only");require(FAILED(range->SetValue(3)),"disabled UIA write rejected");
    SendMessageW(host,command,key,VK_RIGHT);require(f.writes==writes,"disabled keyboard and UIA cannot edit");
    SendMessageW(host,command,enable,1);SendMessageW(host,command,restore,0);
    SendMessageW(host,command,key,VK_RIGHT);require(std::abs(f.normalized.load()-.51)<1e-12,"Right increments one normal step");
    SendMessageW(host,command,key,VK_LEFT);require(std::abs(f.normalized.load()-.5)<1e-12,"Left decrements one normal step");
    SendMessageW(host,command,key,VK_PRIOR);require(std::abs(f.normalized.load()-.6)<1e-12,"PageUp increments one largeChange step");
    SendMessageW(host,command,key,VK_NEXT);require(std::abs(f.normalized.load()-.5)<1e-12,"PageDown decrements one largeChange step");
    SendMessageW(host,command,key,VK_HOME);require(f.normalized==0,"Home reaches minimum");
    SendMessageW(host,command,key,VK_END);require(f.normalized==1,"End reaches maximum");
    writes=f.writes;SendMessageW(host,command,key,VK_RIGHT);require(f.writes==writes,"keyboard endpoint emits no redundant edit");
    MSG tab{};tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;
    auto code=SendMessageW(f.knob,WM_GETDLGCODE,VK_TAB,reinterpret_cast<LPARAM>(&tab));require((code&DLGC_WANTTAB)==0 && (code&DLGC_WANTALLKEYS)==0,"knob does not trap Tab navigation");
    MSG enter{};enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
    code=SendMessageW(f.knob,WM_GETDLGCODE,VK_RETURN,reinterpret_cast<LPARAM>(&enter));require((code&DLGC_WANTMESSAGE)!=0,"Enter routed to numeric editing instead of a dialog default action");
    require(f.starts==f.ends && f.wrongID==0 && f.wrongThread==0,"all edits balanced, use unchanged ID77 and execute on UI thread");
    // Keep real client proxies across destruction. Provider invalidation is
    // immediate, but UiaDisconnectProvider runs asynchronously: Microsoft forbids
    // it inside the cross-thread SendMessage that destroys this HWND. Current
    // properties may remain available from UIA until that disconnect completes.
    starts=f.starts;writes=f.writes;ends=f.ends;const auto target=f.normalized.load();
    SendMessageW(host,command,remove,0);
    require(!IsWindow(f.knob) && !IsWindow(f.field),"native knob and EDIT are destroyed");
    const auto teardownStart=GetTickCount64(),deadline=teardownStart+10000;
    unsigned attempts=0;
    for(;;){
        ++attempts;double after=0;BSTR afterText=nullptr;
        const auto rangeResult=range->get_CurrentValue(&after);
        const auto valueResult=value->get_CurrentValue(&afterText);SysFreeString(afterText);
        const auto writeResult=value->SetValue(L"0");
        const auto detail="range="+resultText(rangeResult)+" value="+resultText(valueResult)+" write="+resultText(writeResult);
        require(FAILED(writeResult),("destroyed provider rejects writes immediately: "+detail).c_str());
        require(f.starts==starts && f.writes==writes && f.ends==ends && f.normalized==target
            && f.wrongID==0 && f.wrongThread==0,"no parameter callback or target change after native teardown");
        if(rangeResult==UIA_E_ELEMENTNOTAVAILABLE && valueResult==UIA_E_ELEMENTNOTAVAILABLE && writeResult==UIA_E_ELEMENTNOTAVAILABLE){
            std::cout<<"UIA teardown disconnected after "<<(GetTickCount64()-teardownStart)<<" ms / "<<attempts<<" attempts: "<<detail<<'\n';break;
        }
        require(GetTickCount64()<deadline,("UIA teardown did not disconnect client references within 10000 ms: "+detail).c_str());
        // Only the MTA client waits; the UI thread keeps pumping window/COM work.
        Sleep(10);
    }
}
}
int main(){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 2;
    Fixture fixture;std::string error;HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread worker;
    try{
        WNDCLASSW klass{};klass.lpfnWndProc=hostProc;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"JUST.Accessibility.Test";
        require(RegisterClassW(&klass)!=0,"register hidden test host");
        HWND host=CreateWindowExW(WS_EX_NOACTIVATE,klass.lpszClassName,L"JUST hidden accessibility host",WS_POPUP,0,0,500,400,nullptr,nullptr,klass.hInstance,&fixture);
        require(host && done,"create test window/event");
        const just::ParameterSpec spec{"test.gain",77,"Gain","dB",-24,24,0,just::Mapping::linear,0,true,"all",just::Transition::continuous,10};
        just::DisplayPolicy policy;policy.labelZh="增益";
        fixture.rotary=just::RotaryControl::create(host,fixture.services(),spec,policy);require(bool(fixture.rotary),"create actual native rotary");
        fixture.knob=static_cast<HWND>(fixture.rotary->nativeHandle());fixture.field=GetDlgItem(fixture.knob,1);
        worker=std::thread([&]{auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{ok(hr,"initialize UIA client MTA");client(host,fixture);}catch(const std::exception& e){error=e.what();}if(SUCCEEDED(hr))CoUninitialize();SetEvent(done);});
        const auto deadline=GetTickCount64()+60000;
        while(WaitForSingleObject(done,0)==WAIT_TIMEOUT){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(GetTickCount64()>deadline){std::cerr<<"FAIL UIA worker timed out\n";std::exit(1);}MsgWaitForMultipleObjects(1,&done,FALSE,10,QS_ALLINPUT);}
        worker.join();fixture.rotary.reset();DestroyWindow(host);UnregisterClassW(klass.lpszClassName,klass.hInstance);
        require(error.empty(),error.c_str());CloseHandle(done);CoUninitialize();
        std::cout<<"PASS native UIA Slider/RangeValue/Value, bilingual names, host gestures, keyboard, disabled/errors and teardown; hidden HWND only, no real screen-reader/REAPER acceptance\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';if(worker.joinable()){std::exit(1);}fixture.rotary.reset();if(done)CloseHandle(done);CoUninitialize();return 1;}
}
