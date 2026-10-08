#include "NativeEditor.hpp"
#include <windows.h>
#include <commctrl.h>
#include <string>
namespace just {
namespace {
struct NativeView {
    HWND window=nullptr,title=nullptr,preset=nullptr,bypass=nullptr,advanced=nullptr,status=nullptr,feedback=nullptr;
    std::array<HWND,4> labels{};const PluginIdentity* product;EditorCallbacks callbacks;HFONT font=nullptr;bool presentationInitialized=false,lastPaused=false;
    HWND contentParent=nullptr;std::unique_ptr<EditorContent> content;HMODULE module=nullptr;std::wstring className;
    HWND tooltip=nullptr;std::wstring bypassHelp;
};
std::wstring wide(const char* text) {
    int size=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),size);return result;
}
void refresh(NativeView* v) {
    const auto& s=v->callbacks.services;
    const auto transaction=s.readPresetTransaction?s.readPresetTransaction(s.owner):PresetTransactionStatus::unavailable;
    EnableWindow(v->preset,effectiveFactoryPresetCount(moduleDefinition()) && transaction!=PresetTransactionStatus::pending);
    SetWindowTextW(v->preset,transaction==PresetTransactionStatus::pending?L"Pending…":transaction==PresetTransactionStatus::rejected?L"Retry preset":L"Preset");
    if(GetForegroundWindow()!=GetAncestor(v->window,GA_ROOT) && s.readAuditionStatus && s.endAudition){auto a=s.readAuditionStatus(s.owner);if(a.token)s.endAudition(s.owner,a.token);}
    bool bypass=v->callbacks.getBypass(v->callbacks.owner),advanced=v->callbacks.view->advanced;
    if(!v->presentationInitialized || v->lastPaused!=bypass){if(v->callbacks.view->visualsPaused && !bypass)++v->callbacks.view->visualResumeGeneration;v->callbacks.view->visualsPaused=bypass;if(v->callbacks.setVisualsPaused)v->callbacks.setVisualsPaused(v->callbacks.owner,bypass);if(v->content)v->content->setVisualsPaused(bypass);v->presentationInitialized=true;v->lastPaused=bypass;}
    SendMessage(v->bypass,BM_SETCHECK,bypass?BST_CHECKED:BST_UNCHECKED,0);
    SendMessage(v->advanced,BM_SETCHECK,advanced?BST_CHECKED:BST_UNCHECKED,0);
    const auto snapshot=v->callbacks.readStatus(v->callbacks.owner);
    SetWindowText(v->status,wide(statusText(snapshot,bypass).c_str()).c_str());
    SetWindowText(v->feedback,advanced?L"Advanced groups pending module review.\r\nView changes preserve all sound targets.":L"Foundation placeholder\r\nAudio passes through. No effect algorithm or analysis is implemented.");
    for(std::size_t i=0;i<v->product->simpleCount;++i)ShowWindow(v->labels[i],advanced || v->content?SW_HIDE:SW_SHOW);
    ShowWindow(v->feedback,v->content?SW_HIDE:SW_SHOW);
    if(v->content)v->content->refresh(*v->callbacks.view,snapshot);
}
LRESULT CALLBACK windowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto* v=reinterpret_cast<NativeView*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE){v=static_cast<NativeView*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}
    if(v && msg==WM_COMMAND) {
        if(LOWORD(wp)==100){const auto& m=moduleDefinition();const auto& s=v->callbacks.services;HMENU menu=CreatePopupMenu();
            for(unsigned i=0;i<effectiveFactoryPresetCount(m);++i)AppendMenuW(menu,MF_STRING,1000+i,m.factoryPresetCount?wide((std::string(m.factoryPresets[i].category)+" — "+m.factoryPresets[i].name).c_str()).c_str():L"Default");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|((s.canUndoLastPreset && s.canUndoLastPreset(s.owner))?0:MF_GRAYED),2000,L"Undo last preset");
            RECT r;GetWindowRect(v->preset,&r);auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,r.left,r.bottom,0,window,nullptr);DestroyMenu(menu);
            if(selected==2000 && s.undoLastPreset)s.undoLastPreset(s.owner);
            else if(selected>=1000 && selected<1000+effectiveFactoryPresetCount(m) && s.readCompleteSoundState && s.requestApplySoundState){SoundState current,desired;if(s.readCompleteSoundState(s.owner,current) && buildFactoryPreset(m,selected-1000,current.plugin,desired)){auto bypass=m.parameters.index(bypassParamID);desired.targets[bypass]=current.targets[bypass];s.requestApplySoundState(s.owner,desired);}}
            refresh(v);return 0;}
        if(LOWORD(wp)==101){v->callbacks.setBypass(v->callbacks.owner,SendMessage(v->bypass,BM_GETCHECK,0,0)==BST_CHECKED);refresh(v);return 0;}
        if(LOWORD(wp)==102){v->callbacks.view->advanced=SendMessage(v->advanced,BM_GETCHECK,0,0)==BST_CHECKED;refresh(v);return 0;}
    }
    if(v && msg==WM_TIMER){refresh(v);return 0;}
    return DefWindowProc(window,msg,wp,lp);
}
HWND child(NativeView* v,const wchar_t* klass,const wchar_t* label,DWORD style,int id) {
    HWND h=CreateWindowEx(0,klass,label,WS_CHILD|WS_VISIBLE|style,0,0,10,10,v->window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),v->module,nullptr);
    SendMessage(h,WM_SETFONT,reinterpret_cast<WPARAM>(v->font),TRUE);return h;
}
}
void* createNativeEditor(void* parent,const PluginIdentity& p,EditorCallbacks c) {
    auto* v=new NativeView;v->product=&p;v->callbacks=c;v->font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&windowProc),&v->module);
    v->className=L"JUST.Foundation.Editor."+std::to_wstring(p.slot);
    WNDCLASS klass{};klass.lpfnWndProc=windowProc;klass.hInstance=v->module;klass.lpszClassName=v->className.c_str();klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClass(&klass);
    v->window=CreateWindowEx(0,v->className.c_str(),L"JUST Foundation",WS_CHILD|WS_VISIBLE,0,0,c.view->width,c.view->height,static_cast<HWND>(parent),nullptr,klass.hInstance,v);
    if(!v->window){delete v;return nullptr;}
    v->title=child(v,L"STATIC",wide(p.name).c_str(),SS_LEFT,0);
    v->preset=child(v,L"BUTTON",L"Preset",BS_PUSHBUTTON,100);EnableWindow(v->preset,FALSE);
    v->bypass=child(v,L"BUTTON",L"Bypass",BS_AUTOCHECKBOX,101);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
    v->bypassHelp=wide(moduleBypassTooltip(moduleDefinition()));
    v->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,v->window,nullptr,v->module,nullptr);
    if(v->tooltip){TOOLINFOW info{};info.cbSize=sizeof(info);info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=v->window;info.uId=reinterpret_cast<UINT_PTR>(v->bypass);info.lpszText=v->bypassHelp.data();SendMessageW(v->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
    v->advanced=child(v,L"BUTTON",L"Advanced",BS_AUTOCHECKBOX,102);
    v->status=child(v,L"STATIC",L"",SS_LEFT,0);v->feedback=child(v,L"STATIC",L"",SS_CENTER,0);
    for(std::size_t i=0;i<p.simpleCount;++i){v->labels[i]=child(v,L"STATIC",wide(p.simpleLabels[i]).c_str(),SS_CENTER,0);EnableWindow(v->labels[i],FALSE);}
    if(auto factory=moduleDefinition().createEditorContent) {
        v->contentParent=child(v,L"STATIC",L"",SS_LEFT,0);v->content.reset(factory());
        if(!v->content || !v->content->attach(v->contentParent,v->callbacks.services)){v->content.reset();DestroyWindow(v->contentParent);v->contentParent=nullptr;}
    }
    SetTimer(v->window,1,33,nullptr);refresh(v);return v;
}
void destroyNativeEditor(void* handle){auto* v=static_cast<NativeView*>(handle);KillTimer(v->window,1);v->content.reset();if(v->tooltip)DestroyWindow(v->tooltip);DestroyWindow(v->window);delete v;}
void resizeNativeEditor(void* handle,int width,int height) {
    auto* v=static_cast<NativeView*>(handle);MoveWindow(v->window,0,0,width,height,TRUE);
    MoveWindow(v->title,20,20,width-340,28,TRUE);MoveWindow(v->preset,width-296,20,80,28,TRUE);
    MoveWindow(v->bypass,width-210,20,86,28,TRUE);MoveWindow(v->advanced,width-120,20,105,28,TRUE);
    MoveWindow(v->status,20,62,width-40,24,TRUE);MoveWindow(v->feedback,40,108,width-80,height-200,TRUE);
    int cell=(width-40)/static_cast<int>(v->product->simpleCount);
    for(std::size_t i=0;i<v->product->simpleCount;++i)MoveWindow(v->labels[i],20+cell*static_cast<int>(i),height-70,cell-10,30,TRUE);
    if(v->content){MoveWindow(v->contentParent,20,94,width-40,height-110,TRUE);v->content->resize(width-40,height-110);}
}
}
