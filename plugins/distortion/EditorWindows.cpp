// Native Win32 source parity; Windows build/runtime acceptance remains pending.
#include <windows.h>
#include <commctrl.h>
#include "EditorModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
#include <vector>
namespace just::distortion {
class WindowsContent final:public EditorContent {
    EditorModel editor;HWND window=nullptr,modelMenu=nullptr,modelLabel=nullptr,modelNote=nullptr,holdLabel=nullptr,holdValue=nullptr,notice=nullptr;
    HINSTANCE moduleHandle=nullptr;bool advanced=false,holdTyping=false,holdCancelled=false;int scroll=0;
    TextEditSession holdEdit;
    struct Rotary {ID id;std::unique_ptr<RotaryControl> control;};std::vector<Rotary> simple,rotaries;
    struct Menu {ID id;HWND label,control;};std::vector<Menu> menus;
    std::unique_ptr<AnalysisView> feedback;
    static HWND child(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style){return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,nullptr,reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE)),nullptr);}
    static void text(HWND w,const std::string& value){SetWindowTextA(w,value.c_str());}
    static LRESULT CALLBACK holdProcedure(HWND w,UINT msg,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR data){auto* p=reinterpret_cast<WindowsContent*>(data);
        if(msg==WM_KEYDOWN && (a==VK_RETURN || a==VK_ESCAPE)){p->holdCancelled=a==VK_ESCAPE;SetFocus(p->window);return 0;}return DefSubclassProc(w,msg,a,b);}
    void commitHold(){if(!holdTyping || holdCancelled || editor.currentModel()!=Model::Crush)return;char text[128]{};GetWindowTextA(holdValue,text,sizeof(text));double next;
        if(holdEdit.changed(parameters[registry.index(crush_hold_hz)],{},text,next))editor.write(crush_hold_hz,next);}
    void createControls(){
        modelLabel=child(window,L"STATIC",L"Model",0);modelMenu=child(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP);for(auto id:modelDisplayOrder)SendMessageA(modelMenu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(modelLabels[id]));modelNote=child(window,L"STATIC",L"",0);
        for(auto id:{drive_db,crush_bits,mix}){DisplayPolicy policy;policy.labelZh=parameterLabelZh(id);simple.push_back({id,RotaryControl::create(window,editor.services,parameters[registry.index(id)],policy)});}
        holdLabel=child(window,L"STATIC",L"Hold Rate",0);holdValue=child(window,L"EDIT",L"",ES_CENTER|ES_AUTOHSCROLL|WS_TABSTOP);SetWindowSubclass(holdValue,holdProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        notice=child(window,L"STATIC",L"",0);feedback=AnalysisView::create(window,editor.services,AnalysisViewMode::waveform);
        for(const auto& p:parameters){if(p.id==bypass || p.id==model || p.id==mix)continue;auto id=static_cast<ID>(p.id);
            if(p.enumLabels){Menu m{id,child(window,L"STATIC",L"",SS_CENTER),child(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP)};text(m.label,id==post_lp_enabled?"High Cut Enable":p.title);
                for(unsigned n=0;n<=p.stepCount;++n)SendMessageA(m.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.enumLabels[n]));EnableWindow(m.control,id!=quality);menus.push_back(m);
            }else{auto spec=p;if(id==post_lp_hz)spec.title="High Cut";DisplayPolicy policy;policy.labelZh=parameterLabelZh(id);rotaries.push_back({id,RotaryControl::create(window,editor.services,spec,policy)});}
        }
    }
    int panelHeight(int width) const {unsigned columns=std::max(3,(width-52)/144);return 52+int((rotaries.size()+menus.size()+columns-1)/columns)*144+56;}
    void layout(){if(!window)return;RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom,mainHeight=std::max(460,h),margin=std::max(24,w*36/1120),graphHeight=mainHeight-231,y=mainHeight-198-scroll;
        int gap=std::max(42,std::min(89,(w-428)/2)),left=(w-(428+2*gap))/2;
        MoveWindow(modelLabel,left,y+40,164,22,TRUE);MoveWindow(modelMenu,left,y+67,164,220,TRUE);MoveWindow(modelNote,left,y+111,176,42,TRUE);
        for(auto& c:simple){int x=c.id==mix?left+164+2*gap+132:left+164+gap;c.control->resize(x,y,132,158);ShowWindow(static_cast<HWND>(c.control->nativeHandle()),c.id==drive_db?(editor.currentModel()==Model::Crush?SW_HIDE:SW_SHOW):c.id==crush_bits?(editor.currentModel()==Model::Crush?SW_SHOW:SW_HIDE):SW_SHOW);}
        int hx=left+164+gap;MoveWindow(holdLabel,hx-3,y+172,55,20,TRUE);MoveWindow(holdValue,hx+53,y+169,102,24,TRUE);bool crush=editor.currentModel()==Model::Crush;ShowWindow(holdLabel,crush?SW_SHOW:SW_HIDE);ShowWindow(holdValue,crush?SW_SHOW:SW_HIDE);
        feedback->resize(margin,26-scroll,w-2*margin,graphHeight);MoveWindow(notice,margin+12,26+graphHeight-19-scroll,w-2*margin-24,17,TRUE);
        unsigned columns=std::max(3,(w-52)/144),i=0;int cell=(w-52)/columns,top=mainHeight+52-scroll;
        for(auto& c:rotaries){c.control->resize(26+(i%columns)*cell+(cell-98)/2,top+(i/columns)*144,98,114);ShowWindow(static_cast<HWND>(c.control->nativeHandle()),advanced?SW_SHOW:SW_HIDE);++i;}
        for(auto& m:menus){int x=26+(i%columns)*cell,yy=top+(i/columns)*144;MoveWindow(m.label,x,yy,cell,22,TRUE);MoveWindow(m.control,x+7,yy+52,cell-14,220,TRUE);ShowWindow(m.label,advanced?SW_SHOW:SW_HIDE);ShowWindow(m.control,advanced?SW_SHOW:SW_HIDE);++i;}
        int height=mainHeight+(advanced?panelHeight(w):0);SCROLLINFO info{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE|SIF_POS,0,height,static_cast<UINT>(h),scroll,0};SetScrollInfo(window,SB_VERT,&info,TRUE);ShowScrollBar(window,SB_VERT,advanced);
    }
    void command(HWND source,unsigned notification){if(source==holdValue){
            if(notification==EN_SETFOCUS){holdTyping=true;holdCancelled=false;text(holdValue,holdEdit.begin(parameters[registry.index(crush_hold_hz)],{},editor.normalized(crush_hold_hz)));}
            if(notification==EN_KILLFOCUS){commitHold();holdTyping=false;refresh(*editor.services.view,{});}return;
        }
        if(notification!=CBN_SELCHANGE)return;
        if(source==modelMenu){auto index=SendMessageW(source,CB_GETCURSEL,0,0);if(index>=0 && index<std::size(modelDisplayOrder))editor.write(model,double(modelDisplayOrder[index])/5);}
        else for(auto& m:menus)if(source==m.control && m.id!=quality)editor.write(m.id,double(SendMessageW(source,CB_GETCURSEL,0,0))/parameters[registry.index(m.id)].stepCount);
    }
    static LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM a,LPARAM b){auto* p=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){p=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p){if(msg==WM_SIZE){if(p->feedback)p->layout();return 0;}if(msg==WM_COMMAND){p->command(reinterpret_cast<HWND>(b),HIWORD(a));return 0;}
            if(msg==WM_VSCROLL){int next=p->scroll;switch(LOWORD(a)){case SB_LINEUP:next-=34;break;case SB_LINEDOWN:next+=34;break;case SB_PAGEUP:next-=200;break;case SB_PAGEDOWN:next+=200;break;case SB_THUMBTRACK:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&info);next=info.nTrackPos;break;}}RECT r;GetClientRect(w,&r);p->scroll=std::clamp(next,0,p->panelHeight(r.right));p->layout();return 0;}}
        return DefWindowProcW(w,msg,a,b);
    }
public:
    ~WindowsContent() override {simple.clear();rotaries.clear();feedback.reset();if(holdValue)RemoveWindowSubclass(holdValue,holdProcedure,1);if(window)DestroyWindow(window);if(moduleHandle)UnregisterClassW(L"JustDistortionContent",moduleHandle);}
    bool attach(void* parent,const EditorServices& s) override {if(!parent || window)return false;editor.services=s;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&moduleHandle))return false;
        WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=moduleHandle;c.lpszClassName=L"JustDistortionContent";c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&c);
        window=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1120,460,static_cast<HWND>(parent),nullptr,c.hInstance,this);if(!window)return false;createControls();refresh(*s.view,{});return true;}
    void resize(int w,int h) override {MoveWindow(window,0,0,w,h,TRUE);layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {if(view.advanced!=advanced)scroll=0;advanced=view.advanced;for(unsigned i=0;i<std::size(modelDisplayOrder);++i)if(modelDisplayOrder[i]==unsigned(editor.currentModel()))SendMessageW(modelMenu,CB_SETCURSEL,i,0);
        text(modelNote,editor.currentModel()==Model::Clean?"Clean preserves the signal; Drive is inactive":"Switching models preserves values");for(auto& c:simple)c.control->refresh(c.id==mix || (c.id==drive_db?editor.currentModel()!=Model::Clean && editor.currentModel()!=Model::Crush:editor.currentModel()==Model::Crush));for(auto& c:rotaries)c.control->refresh(advanced);
        for(auto& m:menus)SendMessageW(m.control,CB_SETCURSEL,int(editor.target(m.id)),0);if(!holdTyping)text(holdValue,editor.text(crush_hold_hz));
        text(notice,editor.hiddenCustom()?"Measured input / output waveform · Advanced: Custom":"Measured input / output waveform · fixed 4x / PDC 32");feedback->refresh();layout();}
};
EditorContent* createEditorContent(){return new WindowsContent;}
}
