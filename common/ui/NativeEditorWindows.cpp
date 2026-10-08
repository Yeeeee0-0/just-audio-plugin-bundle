#include "NativeEditor.hpp"
#include "VisualAssetsWindows.hpp"
#include "../state/UserPresetStore.hpp"
#include <commctrl.h>
#include <shlobj.h>
#include <stdexcept>
namespace just {
namespace {
struct NativeView;
LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK surfaceProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK buttonProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
HWND modalEditor(HWND h){for(;h;h=GetParent(h))if(GetPropW(h,win::viewProperty))return GetDlgItem(h,300)?h:nullptr;return nullptr;}
bool wantsModalEscape(HWND h,WPARAM key,LPARAM data){auto* message=reinterpret_cast<const MSG*>(data);return (message?message->wParam:key)==VK_ESCAPE && modalEditor(h);}
bool queueModalEscape(HWND h);

// System controls keep their native input, accessibility and edit semantics.
// Desaturate their actual native paint only while the ancestor is bypassed.
LRESULT CALLBACK nativeGrayProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    // Let the actual host/dialog keyboard router deliver Escape to the plugin.
    // A direct WM_KEYDOWN fixture bypasses this negotiation and cannot test it.
    if(m==WM_GETDLGCODE && wantsModalEscape(h,w,l))return DefSubclassProc(h,m,w,l)|DLGC_WANTMESSAGE;
    if(m==WM_KEYDOWN && w==VK_ESCAPE && queueModalEscape(h))return 0;

    if((m==WM_PAINT || m==WM_PRINTCLIENT) && win::paused(h)){win::Paint p(h,true,m==WM_PRINTCLIENT?reinterpret_cast<HDC>(w):nullptr);DefSubclassProc(h,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(p.dc()),PRF_CLIENT|PRF_ERASEBKGND);return 0;}
    // Native setters can draw immediately, without going through WM_PAINT.
    // Limiter refreshes its checked BUTTON every timer tick. Repaint that
    // control through the grayscale buffer before the setter returns.
    if(win::paused(h) && (m==BM_SETCHECK || m==BM_SETSTATE || m==WM_SETTEXT || m==WM_ENABLE || m==WM_SETFONT)){
        LRESULT result=DefSubclassProc(h,m,w,l);
        if(IsWindow(h))RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        return result;
    }
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,nativeGrayProc,0x4a555354);return DefSubclassProc(h,m,w,l);
}
BOOL CALLBACK prepareNativeGray(HWND h,LPARAM exclude){if(h==reinterpret_cast<HWND>(exclude))return TRUE;wchar_t name[64]{};GetClassNameW(h,name,64);if(!lstrcmpiW(name,L"STATIC") || !lstrcmpiW(name,L"EDIT") || !lstrcmpiW(name,L"COMBOBOX") || !lstrcmpiW(name,L"BUTTON"))SetWindowSubclass(h,nativeGrayProc,0x4a555354,0);return TRUE;}
constexpr UINT deferredModalCommand=WM_APP+0x4a5;
constexpr int presetID=100,bypassID=101,advancedID=102,settingsID=103,aboutID=104;
constexpr int settingsTabID=201,aboutTabID=202,presetsTabID=203,closeID=204,scaleID=210,languageID=211,lowID=212;
constexpr int selectionID=220,nameID=221,saveID=222,loadID=223,renameID=224,deleteID=225,confirmID=226,cancelID=227;
struct NativeView {
    win::Session drawingSession; // destroyed after all owned GDI+ images
    win::WindowClass editorClass,surfaceClass; // after all owned HWNDs are destroyed
    HWND window=nullptr,preset=nullptr,bypass=nullptr,advanced=nullptr,settings=nullptr,about=nullptr,contentParent=nullptr,tooltip=nullptr;
    HWND overlay=nullptr,panel=nullptr,body=nullptr,scaleMenu=nullptr,languageMenu=nullptr,managed=nullptr,name=nullptr;
    const PluginIdentity* product=nullptr;EditorCallbacks callbacks{};HMODULE module=nullptr;HFONT font=nullptr;
    std::unique_ptr<EditorContent> content;std::unique_ptr<UserPresetStore> store;std::unique_ptr<Gdiplus::Image> productIcon;
    std::vector<HWND> controls;std::vector<std::pair<std::string,std::string>> choices;
    std::wstring bypassHelp,presetMessage,scaleError,draft;std::string selection,pendingDelete;int modalTab=0,scroll=0,logicalWidth=720,logicalHeight=420;
    ULONG_PTR modalGeneration=0;
    bool presentationInitialized=false,lastPaused=false,waiting=false,rebuilding=false,destroying=false;int timerMilliseconds=33;
    ~NativeView(){
        destroying=true;
        if(window)KillTimer(window,1);
        if(presentationInitialized)endAudition();
        content.reset();
        if(tooltip && IsWindow(tooltip))DestroyWindow(tooltip);
        tooltip=nullptr;
        if(window && IsWindow(window)){RemovePropW(window,win::viewProperty);DestroyWindow(window);}
        window=nullptr;
        if(font)DeleteObject(font);
    }
    const wchar_t* tr(const wchar_t* zh,const wchar_t* en)const{return callbacks.view->language==UiLanguage::chinese?zh:en;}
    bool isUser()const{return selection.compare(0,5,"user:")==0;}
    std::wstring factoryTitle(unsigned i)const{const auto& m=moduleDefinition();if(!m.factoryPresetCount)return tr(L"默认",L"Default");return win::wide(std::string(m.factoryPresets[i].category)+" — "+m.factoryPresets[i].name);}
    // Repainting lower siblings must respect the live modal overlay's Z order.
    HWND child(HWND parent,const wchar_t* type,const wchar_t* label,DWORD style,int id){HWND h=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,0,0,10,10,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);if(font)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;}
    HWND button(HWND parent,const wchar_t* text,int id){auto h=child(parent,L"BUTTON",text,(id==lowID?BS_CHECKBOX:BS_PUSHBUTTON)|WS_TABSTOP,id);if(h && !SetWindowSubclass(h,buttonProc,1,reinterpret_cast<DWORD_PTR>(this))){DestroyWindow(h);return nullptr;}return h;}
    void labelButton(HWND h,const wchar_t* text){if(win::windowText(h)!=text)SetWindowTextW(h,text);}
    void invalidate(){RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
    void paint(HWND h,HDC dc=nullptr){
        win::Paint p(h,true,dc);auto& g=p.graphics();float w=p.width(),height=p.height();
        if(h==window){win::fill(g,{0,0,w,height},0xfbfdfe);win::fill(g,{0,84,w,std::max(1.f,height-124)},0xf5fafb);win::line(g,0,84,w,84,0xd9e5e9);win::line(g,0,height-40,w,height-40,0xd9e5e9);
            bool narrow=w<900,compactHeader=w<700;Gdiplus::RectF iconRect{compactHeader?20.f:28.f,compactHeader?9.f:19.f,compactHeader?30.f:46.f,compactHeader?30.f:46.f};if(productIcon){Gdiplus::GraphicsPath clip;win::roundedPath(clip,iconRect,11);auto state=g.Save();g.SetClip(&clip);g.DrawImage(productIcon.get(),iconRect);g.Restore(state);}
            win::text(g,displayProductName(product->slug,product->name),{compactHeader?60.f:90.f,compactHeader?10.f:27.f,compactHeader?std::max(110.f,w-280):(narrow?200.f:280.f),32},compactHeader?17.f:narrow?18.f:22.f,0x17333c,true);
            win::text(g,productSubtitle(product->slug),{28,height-27,190,18},10,0x6b959e,true);
            win::text(g,callbacks.view->advanced?tr(L"高级视图",L"Advanced view"):tr(L"简易视图",L"Simple view"),{w/2-70,height-27,140,18},10,0x758b93,false,Gdiplus::StringAlignmentCenter);
            if(!content)win::text(g,tr(L"此模块的原生内容不可用",L"Native content is unavailable for this module"),{28,120,w-56,80},12,0x17333c,false,Gdiplus::StringAlignmentCenter);
        }else if(h==overlay){
            // Render only our owned editor beneath the dialog. No desktop or
            // host screenshot: custom WM_PRINTCLIENT follows the live widgets.
            HDC background=p.dc();SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(background),PRF_CLIENT);
            for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(child==overlay || !IsWindowVisible(child))continue;RECT r{};GetWindowRect(child,&r);POINT at{r.left,r.top};ScreenToClient(window,&at);int saved=SaveDC(background);SetViewportOrgEx(background,at.x,at.y,nullptr);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(background),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);RestoreDC(background,saved);}
            win::fill(g,{0,0,w,height},0x13323c,0,46);
        }
        else if(h==panel){win::fill(g,{0,0,w,height},0xd1dfe3);win::fill(g,{0,0,w,height},0xf7fbfc,16);}
        else if(h==body){win::fill(g,{0,0,w,height},0xf7fbfc);g.TranslateTransform(0,float(-scroll));
            if(modalTab==1){if(productIcon)g.DrawImage(productIcon.get(),Gdiplus::RectF{(w-84)/2,18,84,84});win::text(g,displayProductName(product->slug,product->name),{0,120,w,28},22,0x17333c,false,Gdiplus::StringAlignmentCenter);win::text(g,uiVersion,{0,154,w,28},12,0x5d818e,false,Gdiplus::StringAlignmentCenter);win::text(g,std::wstring(tr(L"作者 ",L"Created by "))+win::wide(uiAuthor),{0,188,w,28},12,0x5d818e,false,Gdiplus::StringAlignmentCenter);win::text(g,uiContact,{0,222,w,28},12,0x5d818e,false,Gdiplus::StringAlignmentCenter);
            }else if(modalTab==0){win::text(g,tr(L"界面缩放",L"Interface scale"),{10,10,w-170,26});win::text(g,tr(L"语言",L"Language"),{10,58,w-170,26});win::text(g,tr(L"低性能模式",L"Low performance"),{10,106,w-170,26});win::text(g,uiVersion,{10,253,w-20,22});auto status=callbacks.readStatus?statusText(callbacks.readStatus(callbacks.owner),callbacks.getBypass(callbacks.owner)):std::string();auto info=win::wide(status)+L"\n"+(scaleError.empty()?tr(L"界面偏好随宿主保存。",L"UI preferences save with the host."):scaleError);win::text(g,info,{10,286,w-20,96},11,0x758b93);
            }else{win::text(g,tr(L"工厂预设只读；用户预设仅属于当前插件。",L"Factory presets are read-only. User presets belong to this plugin."),{10,8,w-20,28},11);win::text(g,tr(L"名称",L"Name"),{10,86,62,28});if(!pendingDelete.empty())win::text(g,tr(L"确定删除所选用户预设？当前声音不会改变。",L"Delete the selected user preset? The current sound stays unchanged."),{10,178,w-20,44},11);auto message=presetMessage.empty()?std::wstring(tr(L"保存完整声音状态；载入保留当前旁路。界面偏好不在预设中。",L"Save the complete sound state. Loading preserves current bypass. UI preferences are excluded.")):presetMessage;win::text(g,message,{10,pendingDelete.empty()?180.f:270.f,w-20,96},11,0x5d818e);}
        }else win::fill(g,{0,0,w,height},0xf5fafb);
    }
    void paintButton(HWND h,HDC dc=nullptr){bool bypassButton=GetDlgCtrlID(h)==bypassID;win::Paint p(h,!bypassButton,dc);auto& g=p.graphics();float w=p.width(),height=p.height();int id=GetDlgCtrlID(h);bool on=(id==bypassID && callbacks.getBypass(callbacks.owner)) || (id==advancedID && callbacks.view->advanced) || (id==lowID && callbacks.view->lowPerformance);if(id==lowID){
            // Preserve the frozen Mac checkbox + ON/OFF structure while using
            // the same Windows drawing, scaling and bypass grayscale surface.
            win::fill(g,{0,0,w,height},0xf7fbfc);float top=(height-14.f)/2.f;
            win::fill(g,{2,top,14,14},on?0x18b5c3:0xffffff,3);win::stroke(g,{2,top,14,14},on?0x008b9e:0xabcbd3,3);
            if(on){win::line(g,5,top+7,8,top+10,0xffffff,1.8f);win::line(g,8,top+10,13,top+4,0xffffff,1.8f);}
            win::text(g,win::windowText(h),{24,0,w-26,height},12,IsWindowEnabled(h)?0x17333c:0x99adb4);
            if(GetFocus()==h)win::stroke(g,{.5f,.5f,std::max(1.f,w-1),std::max(1.f,height-1)},0x74b9c3,3);return;
        }bool down=(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)!=0;unsigned fillColor=bypassButton && on?0xd83e48:on?0xd8f5f5:down?0xe8f2f5:0xffffff;win::fill(g,{0,0,w,height},(GetParent(h)==body || GetParent(h)==panel)?0xf7fbfc:0xfbfdfe);win::fill(g,{.5f,.5f,std::max(1.f,w-1),std::max(1.f,height-1)},fillColor,8);if(!bypassButton)win::stroke(g,{.5f,.5f,std::max(1.f,w-1),std::max(1.f,height-1)},0xd5e2e7,8);win::text(g,win::windowText(h),{3,0,w-6,height},id==settingsID?20.f:12.f,!IsWindowEnabled(h)?0x99adb4:bypassButton && on?0xffffff:0x17333c,false,Gdiplus::StringAlignmentCenter);if(GetFocus()==h)win::stroke(g,{2,2,std::max(1.f,w-4),std::max(1.f,height-4)},0x74b9c3,6);}
    void endAudition(){const auto& s=callbacks.services;if(s.readAuditionStatus && s.endAudition){auto a=s.readAuditionStatus(s.owner);if(a.token)s.endAudition(s.owner,a.token);}}
    void refresh(){if(!window || destroying)return;EnumChildWindows(window,prepareNativeGray,reinterpret_cast<LPARAM>(bypass));const auto& s=callbacks.services;auto transaction=s.readPresetTransaction?s.readPresetTransaction(s.owner):PresetTransactionStatus::unavailable;EnableWindow(preset,transaction!=PresetTransactionStatus::pending);if(GetForegroundWindow()!=GetAncestor(window,GA_ROOT))endAudition();bool pause=callbacks.getBypass(callbacks.owner);
        if(!presentationInitialized || lastPaused!=pause){if(callbacks.view->visualsPaused && !pause)++callbacks.view->visualResumeGeneration;callbacks.view->visualsPaused=pause;if(callbacks.setVisualsPaused)callbacks.setVisualsPaused(callbacks.owner,pause);if(content)content->setVisualsPaused(pause);presentationInitialized=true;lastPaused=pause;invalidate();}
        labelButton(preset,transaction==PresetTransactionStatus::pending?tr(L"等待预设…",L"Pending…"):transaction==PresetTransactionStatus::rejected?tr(L"重试预设",L"Retry preset"):tr(L"预设 ▾",L"Preset ▾"));labelButton(bypass,tr(L"● 旁路",L"● Bypass"));labelButton(advanced,callbacks.view->advanced?tr(L"简易",L"Simple"):tr(L"高级",L"Advanced"));labelButton(about,tr(L"关于",L"About"));SendMessageW(bypass,BM_SETCHECK,pause?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(advanced,BM_SETCHECK,callbacks.view->advanced?BST_CHECKED:BST_UNCHECKED,0);
        auto snapshot=callbacks.readStatus?callbacks.readStatus(callbacks.owner):StatusSnapshot{};if(content)content->refresh(*callbacks.view,snapshot);
        if(waiting && (transaction==PresetTransactionStatus::applied || transaction==PresetTransactionStatus::rejected)){waiting=false;presetMessage=transaction==PresetTransactionStatus::applied?tr(L"预设已载入。",L"Preset loaded."):tr(L"载入被拒绝；当前声音保留。",L"Load rejected; current sound preserved.");}
        int rate=callbacks.view->lowPerformance?67:33;if(rate!=timerMilliseconds){timerMilliseconds=rate;SetTimer(window,1,rate,nullptr);}if(body){if(HWND low=GetDlgItem(body,lowID))SendMessageW(low,BM_SETCHECK,callbacks.view->lowPerformance?BST_CHECKED:BST_UNCHECKED,0);InvalidateRect(body,nullptr,FALSE);}if(overlay)InvalidateRect(overlay,nullptr,FALSE);
    }
    void fonts(){HFONT next=CreateFontW(-int(std::lround(12*win::scale(window))),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(HWND h:controls)if(IsWindow(h))SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;}
    void layout(){double s=win::scale(window);RECT bounds{};GetClientRect(window,&bounds);logicalWidth=int(std::lround(bounds.right/s));logicalHeight=int(std::lround(bounds.bottom/s));int w=logicalWidth,h=logicalHeight;bool narrow=w<900;
        int px=narrow?300:390;win::place(preset,px,28,std::max(66,std::min(150,w-px-270)),30);win::place(bypass,w-250,29,84,26);win::place(advanced,w-164,26,92,32);win::place(settings,w-65,26,38,32);
        if(w<700){win::place(preset,w-180,9,110,28);win::place(settings,w-59,9,38,28);win::place(bypass,w-228,49,84,26);win::place(advanced,w-124,46,96,30);}win::place(about,w-100,h-33,76,26);
        if(content){win::place(contentParent,0,84,w,std::max(1,h-124));content->resize(w,std::max(1,h-124));}
        if(overlay){win::place(overlay,0,0,w,h);int pw=std::min(472,w-24),ph=std::min(modalTab==1?398:476,h-24);win::place(panel,(w-pw)/2,(h-ph)/2,pw,ph);win::place(body,8,58,pw-16,std::max(1,ph-70));win::place(GetDlgItem(panel,settingsTabID),24,18,84,30);win::place(GetDlgItem(panel,aboutTabID),112,18,84,30);win::place(GetDlgItem(panel,presetsTabID),200,18,84,30);win::place(GetDlgItem(panel,closeID),pw-60,18,36,30);layoutBody();}fonts();invalidate();
    }
    void layoutBody(){if(!body)return;auto b=win::size(body);int doc=modalTab==1?300:394;scroll=std::clamp(scroll,0,std::max(0,doc-int(b.Height)));SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,doc-1,UINT(std::max(1.f,b.Height)),scroll,0};SetScrollInfo(body,SB_VERT,&si,TRUE);float w=win::size(body).Width;auto place=[&](int id,double x,double y,double width,double height){auto c=GetDlgItem(body,id);if(c)win::place(c,x,y-scroll,width,height);};
        if(modalTab==0){place(scaleID,w-174,8,164,220);place(languageID,w-174,56,164,180);place(lowID,w-94,106,84,24);}else if(modalTab==2){place(selectionID,10,42,w-20,220);place(nameID,78,86,w-88,28);double ratio=(w-20)/414;place(saveID,10,132,126*ratio,30);place(loadID,10+136*ratio,132,80*ratio,30);place(renameID,10+226*ratio,132,88*ratio,30);place(deleteID,10+324*ratio,132,90*ratio,30);place(confirmID,10,226,142,30);place(cancelID,162,226,100,30);}InvalidateRect(body,nullptr,FALSE);}
    void closeModal(){++modalGeneration;if(name && IsWindow(name))draft=win::windowText(name);if(overlay)DestroyWindow(overlay);overlay=panel=body=scaleMenu=languageMenu=managed=name=nullptr;controls.clear();SetFocus(window);invalidate();}
    void buildModal(int tab,bool keepDraft=true){if(rebuilding)return;++modalGeneration;rebuilding=true;if(!keepDraft)draft.clear();else if(name)draft=win::windowText(name);if(overlay)DestroyWindow(overlay);overlay=panel=body=scaleMenu=languageMenu=managed=name=nullptr;controls.clear();modalTab=tab;scroll=0;
        overlay=CreateWindowExW(0,L"JUST.Shared.Surface.v3",L"JUST dialog",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,10,10,window,reinterpret_cast<HMENU>(300),module,this);panel=CreateWindowExW(0,L"JUST.Shared.Surface.v3",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,10,10,overlay,reinterpret_cast<HMENU>(301),module,this);body=CreateWindowExW(0,L"JUST.Shared.Surface.v3",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_VSCROLL,0,0,10,10,panel,reinterpret_cast<HMENU>(302),module,this);
        button(panel,tr(L"设置",L"Settings"),settingsTabID);button(panel,tr(L"关于",L"About"),aboutTabID);button(panel,tr(L"预设",L"Presets"),presetsTabID);button(panel,L"×",closeID);
        if(tab==0){scaleMenu=child(body,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,scaleID);for(int n:{75,100,125,150}){auto text=std::to_wstring(n)+L"%";int index=int(SendMessageW(scaleMenu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())));SendMessageW(scaleMenu,CB_SETITEMDATA,index,n);if(n==int(std::lround(callbacks.view->renderScale*100)))SendMessageW(scaleMenu,CB_SETCURSEL,index,0);}controls.push_back(scaleMenu);languageMenu=child(body,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,languageID);SendMessageW(languageMenu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"中文"));SendMessageW(languageMenu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));SendMessageW(languageMenu,CB_SETCURSEL,callbacks.view->language==UiLanguage::english?1:0,0);controls.push_back(languageMenu);button(body,callbacks.view->lowPerformance?L"ON":L"OFF",lowID);
        }else if(tab==2){choices.clear();managed=child(body,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,selectionID);unsigned count=effectiveFactoryPresetCount(moduleDefinition());for(unsigned i=0;i<count;++i){choices.push_back({"factory:"+std::to_string(i),""});auto title=std::wstring(tr(L"工厂 · ",L"Factory · "))+factoryTitle(i);SendMessageW(managed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));}auto list=store->list();for(auto& entry:list.presets){choices.push_back({"user:"+entry.id,entry.name});auto title=std::wstring(tr(L"用户 · ",L"User · "))+win::wide(entry.name);SendMessageW(managed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));}int selected=0;for(std::size_t i=0;i<choices.size();++i)if(choices[i].first==selection)selected=int(i);if(!choices.empty())selection=choices[std::size_t(selected)].first;else selection.clear();SendMessageW(managed,CB_SETCURSEL,selected,0);controls.push_back(managed);
            name=child(body,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,nameID);SendMessageW(name,EM_SETLIMITTEXT,240,0);if(draft.empty())draft=isUser() && !choices.empty()?win::wide(choices[std::size_t(selected)].second):tr(L"我的预设",L"My preset");SetWindowTextW(name,draft.c_str());controls.push_back(name);
            button(body,tr(L"保存为新预设",L"Save new"),saveID);button(body,tr(L"载入",L"Load"),loadID);EnableWindow(button(body,tr(L"重命名",L"Rename"),renameID),isUser());EnableWindow(button(body,tr(L"删除…",L"Delete…"),deleteID),isUser());if(!pendingDelete.empty()){button(body,tr(L"确认删除",L"Confirm delete"),confirmID);button(body,tr(L"取消",L"Cancel"),cancelID);}if(!list.error.empty())presetMessage+=L"\n"+win::wide(list.error);if(list.unreadable)presetMessage+=L"\n"+std::to_wstring(list.unreadable)+tr(L" 个无法读取的文件已保留。",L" unreadable files were left untouched.");
        }SetWindowPos(overlay,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);rebuilding=false;layout();SetFocus(GetDlgItem(panel,closeID));refresh();}
    bool apply(SoundState& desired){const auto& m=moduleDefinition();const auto& s=callbacks.services;auto bypassIndex=m.parameters.index(bypassParamID);if(bypassIndex>=m.parameters.count)return false;desired.targets[bypassIndex]=callbacks.getBypass(callbacks.owner)?1:0;bool valid=validCompleteState(desired,product->processor,m.parameters) && (!m.validatePresetState || m.validatePresetState(desired));bool ok=valid && s.requestApplySoundState && s.requestApplySoundState(s.owner,desired);waiting=ok;presetMessage=ok?tr(L"已请求载入；等待音频事务确认。",L"Load requested; awaiting audio confirmation."):tr(L"无法载入：状态未连接、正在变化，或配置未获模块支持。",L"Load unavailable: disconnected, changing state, or unsupported configuration.");refresh();return ok;}
    void loadSelection(){if(isUser()){UserPreset preset;std::string error;if(store->load(selection.substr(5),preset,error))apply(preset.state);else presetMessage=win::wide(error);}else if(selection.compare(0,8,"factory:")==0){SoundState desired;try{unsigned index=unsigned(std::stoul(selection.substr(8)));if(buildFactoryPreset(moduleDefinition(),index,product->processor,desired))apply(desired);}catch(const std::exception&){presetMessage=tr(L"无效预设选择。",L"Invalid preset selection.");}}}
    void presetMenu(){HMENU menu=CreatePopupMenu();std::vector<std::string> ids;for(unsigned i=0;i<effectiveFactoryPresetCount(moduleDefinition());++i){AppendMenuW(menu,MF_STRING,1000+ids.size(),factoryTitle(i).c_str());ids.push_back("factory:"+std::to_string(i));}auto list=store->list();if(!list.presets.empty())AppendMenuW(menu,MF_SEPARATOR,0,nullptr);for(auto& p:list.presets){auto title=std::wstring(tr(L"用户 · ",L"User · "))+win::wide(p.name);AppendMenuW(menu,MF_STRING,1000+ids.size(),title.c_str());ids.push_back("user:"+p.id);}AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,100,tr(L"管理预设…",L"Manage presets…"));auto& s=callbacks.services;AppendMenuW(menu,MF_STRING|((s.canUndoLastPreset && s.canUndoLastPreset(s.owner))?0:MF_GRAYED),101,tr(L"撤销上次预设",L"Undo last preset"));RECT r{};GetWindowRect(preset,&r);UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,r.left,r.bottom,0,window,nullptr);DestroyMenu(menu);if(selected==100){pendingDelete.clear();buildModal(2);}else if(selected==101){if(s.undoLastPreset)s.undoLastPreset(s.owner);}else if(selected>=1000 && selected<1000+ids.size()){selection=ids[selected-1000];loadSelection();if(!waiting)buildModal(2,false);}refresh();}
    void command(int id,int notification){
        if(rebuilding || destroying)return;
        // BN_CLICKED is still inside BUTTON input dispatch (including BM_CLICK).
        // Rebuilding its parent here can free native state before that dispatch
        // returns. Queue destructive modal actions on the surviving editor HWND.
        if(notification==BN_CLICKED && (id==settingsID || id==aboutID || id==settingsTabID || id==aboutTabID || id==presetsTabID || id==closeID || id==lowID || id==loadID || id==saveID || id==renameID || id==deleteID || id==confirmID || id==cancelID)){
            PostMessageW(window,deferredModalCommand,WPARAM(id),LPARAM(modalGeneration));return;
        }
        commandNow(id,notification);
    }
    void commandNow(int id,int notification){if(rebuilding || destroying)return;const auto& s=callbacks.services;
        if(id==presetID){presetMenu();return;}if(id==bypassID){callbacks.setBypass(callbacks.owner,!callbacks.getBypass(callbacks.owner));refresh();invalidate();return;}if(id==advancedID){callbacks.view->advanced=!callbacks.view->advanced;refresh();layout();return;}if(id==settingsID || id==settingsTabID){buildModal(0);return;}if(id==aboutID || id==aboutTabID){buildModal(1);return;}if(id==presetsTabID){pendingDelete.clear();buildModal(2);return;}if(id==closeID){closeModal();return;}
        if(id==scaleID && notification==CBN_SELCHANGE){int index=int(SendMessageW(scaleMenu,CB_GETCURSEL,0,0));int percent=int(SendMessageW(scaleMenu,CB_GETITEMDATA,index,0));if(percent<75 || percent>150)return;if(!callbacks.requestScale || !callbacks.requestScale(callbacks.resizeOwner,percent/100.))scaleError=tr(L"无法应用此缩放，已保持原尺寸。",L"Scale could not be applied; size is unchanged.");else scaleError.clear();
            // CBN_SELCHANGE runs inside the native combo's input dispatch. Keep
            // its HWND (and parent modal) alive until that dispatch returns.
            // Rebuild-free updates also preserve the open list and keyboard focus.
            int accepted=int(std::lround(callbacks.view->renderScale*100));
            for(int n=0;n<SendMessageW(scaleMenu,CB_GETCOUNT,0,0);++n)if(SendMessageW(scaleMenu,CB_GETITEMDATA,n,0)==accepted){SendMessageW(scaleMenu,CB_SETCURSEL,n,0);break;}
            layout();return;}
        if(id==languageID && notification==CBN_SELCHANGE){callbacks.view->language=SendMessageW(languageMenu,CB_GETCURSEL,0,0)==1?UiLanguage::english:UiLanguage::chinese;
            labelButton(GetDlgItem(panel,settingsTabID),tr(L"设置",L"Settings"));labelButton(GetDlgItem(panel,aboutTabID),tr(L"关于",L"About"));labelButton(GetDlgItem(panel,presetsTabID),tr(L"预设",L"Presets"));
            refresh();invalidate();return;}
        if(id==lowID){callbacks.view->lowPerformance=!callbacks.view->lowPerformance;refresh();buildModal(0);return;}
        if(id==selectionID && notification==CBN_SELCHANGE){int i=int(SendMessageW(managed,CB_GETCURSEL,0,0));if(i<0 || std::size_t(i)>=choices.size())return;selection=choices[std::size_t(i)].first;pendingDelete.clear();
            draft=isUser()?win::wide(choices[std::size_t(i)].second):tr(L"我的预设",L"My preset");SetWindowTextW(name,draft.c_str());
            EnableWindow(GetDlgItem(body,renameID),isUser());EnableWindow(GetDlgItem(body,deleteID),isUser());
            // Remove only the obsolete confirmation buttons, never the sender.
            for(int buttonID:{confirmID,cancelID})if(HWND button=GetDlgItem(body,buttonID))DestroyWindow(button);
            layoutBody();invalidate();return;}
        if(id==loadID){loadSelection();buildModal(2);return;}if(id==saveID){SoundState state;std::string key,error;if(!s.capturePresetState || !s.capturePresetState(s.owner,state))presetMessage=tr(L"声音状态尚未同步，请稍后再保存。",L"Sound state is not synchronized yet. Please retry shortly.");else if(store->save(win::utf8(win::windowText(name)),state,key,error)){selection="user:"+key;presetMessage=tr(L"用户预设已保存。",L"User preset saved.");}else presetMessage=win::wide(error);buildModal(2);return;}
        if(id==renameID && isUser()){std::string error;bool ok=store->rename(selection.substr(5),win::utf8(win::windowText(name)),error);presetMessage=ok?tr(L"预设已重命名。",L"Preset renamed."):win::wide(error);pendingDelete.clear();buildModal(2);return;}if(id==deleteID && isUser()){pendingDelete=selection;buildModal(2);return;}if(id==cancelID){pendingDelete.clear();buildModal(2);return;}if(id==confirmID && isUser() && pendingDelete==selection){std::string error;bool ok=store->remove(selection.substr(5),error);presetMessage=ok?tr(L"用户预设已删除。",L"User preset deleted."):win::wide(error);pendingDelete.clear();if(ok){selection.clear();draft.clear();}buildModal(2,!ok);return;}
    }
};
bool queueModalEscape(HWND h){
    HWND root=modalEditor(h);if(!root)return false;
    // First Escape retains the native behavior of dismissing an open list.
    wchar_t klass[32]{};GetClassNameW(h,klass,32);
    if(!lstrcmpiW(klass,L"COMBOBOX") && SendMessageW(h,CB_GETDROPPEDSTATE,0,0))return false;
    auto* v=reinterpret_cast<NativeView*>(GetWindowLongPtrW(root,GWLP_USERDATA));
    if(!v || v->destroying || !v->overlay)return false;
    v->command(closeID,BN_CLICKED);return true; // existing generation-checked queue
}
LRESULT CALLBACK buttonProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){if(m==WM_GETDLGCODE && wantsModalEscape(h,w,l))return DefSubclassProc(h,m,w,l)|DLGC_WANTMESSAGE;auto* v=reinterpret_cast<NativeView*>(data);if(m==WM_PAINT){v->paintButton(h);return 0;}if(m==WM_PRINTCLIENT){v->paintButton(h,reinterpret_cast<HDC>(w));return 0;}if(m==WM_ERASEBKGND)return 1;if(m==WM_KEYDOWN && w==VK_ESCAPE && v->overlay){v->command(closeID,BN_CLICKED);return 0;}if(m==WM_NCDESTROY)RemoveWindowSubclass(h,buttonProc,1);return DefSubclassProc(h,m,w,l);}
LRESULT CALLBACK surfaceProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_GETDLGCODE && wantsModalEscape(h,w,l))return DefWindowProcW(h,m,w,l)|DLGC_WANTMESSAGE;auto* v=reinterpret_cast<NativeView*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){v=static_cast<NativeView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}if(!v)return DefWindowProcW(h,m,w,l);if(m==WM_NCDESTROY){SetWindowLongPtrW(h,GWLP_USERDATA,0);return DefWindowProcW(h,m,w,l);}if(m==WM_PAINT){v->paint(h);return 0;}if(m==WM_PRINTCLIENT){v->paint(h,reinterpret_cast<HDC>(w));return 0;}if(m==WM_ERASEBKGND)return 1;if(m==WM_COMMAND){v->command(LOWORD(w),HIWORD(w));return 0;}if(m==WM_KEYDOWN && w==VK_ESCAPE){v->command(closeID,BN_CLICKED);return 0;}if(h==v->body && (m==WM_VSCROLL || m==WM_MOUSEWHEEL)){SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,SB_VERT,&si);if(m==WM_MOUSEWHEEL)v->scroll-=GET_WHEEL_DELTA_WPARAM(w)/3;else switch(LOWORD(w)){case SB_THUMBTRACK:v->scroll=si.nTrackPos;break;case SB_LINEUP:v->scroll-=30;break;case SB_LINEDOWN:v->scroll+=30;break;case SB_PAGEUP:v->scroll-=int(si.nPage);break;case SB_PAGEDOWN:v->scroll+=int(si.nPage);break;}v->layoutBody();return 0;}return DefWindowProcW(h,m,w,l);}
LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_GETDLGCODE && wantsModalEscape(h,w,l))return DefWindowProcW(h,m,w,l)|DLGC_WANTMESSAGE;auto* v=reinterpret_cast<NativeView*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){v=static_cast<NativeView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);v->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}if(!v)return DefWindowProcW(h,m,w,l);switch(m){case WM_NCDESTROY:RemovePropW(h,win::viewProperty);SetWindowLongPtrW(h,GWLP_USERDATA,0);if(v->window==h){v->window=nullptr;v->tooltip=nullptr;}return DefWindowProcW(h,m,w,l);case WM_PAINT:v->paint(h);return 0;case WM_PRINTCLIENT:v->paint(h,reinterpret_cast<HDC>(w));return 0;case WM_ERASEBKGND:return 1;case WM_COMMAND:v->command(LOWORD(w),HIWORD(w));return 0;case deferredModalCommand:if(ULONG_PTR(l)==v->modalGeneration)v->commandNow(int(w),BN_CLICKED);return 0;case WM_TIMER:v->refresh();return 0;case WM_KEYDOWN:if(w==VK_ESCAPE && v->overlay){v->command(closeID,BN_CLICKED);return 0;}break;case WM_ACTIVATEAPP:if(!w)v->endAudition();break;}return DefWindowProcW(h,m,w,l);}
}
void* createNativeEditor(void* parent,const PluginIdentity& p,EditorCallbacks c){if(!parent || !c.view || !c.getBypass || !c.setBypass)return nullptr;auto v=std::make_unique<NativeView>();v->product=&p;v->callbacks=c;v->module=win::moduleAt(reinterpret_cast<const void*>(&windowProc));win::startup();INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES};InitCommonControlsEx(&common);WNDCLASSW klass{};klass.lpfnWndProc=windowProc;klass.hInstance=v->module;klass.lpszClassName=L"JUST.Shared.Editor.v3";klass.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!v->editorClass.acquire(klass))return nullptr;klass.lpfnWndProc=surfaceProc;klass.lpszClassName=L"JUST.Shared.Surface.v3";if(!v->surfaceClass.acquire(klass))return nullptr;
    v->window=CreateWindowExW(0,L"JUST.Shared.Editor.v3",win::wide(displayProductName(p.slug,p.name)).c_str(),WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,c.view->width,c.view->height,static_cast<HWND>(parent),nullptr,v->module,v.get());if(!v->window)return nullptr;if(!SetPropW(v->window,win::viewProperty,reinterpret_cast<HANDLE>(c.view)))return nullptr;v->productIcon=win::icon(v->module,p.slug);
    v->preset=v->button(v->window,L"Preset ▾",presetID);v->bypass=v->button(v->window,L"● Bypass",bypassID);v->advanced=v->button(v->window,L"Advanced",advancedID);v->settings=v->button(v->window,L"⚙",settingsID);v->about=v->button(v->window,L"About",aboutID);if(!v->preset || !v->bypass || !v->advanced || !v->settings || !v->about)return nullptr;
    std::filesystem::path root;if(c.presetDirectoryOverride)root=std::filesystem::u8path(c.presetDirectoryOverride);else{wchar_t isolated[32768]{};DWORD length=GetEnvironmentVariableW(L"JUST_USER_PRESET_ROOT",isolated,32768);if(length>0 && length<32768)root=std::filesystem::path(isolated);else{PWSTR roaming=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming)) && roaming){root=std::filesystem::path(roaming)/L"JUST"/L"Presets"/L"v1";CoTaskMemFree(roaming);}}}v->store=std::make_unique<UserPresetStore>(root,p.processor,moduleDefinition().parameters);
    v->bypassHelp=win::wide(moduleBypassTooltip(moduleDefinition()));v->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,v->window,nullptr,v->module,nullptr);if(v->tooltip){TOOLINFOW info{};info.cbSize=sizeof(info);info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=v->window;info.uId=reinterpret_cast<UINT_PTR>(v->bypass);info.lpszText=v->bypassHelp.data();SendMessageW(v->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(v->tooltip,TTM_SETMAXTIPWIDTH,0,420);}
    if(auto factory=moduleDefinition().createEditorContent){v->contentParent=CreateWindowExW(0,L"JUST.Shared.Surface.v3",L"JUST module",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,84,10,10,v->window,nullptr,v->module,v.get());if(!v->contentParent)return nullptr;v->content.reset(factory());if(!v->content || !v->content->attach(v->contentParent,c.services)){v->content.reset();DestroyWindow(v->contentParent);v->contentParent=nullptr;}}
    v->layout();v->refresh();SetTimer(v->window,1,v->timerMilliseconds,nullptr);return v.release();}
void destroyNativeEditor(void* handle){delete static_cast<NativeView*>(handle);}
void resizeNativeEditor(void* handle,int width,int height){if(!handle)return;auto* v=static_cast<NativeView*>(handle);if(!v->window)return;MoveWindow(v->window,0,0,width,height,TRUE);v->layout();}
}
