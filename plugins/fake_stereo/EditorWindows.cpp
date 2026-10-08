#include "EditorModel.hpp"
#include "FieldModel.hpp"
#include "common/ui/Controls.hpp"
#include <windows.h>
#include <commctrl.h>
#include <map>
#include <string>

namespace just::stereo { namespace {
constexpr wchar_t className[]=L"JUST.Wider.ModuleContent";
std::wstring wide(const char* s){const int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,v.data(),n);return v;}
class WinContent final:public EditorContent {
    HWND window=nullptr,inputMode=nullptr,meterMode=nullptr,invertL=nullptr,invertR=nullptr,swapButton=nullptr,monoButton=nullptr;
    HMODULE module=nullptr;HFONT font=nullptr;EditorServices services{};
    std::unique_ptr<EditorModel> model;
    std::map<ParamID,std::unique_ptr<RotaryControl>> controls;
    std::map<ParamID,HWND> menus,labels;
    FieldMeasurement measured;AnalysisAvailability availability=AnalysisAvailability::unavailable;
    bool advanced=false,ms=false,zh=false;int width=1120,height=520,scroll=0;
    int contentHeight() const {return advanced?900:500;}
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WinContent*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WinContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self || !self->model)return DefWindowProcW(hwnd,msg,wp,lp);
        switch(msg){
        case WM_COMMAND:{const auto id=LOWORD(wp),action=HIWORD(wp);
            if(id==1000 && action==BN_CLICKED){self->model->toggleMono();SetFocus(hwnd);self->refresh(*self->services.view,{});return 0;}
            if((id==InvertLeft || id==InvertRight || id==Swap) && action==BN_CLICKED){self->model->write(id,self->model->read(id)>=.5?0:1);self->refresh(*self->services.view,{});return 0;}
            if(id==1001 && action==CBN_SELCHANGE){self->ms=SendMessageW(self->meterMode,CB_GETCURSEL,0,0)==1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(action==CBN_SELCHANGE && registry.index(id)<registry.count){const auto& p=spec(id);self->model->write(id,double(SendMessageW(reinterpret_cast<HWND>(lp),CB_GETCURSEL,0,0))/p.stepCount);return 0;}break;}
        case WM_KEYDOWN:if(wp=='M'){if(!(lp&(1ll<<30)))self->model->holdMono();return 0;}break;
        case WM_KEYUP:if(wp=='M'){self->model->releaseMono();return 0;}break;
        case WM_KILLFOCUS:self->model->releaseMono();self->model->end();return 0;
        case WM_SIZE:self->width=LOWORD(lp);self->height=HIWORD(lp);self->layout();return 0;
        case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&si);int next=self->scroll;
            switch(LOWORD(wp)){case SB_LINEUP:next-=24;break;case SB_LINEDOWN:next+=24;break;case SB_PAGEUP:next-=self->height;break;case SB_PAGEDOWN:next+=self->height;break;case SB_THUMBTRACK:next=si.nTrackPos;break;default:break;}
            self->scroll=std::clamp(next,0,std::max(0,self->contentHeight()-self->height));self->layout();return 0;}
        case WM_PAINT:self->paint();return 0;
        }return DefWindowProcW(hwnd,msg,wp,lp);
    }
    HWND child(const wchar_t* klass,const wchar_t* title,DWORD style,unsigned id){auto h=CreateWindowExW(0,klass,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,window,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;}
    HWND combo(unsigned id){auto h=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,id);SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"L / R"));SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"M / S"));SendMessageW(h,CB_SETCURSEL,0,0);return h;}
    void layout(){
        if(!window)return;const double pad=27,gap=20,total=width-2*pad-2*gap,left=total*1.05/4.94,center=total*2.85/4.94,right=total*1.04/4.94,cx=pad+left+gap,rx=cx+center+gap;
        controls[Input]->resize(int(pad),27-scroll,int(left*.5-2),324);controls[FieldWidth]->resize(int(pad+left*.5+2),27-scroll,int(left*.5-2),324);
        controls[Asymmetry]->resize(int(cx),339-scroll,int(center),38);controls[Rotation]->resize(int(cx),386-scroll,int(center),38);
        MoveWindow(inputMode,int(pad),378-scroll,int(left),120,TRUE);MoveWindow(meterMode,int(rx),378-scroll,int(right),120,TRUE);
        int cell=int((left-8)/3);MoveWindow(invertL,int(pad),414-scroll,cell,25,TRUE);MoveWindow(swapButton,int(pad)+cell+4,414-scroll,cell,25,TRUE);MoveWindow(invertR,int(pad)+2*(cell+4),414-scroll,cell,25,TRUE);
        MoveWindow(monoButton,int(cx+(center-132)/2),433-scroll,132,26,TRUE);
        unsigned n=0,columns=std::max(3u,unsigned((width-54)/132));const int step=(width-54)/columns;
        for(auto id:{Width,Existing,Mix,Low,High,Output}){ShowWindow(static_cast<HWND>(controls[id]->nativeHandle()),advanced?SW_SHOW:SW_HIDE);controls[id]->resize(27+(n%columns)*step+(step-112)/2,543+(n/columns)*156-scroll,112,148);++n;}
        unsigned i=0;for(auto id:{Character,HighEnabled}){ShowWindow(menus[id],advanced?SW_SHOW:SW_HIDE);ShowWindow(labels[id],advanced?SW_SHOW:SW_HIDE);int top=543+int(std::ceil(6./columns))*156-scroll;MoveWindow(labels[id],27+i*220,top,200,20,TRUE);MoveWindow(menus[id],27+i*220,top+22,192,120,TRUE);++i;}
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight()-1,UINT(height),scroll,0};SetScrollInfo(window,SB_VERT,&si,TRUE);InvalidateRect(window,nullptr,TRUE);
    }
    void paint(){
        PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);SetTextColor(dc,RGB(83,125,140));
        double total=width-94,left=total*1.05/4.94,center=total*2.85/4.94,right=total*1.04/4.94,cx=47+left,rx=cx+center+20;
        RECT card{int(cx),27-scroll,int(cx+center),319-scroll};auto brush=CreateSolidBrush(RGB(242,250,252));FillRect(dc,&card,brush);DeleteObject(brush);
        auto g=FieldGeometry::fit(center,292);const int gx=int(cx+g.centerX),gy=int(27-scroll+g.baseline);auto pen=CreatePen(PS_SOLID,1,RGB(187,217,229));auto old=SelectObject(dc,pen);
        for(int ring=1;ring<=5;++ring){double radius=g.radius*ring/5;for(int step=0;step<=80;++step){double a=3.14159265358979323846*step/80;int x=int(gx-radius*std::cos(a)),y=int(gy-radius*std::sin(a));if(!step)MoveToEx(dc,x,y,nullptr);else LineTo(dc,x,y);}}
        MoveToEx(dc,gx-int(g.radius),gy,nullptr);LineTo(dc,gx+int(g.radius),gy);MoveToEx(dc,gx,gy,nullptr);LineTo(dc,gx,gy-int(g.radius));SelectObject(dc,old);DeleteObject(pen);
        const bool fresh=availability==AnalysisAvailability::fresh && measured.valid;
        if(fresh)for(unsigned i=0;i<measured.count;++i){auto p=g.pixel(measured.points[i],measured.scale);SetPixelV(dc,int(cx+p.side),int(27-scroll+p.mid),RGB(24,168,185));}
        RECT title{int(cx+18),43-scroll,int(cx+center-18),68-scroll};DrawTextW(dc,zh?L"立体声声场 · 实测输出":L"STEREO FIELD · MEASURED OUTPUT",-1,&title,DT_SINGLELINE);
        RECT note{int(cx+18),297-scroll,int(cx+center-18),317-scroll};DrawTextW(dc,fresh?L"Equal M/S scale":availability==AnalysisAvailability::stale?L"Measurement stale":L"Waiting for audio",-1,&note,DT_SINGLELINE);
        RECT mode{27,354-scroll,int(27+left),374-scroll};DrawTextW(dc,model->read(InputMode)>=.5?L"INPUT: 1=M / 2=S":L"INPUT: 1=L / 2=R",-1,&mode,DT_CENTER|DT_SINGLELINE);
        for(unsigned c=0;c<2;++c){int x=int(rx+(c?.72:.28)*right);RECT rail{x-8,86-scroll,x+8,315-scroll};auto bg=CreateSolidBrush(RGB(216,233,238));FillRect(dc,&rail,bg);DeleteObject(bg);double peak=measured.peak(c,ms),db=peak>0?20*std::log10(peak):-120;
            if(fresh){RECT bar=rail;bar.top=bar.bottom-int(229*std::clamp((db+60)/60,0.,1.));auto fg=CreateSolidBrush(RGB(24,175,193));FillRect(dc,&bar,fg);DeleteObject(fg);}
            RECT label{x-22,61-scroll,x+22,82-scroll};DrawTextW(dc,ms?(c?L"S":L"M"):(c?L"R":L"L"),-1,&label,DT_CENTER|DT_SINGLELINE);wchar_t value[32];if(fresh)swprintf(value,32,L"%.1f",db);else wcscpy_s(value,L"—");RECT text{x-28,325-scroll,x+28,348-scroll};DrawTextW(dc,value,-1,&text,DT_CENTER|DT_SINGLELINE);}
        EndPaint(window,&ps);
    }
public:
    ~WinContent() override {controls.clear();if(model){model->releaseMono();model->end();}if(window)DestroyWindow(window);}
    bool attach(void* parent,const EditorServices& s) override {
        if(!parent || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;model=std::make_unique<EditorModel>(s);font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=module;wc.lpszClassName=className;wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
        window=CreateWindowExW(0,className,L"JUST Wider",WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        EditorServices binding;binding.owner=this;binding.view=s.view;
        binding.readTarget=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);return v->services.readTarget(v->services.owner,id);};
        binding.beginEdit=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);v->model->releaseMono();return v->services.beginEdit(v->services.owner,id);};
        binding.performEdit=[](void* p,ParamID id,double n){auto* v=static_cast<WinContent*>(p);return v->services.performEdit(v->services.owner,id,n);};
        binding.endEdit=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);v->services.endEdit(v->services.owner,id);};
        for(auto id:{Input,FieldWidth,Asymmetry,Rotation,Width,Existing,Mix,Low,High,Output}){auto p=spec(id);if(id==Input)p.title="Gain";DisplayPolicy policy;policy.decimals=1;policy.style=id==Input || id==FieldWidth?ControlStyle::vertical:id==Asymmetry || id==Rotation?ControlStyle::horizontal:ControlStyle::rotary;controls[id]=RotaryControl::create(window,binding,p,policy);}
        inputMode=combo(InputMode);meterMode=combo(1001);invertL=child(L"BUTTON",L"L +",BS_PUSHBUTTON,InvertLeft);invertR=child(L"BUTTON",L"R +",BS_PUSHBUTTON,InvertRight);swapButton=child(L"BUTTON",L"Swap",BS_PUSHBUTTON,Swap);monoButton=child(L"BUTTON",L"Mono Check",BS_PUSHBUTTON,1000);
        for(auto id:{Character,HighEnabled}){const auto& p=spec(id);labels[id]=child(L"STATIC",wide(p.title).c_str(),0,0);menus[id]=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,id);for(unsigned i=0;i<=p.stepCount;++i)SendMessageW(menus[id],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.enumLabels[i]).c_str()));}
        refresh(*s.view,{});return true;
    }
    void resize(int w,int h) override {width=w;height=h;if(window)MoveWindow(window,0,0,w,h,TRUE);layout();}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {
        if(!window)return;advanced=state.advanced;zh=state.language==UiLanguage::chinese;BusLayoutSnapshot bus;const bool stereo=services.readBusLayout && services.readBusLayout(services.owner,bus) && bus.valid() && (bus.validFields&layoutBuses) && bus.inputChannels==2;
        for(auto& c:controls)c.second->refresh(c.first==High?model->read(HighEnabled)>=.5:c.first==Existing?stereo:true);
        EnableWindow(inputMode,stereo);EnableWindow(invertR,stereo);SendMessageW(inputMode,CB_SETCURSEL,model->read(InputMode)>=.5?1:0,0);
        for(auto id:{Character,HighEnabled})SendMessageW(menus[id],CB_SETCURSEL,WPARAM(std::round(model->read(id))),0);
        SetWindowTextW(invertL,model->read(InvertLeft)>=.5?L"L −":L"L +");SetWindowTextW(invertR,model->read(InvertRight)>=.5?L"R −":L"R +");SetWindowTextW(swapButton,model->read(Swap)>=.5?L"Swap On":L"Swap");SetWindowTextW(monoButton,model->read(Mono)>=.5?L"Mono · locked":L"Mono Check");
        SampleFrame frame;availability=services.readSamples?services.readSamples(services.owner,frame):AnalysisAvailability::unavailable;if(availability==AnalysisAvailability::fresh){if(!measured.accept(frame))availability=AnalysisAvailability::unavailable;}else measured.clear();layout();
    }
};
} EditorContent* createEditorContent(){return new WinContent;}
}
