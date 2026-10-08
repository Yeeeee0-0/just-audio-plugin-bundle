// Dedicated native Wider editor, using the same measured field and layout as macOS.
#include "EditorModel.hpp"
#include "FieldModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <map>
#include <mutex>

namespace just::stereo { namespace {
constexpr wchar_t className[]=L"JUST.Wider.ModuleContent.v2";
class WinContent final:public EditorContent {
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
        if(--classUsers==0)UnregisterClassW(className,module);
    }
    HWND window=nullptr,inputMode[2]{},meterMode[2]{},invertL=nullptr,invertR=nullptr,swapButton=nullptr,monoButton=nullptr,tooltip=nullptr;
    HMODULE module=nullptr;HFONT font=nullptr;HBRUSH background=nullptr;EditorServices services{};
    std::unique_ptr<EditorModel> model;
    std::map<ParamID,std::unique_ptr<RotaryControl>> controls;
    std::map<ParamID,HWND> menus,labels;std::map<HWND,std::wstring> tips;
    FieldMeasurement measured;AnalysisAvailability availability=AnalysisAvailability::unavailable;
    std::uint64_t resumeGeneration=0;bool advanced=false,ms=false,zh=false,stereoInput=false,knownBus=false,gray=false;int width=1120,height=520,scroll=0;double fontScale=0;
    unsigned columns()const{return std::max(3,(width-54)/132);}
    int contentHeight()const{return advanced?613+int(std::ceil(6./columns()))*156:498;}
    const char* local(const char* cn,const char* en)const{return zh?cn:en;}
    unsigned palette(unsigned rgb)const{if(!services.view || !services.view->visualsPaused)return rgb;unsigned v=((rgb>>16)*54+((rgb>>8)&255)*183+(rgb&255)*19)/256;return (v<<16)|(v<<8)|v;}
    void tip(HWND h,const char* value){if(!tooltip || !h)return;bool exists=tips.count(h);tips[h]=win::wide(value);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(h);info.lpszText=tips[h].data();SendMessageW(tooltip,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
    void updateFont(){double scale=win::scale(window);if(font && scale==fontScale)return;fontScale=scale;auto next=CreateFontW(-int(std::lround(13*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto& m:menus)SendMessageW(m.second,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto& l:labels)SendMessageW(l.second,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;}
    static LRESULT CALLBACK keyProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){auto* self=reinterpret_cast<WinContent*>(data);if((msg==WM_KEYDOWN || msg==WM_KEYUP) && w=='M' && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000))return SendMessageW(self->window,msg,w,l);if(msg==WM_KILLFOCUS)self->model->releaseMono();return DefSubclassProc(h,msg,w,l);}
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WinContent*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WinContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self || !self->model)return DefWindowProcW(hwnd,msg,wp,lp);
        switch(msg){
        case WM_COMMAND:{const unsigned id=LOWORD(wp),action=HIWORD(wp);
            if(id==1000 && action==BN_CLICKED){self->model->toggleMono();SetFocus(hwnd);self->refresh(*self->services.view,{});return 0;}
            if((id==InvertLeft || id==InvertRight || id==Swap) && action==BN_CLICKED){self->model->write(id,self->model->read(id)>=.5?0:1);self->refresh(*self->services.view,{});return 0;}
            if(action==BN_CLICKED && (id==1001 || id==1002)){self->ms=id==1002;self->refresh(*self->services.view,{});return 0;}
            if(action==BN_CLICKED && (id==1003 || id==1004)){if(self->stereoInput)self->model->write(InputMode,id==1004?1:0);self->refresh(*self->services.view,{});return 0;}
            if(action==CBN_SELCHANGE && registry.index(id)<registry.count){const auto& p=spec(id);auto index=SendMessageW(reinterpret_cast<HWND>(lp),CB_GETCURSEL,0,0);if(index>=0 && index<=p.stepCount)self->model->write(id,double(index)/p.stepCount);return 0;}break;}
        case WM_KEYDOWN:if(wp=='M'){if(!(lp&(1ll<<30)))self->model->holdMono();self->refresh(*self->services.view,{});return 0;}break;
        case WM_KEYUP:if(wp=='M'){self->model->releaseMono();self->refresh(*self->services.view,{});return 0;}break;
        case WM_KILLFOCUS:case WM_CANCELMODE:self->model->releaseMono();self->model->end();return 0;
        case WM_VSCROLL:case WM_MOUSEWHEEL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&si);int next=self->scroll;
            if(msg==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;
            else switch(LOWORD(wp)){case SB_LINEUP:next-=24;break;case SB_LINEDOWN:next+=24;break;case SB_PAGEUP:next-=self->height;break;case SB_PAGEDOWN:next+=self->height;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=si.nTrackPos;break;default:break;}
            self->scroll=std::clamp(next,0,std::max(0,self->contentHeight()-self->height));self->layout();return 0;}
        case WM_DRAWITEM:self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wp);auto rgb=self->palette(0x17333c);SetTextColor(dc,RGB(rgb>>16,(rgb>>8)&255,rgb&255));SetBkMode(dc,TRANSPARENT);return reinterpret_cast<LRESULT>(self->background);}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->paint();return 0;
        case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
        }return DefWindowProcW(hwnd,msg,wp,lp);
    }
    HWND child(const wchar_t* klass,const wchar_t* title,DWORD style,unsigned id){auto h=CreateWindowExW(0,klass,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,window,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;}
    HWND button(const wchar_t* title,unsigned id){auto h=child(L"BUTTON",title,BS_OWNERDRAW,id);SetWindowSubclass(h,keyProc,1,reinterpret_cast<DWORD_PTR>(this));return h;}
    void layout(){
        if(!window || controls.empty())return;updateFont();scroll=std::clamp(scroll,0,std::max(0,contentHeight()-height));const double pad=27,gap=20,total=width-2*pad-2*gap,left=total*1.05/4.94,center=total*2.85/4.94,right=total*1.04/4.94,cx=pad+left+gap,rx=cx+center+gap;
        controls[Input]->resize(int(pad),27-scroll,int(left*.5-2),324);controls[FieldWidth]->resize(int(pad+left*.5+2),27-scroll,int(left*.5-2),324);
        controls[Asymmetry]->resize(int(cx),339-scroll,int(center),38);controls[Rotation]->resize(int(cx),386-scroll,int(center),38);
        for(unsigned i=0;i<2;++i){win::place(inputMode[i],pad+i*left/2,378-scroll,left/2,27);win::place(meterMode[i],rx+i*right/2,378-scroll,right/2,27);}
        const double cell=(left-8)/3;win::place(invertL,pad,412-scroll,cell,25);win::place(swapButton,pad+cell+4,412-scroll,cell,25);win::place(invertR,pad+2*(cell+4),412-scroll,cell,25);win::place(monoButton,cx+(center-132)/2,433-scroll,132,26);
        unsigned n=0;const double step=(width-54)/double(columns());
        for(auto id:{Width,Existing,Mix,Low,High,Output}){ShowWindow(static_cast<HWND>(controls[id]->nativeHandle()),advanced?SW_SHOW:SW_HIDE);controls[id]->resize(int(27+(n%columns())*step+(step-112)/2),543+int(n/columns())*156-scroll,112,148);++n;}
        unsigned i=0;for(auto id:{Character,HighEnabled}){ShowWindow(menus[id],advanced?SW_SHOW:SW_HIDE);ShowWindow(labels[id],advanced?SW_SHOW:SW_HIDE);int top=543+int(std::ceil(6./columns()))*156-scroll;win::place(labels[id],27+i*220,top,200,20);win::place(menus[id],27+i*220,top+22,192,120);++i;}
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight()-1,UINT(height),scroll,0};SetScrollInfo(window,SB_VERT,&si,TRUE);InvalidateRect(window,nullptr,FALSE);
    }
    void drawButton(const DRAWITEMSTRUCT& d){bool selected=false;unsigned id=d.CtlID;
        if(id==1001 || id==1002)selected=(id==1002)==ms;else if(id==1003 || id==1004)selected=(id==1004)==(model->read(InputMode)>=.5);
        else if(id==1000)selected=model->read(Mono)>=.5;else if(id==InvertLeft || id==InvertRight || id==Swap)selected=model->read(id)>=.5;
        win::startup();Gdiplus::Graphics g(d.hDC);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);const auto scale=float(win::scale(d.hwndItem));g.ScaleTransform(scale,scale);Gdiplus::RectF r{0,0,(d.rcItem.right-d.rcItem.left)/scale,(d.rcItem.bottom-d.rcItem.top)/scale};win::fill(g,r,palette(selected?0xcdeff3:0xf5fafb),6);win::stroke(g,{.5f,.5f,r.Width-1,r.Height-1},palette(0xcbdfe4),6);win::text(g,win::windowText(d.hwndItem),r,12,palette((d.itemState&ODS_DISABLED)?0x9baeb4:selected?0x078f9e:0x537d8c),false,Gdiplus::StringAlignmentCenter);if(d.itemState&ODS_FOCUS)win::stroke(g,{2,2,r.Width-4,r.Height-4},palette(0x739eaa),4);
    }
    void paint(HDC target=nullptr){
        win::Paint paint(window,true,target);auto& g=paint.graphics();const float total=float(width-94),left=total*1.05f/4.94f,center=total*2.85f/4.94f,right=total*1.04f/4.94f,cx=47+left,rx=cx+center+20,sy=float(scroll);
        auto label=[&](const char* text,float x,float y,float w,float h,float points=10,unsigned rgb=0x7096a3,Gdiplus::StringAlignment align=Gdiplus::StringAlignmentNear){win::text(g,text,{x,y-sy,w,h},points,rgb,false,align);};
        win::fill(g,{cx,27-sy,center,292},0xf1f9fb,12);win::stroke(g,{cx+.5f,27.5f-sy,center-1,291},0xccE0E8,12);
        label(local("立体声声场","STEREO FIELD"),cx+18,43,140,16,10);label(local("实测输出","MEASURED OUTPUT"),cx+std::max(160.f,center-135),43,124,16,9);
        const auto field=FieldGeometry::fit(center,292);const float gx=cx+float(field.centerX),gy=27-sy+float(field.baseline);
        for(int ring=1;ring<=5;++ring){const float radius=float(field.radius*ring/5);Gdiplus::GraphicsPath grid;for(int step=1;step<=80;++step){const double a=3.14159265358979323846*(step-1)/80,b=3.14159265358979323846*step/80;grid.AddLine(gx-radius*float(std::cos(a)),gy-radius*float(std::sin(a)),gx-radius*float(std::cos(b)),gy-radius*float(std::sin(b)));}Gdiplus::Pen pen(win::color(0xbad9e6),.75f);g.DrawPath(&pen,&grid);}
        win::line(g,gx-float(field.radius),gy,gx+float(field.radius),gy,0xbad9e6,.75f);win::line(g,gx,gy,gx,gy-float(field.radius),0xbad9e6,.75f);
        const bool fresh=availability==AnalysisAvailability::fresh && measured.valid;
        if(fresh){Gdiplus::SolidBrush dot(win::color(0x0fa8ba,102));for(std::size_t i=0;i<measured.count;++i){const auto p=field.pixel(measured.points[i],measured.scale);g.FillEllipse(&dot,cx+float(p.side)-.9f,27-sy+float(p.mid)-.9f,1.8f,1.8f);}}
        else label(availability==AnalysisAvailability::stale?local("测量已过期","Measurement stale"):local("等待音频","Waiting for audio"),gx-80,27+float(field.baseline-field.radius*.45),160,20,12,0x7096a3,Gdiplus::StringAlignmentCenter);
        label("L",gx-float(field.radius),27+float(field.baseline)+13,15,14);label("0",gx-3,27+float(field.baseline)+13,15,14);label("R",gx+float(field.radius)-6,27+float(field.baseline)+13,15,14);
        char text[256];std::snprintf(text,sizeof(text),local("M/S 等比例 · ±%.1f","Equal M/S scale · ±%.1f"),measured.scale);label(text,cx+18,304,center-36,13,9);
        label(local("输入模式","INPUT MODE"),27,360,left,14,9,0x7096a3,Gdiplus::StringAlignmentCenter);label(local("电平模式","METER MODE"),rx,359,right,15,9,0x7096a3,Gdiplus::StringAlignmentCenter);
        label(knownBus && !stereoInput?local("单声道输入：M/S 不启用","Mono input · M/S inactive"):model->read(InputMode)>=.5?"1 = M  /  2 = S":"1 = L  /  2 = R",27,443,left,15,9,0x7096a3,Gdiplus::StringAlignmentCenter);
        label(local("输出电平","Output level"),rx,36,right,20,12,0x2e5966,Gdiplus::StringAlignmentCenter);
        const float top=89,bottom=302,rail=bottom-top;
        for(unsigned c=0;c<2;++c){const float x=rx+(c?.72f:.28f)*right;win::fill(g,{x-8,top-sy,16,rail},0xd9e8ed,4);const double peak=measured.peak(c,ms),db=peak>0?20*std::log10(peak):-120;
            if(fresh && peak>0){const float amount=float(std::clamp((db+60)/60,0.,1.));win::fill(g,{x-8,bottom-sy-rail*amount,16,rail*amount},0x0fadbf,4);if(db>0)win::fill(g,{x-8,top-sy-4,16,3},0xe87d5c);}
            label(ms?(c?"S":"M"):(c?"R":"L"),x-16,66,32,16,10,0x7096a3,Gdiplus::StringAlignmentCenter);
            if(fresh && peak>0)std::snprintf(text,sizeof(text),"%.1f",db);else std::snprintf(text,sizeof(text),"%s",fresh?"−∞":"—");label(text,x-30,bottom+12,60,18,11,0x2e5966,Gdiplus::StringAlignmentCenter);}
        label(local("dBFS · 采样峰值","dBFS · sample peak"),rx,336,right,14,9,0x7096a3,Gdiplus::StringAlignmentCenter);
        std::string status;if(fresh){if(measured.correlationValid){std::snprintf(text,sizeof(text),local("相关度 %.2f","Correlation %.2f"),measured.correlation);status=text;}else status=local("相关度 —","Correlation —");if(measured.header.flags&analysisBypassed)status+=local(" · 旁通"," · Bypass");else if((measured.header.flags&analysisTransportKnown) && !(measured.header.flags&analysisPlaying))status+=local(" · 宿主停止"," · Host stopped");}
        else status=availability==AnalysisAvailability::stale?local("测量已过期","Measurement stale"):local("等待音频","Waiting for audio");label(status.c_str(),27,467,float(width-54),21,10,fresh && measured.correlationValid && measured.correlation<0?0xc76340:0x7096a3,Gdiplus::StringAlignmentCenter);
        if(advanced)label(local("生成侧信号 / 输出","GENERATED SIDE / OUTPUT"),27,510,float(width-54),18,10);
    }
public:
    ~WinContent()override{controls.clear();if(model){model->releaseMono();model->end();}if(tooltip)DestroyWindow(tooltip);if(window)DestroyWindow(window);releaseClass();if(font)DeleteObject(font);if(background)DeleteObject(background);}
    bool attach(void* parent,const EditorServices& s)override{
        if(!parent || window || classAcquired || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;model=std::make_unique<EditorModel>(s);module=win::moduleAt(reinterpret_cast<const void*>(&proc));background=CreateSolidBrush(RGB(245,250,251));
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=module;wc.lpszClassName=className;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!acquireClass(wc))return false;
        window=CreateWindowExW(0,className,L"JUST Wider",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,module,nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,440);
        EditorServices binding;binding.owner=this;binding.view=s.view;
        binding.readTarget=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);return v->services.readTarget(v->services.owner,id);};
        binding.beginEdit=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);v->model->releaseMono();return v->services.beginEdit(v->services.owner,id);};
        binding.performEdit=[](void* p,ParamID id,double n){auto* v=static_cast<WinContent*>(p);return v->services.performEdit(v->services.owner,id,n);};
        binding.endEdit=[](void* p,ParamID id){auto* v=static_cast<WinContent*>(p);v->services.endEdit(v->services.owner,id);};
        for(auto id:{Input,FieldWidth,Asymmetry,Rotation,Width,Existing,Mix,Low,High,Output}){auto p=spec(id);if(id==Input)p.title="Gain";DisplayPolicy policy;policy.decimals=1;
            policy.labelZh=id==Input?"增益":id==FieldWidth?"宽度":id==Asymmetry?"不对称":id==Rotation?"旋转":id==Width?"生成宽度":id==Existing?"原始侧信号":id==Mix?"混合":id==Low?"生成侧信号低切":id==High?"生成侧信号高切":"输出增益";
            policy.style=id==Input || id==FieldWidth?ControlStyle::vertical:id==Asymmetry || id==Rotation?ControlStyle::horizontal:ControlStyle::rotary;controls[id]=RotaryControl::create(window,binding,p,policy);if(!controls[id])return false;}
        for(unsigned i=0;i<2;++i){inputMode[i]=button(i?L"M / S":L"L / R",1003+i);meterMode[i]=button(i?L"M / S":L"L / R",1001+i);}
        invertL=button(L"L +",InvertLeft);invertR=button(L"R +",InvertRight);swapButton=button(L"⇄",Swap);monoButton=button(L"Mono Check",1000);
        for(auto id:{Character,HighEnabled}){const auto& p=spec(id);labels[id]=child(L"STATIC",win::wide(p.title).c_str(),0,0);menus[id]=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,id);for(unsigned i=0;i<=p.stepCount;++i)SendMessageW(menus[id],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(p.enumLabels[i]).c_str()));}
        refresh(*s.view,{});return true;
    }
    void resize(int w,int h)override{width=w;height=h;if(window)win::place(window,0,0,w,h);layout();}
    void refresh(const EditorViewState& state,const StatusSnapshot&)override{
        if(!window)return;if(advanced!=state.advanced){model->releaseMono();model->end();scroll=0;}advanced=state.advanced;zh=state.language==UiLanguage::chinese;if(gray!=state.visualsPaused){gray=state.visualsPaused;auto next=CreateSolidBrush(gray?RGB(249,249,249):RGB(245,250,251));DeleteObject(background);background=next;}
        if(GetForegroundWindow()!=GetAncestor(window,GA_ROOT)){model->releaseMono();model->end();}
        BusLayoutSnapshot bus;knownBus=services.readBusLayout && services.readBusLayout(services.owner,bus) && bus.valid() && (bus.validFields&layoutBuses);stereoInput=knownBus && bus.inputChannels==2;
        for(auto& c:controls)c.second->refresh(c.first==High?model->read(HighEnabled)>=.5:c.first==Existing?stereoInput:true);
        for(auto h:inputMode){EnableWindow(h,stereoInput);tip(h,stereoInput?local("L/R 输入或编码 M/S 输入。","L/R input or encoded M/S input."):knownBus?local("M/S 输入需要双声道总线；保留已保存选项。","M/S input needs a two-channel bus; saved choice is retained."):local("输入总线布局不可用；等待宿主报告后才能更改输入模式。","Input bus layout unavailable; no input mode change until the host reports it."));}
        EnableWindow(invertR,stereoInput);for(auto id:{Character,HighEnabled})SendMessageW(menus[id],CB_SETCURSEL,WPARAM(std::round(model->read(id))),0);
        SetWindowTextW(labels[Character],zh?L"特性":L"Character");SetWindowTextW(labels[HighEnabled],zh?L"启用生成侧信号高切":L"High Cut Enabled");
        SetWindowTextW(invertL,model->read(InvertLeft)>=.5?L"L −":L"L +");SetWindowTextW(invertR,model->read(InvertRight)>=.5?L"R −":L"R +");SetWindowTextW(swapButton,model->read(Swap)>=.5?L"⇄ On":L"⇄");SetWindowTextW(monoButton,win::wide(model->isHoldingMono()?local("单声道 · 按住","Mono · held"):model->read(Mono)>=.5?local("单声道 · 锁定","Mono · locked"):local("单声道试听","Mono Check")).c_str());
        tip(monoButton,local("点击锁定。按住 M 试听；松开恢复之前的锁定状态。","Click to lock. Hold M to audition; release restores the previous lock."));tip(swapButton,local("反转最终侧信号以交换左右声道。","Swap left/right by reversing the final side signal."));for(auto h:meterMode)tip(h,local("仅显示最终输出采样窗口峰值。M/S = 0.5 × (L ± R)。","Display only. Peak levels of the final output sample window; M/S = 0.5 × (L ± R)."));
        tip(static_cast<HWND>(controls[FieldWidth]->nativeHandle()),local("生成、剪切和旋转后的最终侧信号宽度。0.0× 移除所有侧信号。","Final side width after generation, shear and rotation. 0.0× removes all side."));tip(static_cast<HWND>(controls[Asymmetry]->nativeHandle()),"JUST side-to-mid shear: M′ = M + sin(angle) × S; before rotation and overall width.");tip(static_cast<HWND>(controls[Rotation]->nativeHandle()),"JUST orthogonal mid/side rotation, before overall Width, Swap and Mono Check.");
        if(resumeGeneration!=state.visualResumeGeneration){measured.clear();availability=AnalysisAvailability::unavailable;resumeGeneration=state.visualResumeGeneration;}
        if(!state.visualsPaused){SampleFrame frame;availability=services.readSamples?services.readSamples(services.owner,frame):AnalysisAvailability::unavailable;if(availability==AnalysisAvailability::fresh && !measured.accept(frame))availability=AnalysisAvailability::unavailable;if(availability!=AnalysisAvailability::fresh)measured.clear();}
        for(auto h:{inputMode[0],inputMode[1],meterMode[0],meterMode[1],invertL,invertR,swapButton,monoButton})InvalidateRect(h,nullptr,FALSE);layout();
    }
};
} EditorContent* createEditorContent(){return new WinContent;}
}
