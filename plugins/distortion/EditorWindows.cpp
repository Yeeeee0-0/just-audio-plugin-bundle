// Frozen Mac presentation contract on Win32. Native Windows acceptance is separate.
#include "EditorModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <map>
#include <mutex>
#include <vector>
namespace just::distortion {
class WindowsContent final:public EditorContent {
    // One registration lease per editor instance, scoped to this module DLL.
    // The last lease is released only after its complete HWND subtree is gone.
    inline static std::mutex classMutex;
    inline static unsigned classUsers=0;
    bool classAcquired=false;
    bool acquireClass(const WNDCLASSW& registration) {
        std::lock_guard<std::mutex> lock(classMutex);
        if(classAcquired || !registration.hInstance)return false;
        if(classUsers==0 && !RegisterClassW(&registration))return false;
        ++classUsers;classAcquired=true;return true;
    }
    void releaseClass() {
        std::lock_guard<std::mutex> lock(classMutex);
        if(!classAcquired)return;
        classAcquired=false;
        if(--classUsers==0)UnregisterClassW(className,moduleHandle);
    }
    EditorModel editor;HWND window=nullptr,modelMenu=nullptr,modelLabel=nullptr,modelNote=nullptr,holdLabel=nullptr,holdValue=nullptr,notice=nullptr,pendingNotice=nullptr,advancedTitle=nullptr,tooltip=nullptr;
    HINSTANCE moduleHandle=nullptr;HFONT font=nullptr,smallFont=nullptr;HBRUSH background=nullptr;double fontScale=0;bool advanced=false,holdTyping=false,holdCancelled=false,zh=false,gray=false;int scroll=0,width=1120,height=460;
    TextEditSession holdEdit;AnalysisCursor analysisCursor;AnalysisWindow measured{};AnalysisAvailability availability=AnalysisAvailability::unavailable;bool haveMeasurement=false;std::uint64_t resumeGeneration=0;
    struct Rotary{ID id;std::unique_ptr<RotaryControl> control;};std::vector<Rotary> simple,rotaries;
    struct Menu{ID id;HWND label,control;};std::vector<Menu> menus;
    std::map<HWND,std::wstring> tips;std::unique_ptr<AnalysisView> feedback;
    const char* local(const char* cn,const char* en)const{return zh?cn:en;}
    static constexpr wchar_t className[]=L"JustDistortionContent.v2";
    static HWND child(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style){return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,nullptr,reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE)),nullptr);}
    static void text(HWND w,const std::string& value){SetWindowTextW(w,win::wide(value).c_str());}
    void tip(HWND h,const std::string& value){if(!tooltip || !h)return;const bool exists=tips.count(h);tips[h]=win::wide(value);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(h);info.lpszText=tips[h].data();SendMessageW(tooltip,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
    void updateFont(){double scale=win::scale(window);if(font && fontScale==scale)return;fontScale=scale;auto next=CreateFontW(-int(std::lround(13*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto small=CreateFontW(-int(std::lround(11*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(auto h:{modelLabel,modelMenu,holdValue,advancedTitle})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto h:{modelNote,holdLabel,notice,pendingNotice})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);for(auto& m:menus)for(auto h:{m.label,m.control})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);font=next;smallFont=small;}
    static LRESULT CALLBACK holdProcedure(HWND w,UINT msg,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR data){auto* p=reinterpret_cast<WindowsContent*>(data);
        if(msg==WM_KEYDOWN && (a==VK_RETURN || a==VK_ESCAPE)){p->holdCancelled=a==VK_ESCAPE;SetFocus(p->window);return 0;}return DefSubclassProc(w,msg,a,b);}
    void commitHold(){if(!holdTyping || holdCancelled || editor.currentModel()!=Model::Crush)return;double next;const auto value=win::utf8(win::windowText(holdValue));if(holdEdit.changed(parameters[registry.index(crush_hold_hz)],{},value.c_str(),next))editor.write(crush_hold_hz,next);}
    bool createControls(){
        modelLabel=child(window,L"STATIC",L"Model",0);modelMenu=child(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP);for(auto id:modelDisplayOrder)SendMessageW(modelMenu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(modelLabels[id]).c_str()));modelNote=child(window,L"STATIC",L"",0);
        for(auto id:{drive_db,crush_bits,mix}){DisplayPolicy policy;policy.labelZh=parameterLabelZh(id);auto c=RotaryControl::create(window,editor.services,parameters[registry.index(id)],policy);if(!c)return false;simple.push_back({id,std::move(c)});}
        holdLabel=child(window,L"STATIC",L"Hold Rate",0);holdValue=child(window,L"EDIT",L"",ES_CENTER|ES_AUTOHSCROLL|WS_TABSTOP);SetWindowSubclass(holdValue,holdProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        feedback=AnalysisView::create(window,editor.services,AnalysisViewMode::waveform);if(!feedback)return false;
        // The readout is a sibling above the native analysis child, just like the Mac overlay.
        notice=child(window,L"STATIC",L"",SS_ENDELLIPSIS);SetWindowPos(notice,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        advancedTitle=child(window,L"STATIC",L"ADVANCED",0);pendingNotice=child(window,L"STATIC",L"",0);
        for(const auto& p:parameters){if(p.id==bypass || p.id==model || p.id==mix)continue;auto id=static_cast<ID>(p.id);
            if(p.enumLabels){Menu m{id,child(window,L"STATIC",L"",SS_CENTER),child(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP)};text(m.label,id==post_lp_enabled?"High Cut Enable":p.title);for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(m.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(p.enumLabels[n]).c_str()));EnableWindow(m.control,id!=quality);menus.push_back(m);}
            else{auto spec=p;if(id==post_lp_hz)spec.title="High Cut";DisplayPolicy policy;policy.labelZh=parameterLabelZh(id);auto c=RotaryControl::create(window,editor.services,spec,policy);if(!c)return false;rotaries.push_back({id,std::move(c)});}
        }return true;
    }
    unsigned columns()const{return std::max(3,(width-52)/144);}
    int panelHeight()const{return 52+int((rotaries.size()+menus.size()+columns()-1)/columns())*144+56;}
    int maximumScroll()const{return std::max(0,std::max(460,height)+(advanced?panelHeight():0)-height);}
    void layout(){if(!window || !feedback)return;updateFont();scroll=std::clamp(scroll,0,maximumScroll());const int w=width,mainHeight=std::max(460,height),margin=std::max(24,w*36/1120),graphHeight=mainHeight-231,y=mainHeight-198-scroll;
        const int gap=std::max(42,std::min(89,(w-428)/2)),left=(w-(428+2*gap))/2;
        win::place(modelLabel,left,y+40,164,22);win::place(modelMenu,left,y+67,164,220);win::place(modelNote,left,y+111,176,42);
        for(auto& c:simple){const int x=c.id==mix?left+164+2*gap+132:left+164+gap;c.control->resize(x,y,132,158);ShowWindow(static_cast<HWND>(c.control->nativeHandle()),c.id==drive_db?(editor.currentModel()==Model::Crush?SW_HIDE:SW_SHOW):c.id==crush_bits?(editor.currentModel()==Model::Crush?SW_SHOW:SW_HIDE):SW_SHOW);}
        const int hx=left+164+gap;win::place(holdLabel,hx-3,y+172,55,20);win::place(holdValue,hx+53,y+169,102,24);const bool crush=editor.currentModel()==Model::Crush;ShowWindow(holdLabel,crush?SW_SHOW:SW_HIDE);ShowWindow(holdValue,crush?SW_SHOW:SW_HIDE);
        feedback->resize(margin,26-scroll,w-2*margin,graphHeight);win::place(notice,margin+12,26+graphHeight-19-scroll,w-2*margin-24,17);
        const int cell=(w-52)/columns(),top=mainHeight+52-scroll;unsigned i=0;
        for(auto& c:rotaries){c.control->resize(26+(i%columns())*cell+(cell-98)/2,top+(i/columns())*144,98,114);ShowWindow(static_cast<HWND>(c.control->nativeHandle()),advanced?SW_SHOW:SW_HIDE);++i;}
        for(auto& m:menus){const int x=26+(i%columns())*cell,yy=top+(i/columns())*144;win::place(m.label,x,yy,cell,22);win::place(m.control,x+7,yy+52,cell-14,220);ShowWindow(m.label,advanced?SW_SHOW:SW_HIDE);ShowWindow(m.control,advanced?SW_SHOW:SW_HIDE);++i;}
        win::place(advancedTitle,26,mainHeight+18-scroll,w-52,20);win::place(pendingNotice,26,mainHeight+panelHeight()-50-scroll,w-52,40);ShowWindow(advancedTitle,advanced?SW_SHOW:SW_HIDE);ShowWindow(pendingNotice,advanced?SW_SHOW:SW_HIDE);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,mainHeight+(advanced?panelHeight():0)-1,UINT(height),scroll,0};SetScrollInfo(window,SB_VERT,&info,TRUE);ShowScrollBar(window,SB_VERT,advanced);InvalidateRect(window,nullptr,FALSE);
    }
    void command(HWND source,unsigned notification){if(source==holdValue){
            if(notification==EN_SETFOCUS){holdTyping=true;holdCancelled=false;text(holdValue,holdEdit.begin(parameters[registry.index(crush_hold_hz)],{},editor.normalized(crush_hold_hz)));SendMessageW(holdValue,EM_SETSEL,0,-1);}
            if(notification==EN_KILLFOCUS){commitHold();holdTyping=false;refresh(*editor.services.view,{});}return;}
        if(notification!=CBN_SELCHANGE)return;const auto index=SendMessageW(source,CB_GETCURSEL,0,0);
        if(source==modelMenu){if(index>=0 && index<std::size(modelDisplayOrder))editor.write(model,double(modelDisplayOrder[index])/5);}
        else for(auto& m:menus)if(source==m.control && m.id!=quality){const auto steps=parameters[registry.index(m.id)].stepCount;if(index>=0 && index<=steps)editor.write(m.id,double(index)/steps);}refresh(*editor.services.view,{});
    }
    static LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM a,LPARAM b){auto* p=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){p=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p){if(msg==WM_COMMAND){p->command(reinterpret_cast<HWND>(b),HIWORD(a));return 0;}
            if(msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX){auto dc=reinterpret_cast<HDC>(a);SetTextColor(dc,p->gray?RGB(51,51,51):RGB(23,51,60));SetBkColor(dc,p->gray?RGB(249,249,249):RGB(245,250,251));return reinterpret_cast<LRESULT>(p->background);}
            if(msg==WM_PAINT || msg==WM_PRINTCLIENT){win::Paint paint(w,true,msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(a):nullptr);return 0;}
            if(msg==WM_ERASEBKGND){RECT r;GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(a),&r,p->background);return 1;}
            if(msg==WM_VSCROLL || msg==WM_MOUSEWHEEL){int next=p->scroll;if(msg==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(a)/WHEEL_DELTA*40;else switch(LOWORD(a)){case SB_LINEUP:next-=34;break;case SB_LINEDOWN:next+=34;break;case SB_PAGEUP:next-=200;break;case SB_PAGEDOWN:next+=200;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&info);next=info.nTrackPos;break;}}p->scroll=std::clamp(next,0,p->maximumScroll());p->layout();return 0;}}
        return DefWindowProcW(w,msg,a,b);
    }
    void refreshMeasurement(const EditorViewState& view){
        if(resumeGeneration!=view.visualResumeGeneration){analysisCursor={};measured={};availability=AnalysisAvailability::unavailable;haveMeasurement=false;resumeGeneration=view.visualResumeGeneration;}
        if(!view.visualsPaused){AnalysisBatch batch;bool got=false;auto available=AnalysisAvailability::unavailable;for(unsigned i=0;i<16 && editor.services.readAnalysis;++i){available=editor.services.readAnalysis(editor.services.owner,analysisCursor,batch);if(available!=AnalysisAvailability::fresh || !batch.count)break;measured=batch.windows[batch.count-1];got=true;}if(got){haveMeasurement=true;availability=AnalysisAvailability::fresh;}else if(available!=AnalysisAvailability::fresh){availability=available;haveMeasurement=false;}}
        std::string status;char buffer[256];
        if(haveMeasurement){const auto& m=measured;const double peak=std::max(m.channels[2].peak,m.channels[3].peak);std::snprintf(buffer,sizeof(buffer),local("实测左声道波形 · PDC %u 采样 · 输出峰值 %.1f dBFS","Measured L waveform · PDC %u samples · Output peak %.1f dBFS"),m.header.latencySamples,20*std::log10(std::max(1e-8,peak)));status=buffer;if(peak>1)status+=local(" · 超出固定 ±1 显示范围"," · fixed ±1 display exceeded");if(m.header.flags&analysisBypassed)status+=local(" · 旁通"," · Bypass");else if((m.header.flags&analysisTransportKnown) && !(m.header.flags&analysisPlaying))status+=local(" · 宿主停止"," · Host stopped");if(std::max(m.channels[0].peak,m.channels[1].peak)==0)status+=peak>0?local(" · 尾音/输出持续"," · Tail/output active"):local(" · 静音"," · Silence");if(m.header.flags&analysisInvalid)status+=local(" · 输入无效"," · Invalid input");}
        else status=availability==AnalysisAvailability::stale?local("实测波形已过期","Measured waveform stale"):local("波形不可用 · 固定 PDC 32 采样","Waveform unavailable · fixed PDC 32 samples");if(editor.hiddenCustom())status+=local(" · 高级：自定义"," · Advanced: Custom");text(notice,status);tip(notice,status);
    }
public:
    ~WindowsContent()override{simple.clear();rotaries.clear();feedback.reset();if(holdValue)RemoveWindowSubclass(holdValue,holdProcedure,1);if(tooltip)DestroyWindow(tooltip);if(window)DestroyWindow(window);releaseClass();if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);if(background)DeleteObject(background);}
    bool attach(void* parent,const EditorServices& s)override{if(!parent || window || classAcquired || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;editor.services=s;moduleHandle=win::moduleAt(reinterpret_cast<const void*>(&procedure));background=CreateSolidBrush(RGB(245,250,251));
        WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=moduleHandle;c.lpszClassName=className;c.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!acquireClass(c))return false;
        window=CreateWindowExW(0,c.lpszClassName,L"JUST Distortion",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1120,460,static_cast<HWND>(parent),nullptr,c.hInstance,this);if(!window)return false;
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,moduleHandle,nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,600);if(!createControls())return false;refresh(*s.view,{});return true;}
    void resize(int w,int h)override{width=w;height=h;win::place(window,0,0,w,h);layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&)override{if(view.advanced!=advanced)scroll=0;advanced=view.advanced;zh=view.language==UiLanguage::chinese;if(gray!=view.visualsPaused){gray=view.visualsPaused;auto next=CreateSolidBrush(gray?RGB(249,249,249):RGB(245,250,251));DeleteObject(background);background=next;}
        for(unsigned i=0;i<std::size(modelDisplayOrder);++i)if(modelDisplayOrder[i]==unsigned(editor.currentModel()))SendMessageW(modelMenu,CB_SETCURSEL,i,0);
        text(modelLabel,local("模型","Model"));text(holdLabel,local("保持速率","Hold Rate"));text(advancedTitle,local("高级参数","ADVANCED"));text(modelNote,editor.currentModel()==Model::Clean?local("Clean 保留原信号，驱动暂不可调","Clean preserves the signal; Drive is inactive"):local("模型切换保留当前参数","Switching models preserves values"));
        for(auto& c:simple)c.control->refresh(c.id==mix || (c.id==drive_db?editor.currentModel()!=Model::Clean && editor.currentModel()!=Model::Crush:editor.currentModel()==Model::Crush));
        if(editor.currentModel()!=Model::Crush && holdTyping){holdCancelled=true;holdTyping=false;if(GetFocus()==holdValue)SetFocus(window);}for(auto& c:rotaries)c.control->refresh(advanced);
        for(auto& m:menus){SendMessageW(m.control,CB_SETCURSEL,int(editor.target(m.id)),0);text(m.label,zh?parameterLabelZh(m.id):(m.id==post_lp_enabled?"High Cut Enable":parameters[registry.index(m.id)].title));}if(!holdTyping)text(holdValue,editor.text(crush_hold_hz));
        tip(holdValue,local("实际 Crush 保持速率（Hz）；受运行采样率限制，但不改写已保存目标。","Actual Crush hold rate in Hz; capped to the running sample rate without rewriting its saved target."));
        char qualityText[384];std::snprintf(qualityText,sizeof(qualityText),local("品质：实际固定 4x · PDC 32 采样。已保存目标：%s%s","Quality: actual fixed 4x · PDC 32 samples. Saved target: %s%s"),qualityLabels[int(editor.target(quality))],editor.target(quality)!=2?local(" · 等待宿主 PDC 支持"," · pending host PDC support"):"");text(pendingNotice,qualityText);tip(pendingNotice,qualityText);
        feedback->refresh();refreshMeasurement(view);layout();}
};
EditorContent* createEditorContent(){return new WindowsContent;}
}
