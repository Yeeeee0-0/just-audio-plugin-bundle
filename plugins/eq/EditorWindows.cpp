#include "EditorModel.hpp"
#include <windows.h>
#include <windowsx.h>
namespace just {
namespace {
using namespace eq;
struct EqWindowsEditor final:EditorContent {
    EditorModel model;HWND window=nullptr;HMODULE module=nullptr;int width=680,height=310,scroll=0;RECT graph{},localPanel{};
    std::array<HWND,12> bands{};HWND on=nullptr,selectedTitle=nullptr;
    struct Control {HWND label=nullptr,value=nullptr;int tag=0;bool combo=false,advanced=false,dirty=false;};
    std::vector<Control> controls;bool dragging=false,refreshing=false;
    static COLORREF color(int f){auto c=fieldStyles[f].rgb;return RGB(c>>16,(c>>8)&255,c&255);}
    static std::wstring wide(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);return w;}
    HWND child(const wchar_t* type,const wchar_t* text,DWORD style,int id) {
        auto h=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;
    }
    void add(const char* title,int tag,const char* const* labels=nullptr,int count=0,bool advanced=false) {
        Control c;c.label=child(L"STATIC",wide(title).c_str(),0,0);c.tag=tag;c.combo=labels;c.advanced=advanced;
        c.value=child(labels?L"COMBOBOX":L"EDIT",L"",labels?CBS_DROPDOWNLIST|WS_VSCROLL:WS_BORDER|ES_AUTOHSCROLL,100+int(controls.size()));
        if(labels)for(int i=0;i<count;++i)SendMessageW(c.value,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(labels[i]).c_str()));
        controls.push_back(c);
    }
    bool attach(void* parent,const EditorServices& services) override {
        model.connect(services);GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module);
        WNDCLASSW klass{};klass.style=CS_DBLCLKS;klass.lpfnWndProc=proc;klass.hInstance=module;klass.lpszClassName=L"JUST.EQ.Content";klass.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&klass);
        window=CreateWindowExW(0,klass.lpszClassName,L"Just EQ",WS_CHILD|WS_VISIBLE|WS_VSCROLL,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return false;
        for(int i=0;i<12;++i)bands[i]=child(L"BUTTON",std::to_wstring(i+1).c_str(),BS_PUSHBUTTON,20+i);
        on=child(L"BUTTON",L"Band on",BS_AUTOCHECKBOX,50);
        selectedTitle=child(L"STATIC",L"Editing band 01 - controls affect this point",0,0);
        add("Frequency",frequency);add("Gain",gain);add("Q",q);add("Shape",type,typeLabels,6);add("Sound field",target,targetLabels,3);
        add("Slope (dB/oct)",slope,slopeChoiceLabels,8,true);add("Dynamic",dynamicEnabled,dynamic_enabledLabels,2,true);
        add("Range (dB)",range,nullptr,0,true);add("Threshold (dBFS)",threshold,nullptr,0,true);add("Knee (dB)",knee,nullptr,0,true);
        add("Attack (ms)",attack,nullptr,0,true);add("Release (ms)",release,nullptr,0,true);add("Detector",detector,detectorLabels,2,true);add("Source",source,sourceLabels,2,true);
        add("Input (dB)",1001,nullptr,0,true);add("Output (dB)",1002,nullptr,0,true);sync();resize(width,height);return true;
    }
    ~EqWindowsEditor() override {model.cancelDrag();if(window)DestroyWindow(window);}
    void sync() {
        refreshing=true;model.refresh();SendMessageW(on,BM_SETCHECK,model.value(enabled)>0.5?BST_CHECKED:BST_UNCHECKED,0);
        auto title=L"Editing band "+std::to_wstring(model.selected+1)+L" - controls affect this point";SetWindowTextW(selectedTitle,title.c_str());
        for(int b=0;b<12;++b) {auto s=std::to_wstring(b+1)+(physical(model.state,index(b,enabled))>0.5?L" *":L"");SetWindowTextW(bands[b],s.c_str());}
        for(auto& c:controls) {
            bool show=c.advanced?model.advanced:model.panelVisible;ShowWindow(c.label,show?SW_SHOW:SW_HIDE);ShowWindow(c.value,show?SW_SHOW:SW_HIDE);
            auto idx=c.tag>=1000?std::size_t(c.tag-1000):index(model.selected,Field(c.tag));
            if(c.combo)SendMessageW(c.value,CB_SETCURSEL,c.tag==slope?model.slopeChoice():int(physical(model.state,idx)),0);
            else if(GetFocus()!=c.value){char s[64];parameters[idx].format(model.state.targets[idx],s,sizeof(s));auto text=std::string(s)+(*parameters[idx].unit?std::string(" ")+parameters[idx].unit:"");SetWindowTextW(c.value,wide(text.c_str()).c_str());}
            bool applicable=true;
            if(c.tag==gain || (c.tag>=dynamicEnabled && c.tag<=source))applicable=int(model.value(type))<=2;
            if(c.tag==slope)applicable=int(model.value(type))==3 || int(model.value(type))==4;
            EnableWindow(c.value,applicable);
        }
        ShowWindow(selectedTitle,model.panelVisible?SW_SHOW:SW_HIDE);refreshing=false;if(!dragging)resize(width,height);InvalidateRect(window,nullptr,FALSE);
    }
    void resize(int w,int h) override {
        width=w;height=h;MoveWindow(window,0,0,w,h,TRUE);
        if(!model.advanced)scroll=0;
        int doc=model.advanced?650:std::max(280,h);scroll=std::clamp(scroll,0,std::max(0,doc-h));
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,doc-1,static_cast<UINT>(h),scroll,0};SetScrollInfo(window,SB_VERT,&si,TRUE);
        graph={42,48-scroll,w-12,48-scroll+std::max(100,(model.advanced?310:doc)-100)};
        int y=graph.bottom+24,cell=(w-82)/12;
        for(int b=0;b<12;++b)MoveWindow(bands[b],b*cell,y,cell-3,25,TRUE);
        MoveWindow(on,w-82,y,82,25,TRUE);y+=31;
        int pw=std::min(560,int(graph.right-graph.left)-10),ph=64,nx=xFor(model.value(frequency)),ny=yFor(int(model.value(type))<=2?model.value(gain):0),px=std::clamp(nx-pw/2,int(graph.left)+5,int(graph.right)-pw-5);
        auto clampY=[&](int yy){return std::clamp(yy,int(graph.top)+6,int(graph.bottom)+16-ph);};RECT above{px,clampY(ny-ph-18),px+pw,clampY(ny-ph-18)+ph},below{px,clampY(ny+18),px+pw,clampY(ny+18)+ph};
        auto collisions=[&](RECT r){int count=0;InflateRect(&r,6,6);for(std::size_t b=0;b<bandCount;++b)if(physical(model.state,index(b,enabled))>0.5){int s=int(physical(model.state,index(b,type)));POINT p{xFor(physical(model.state,index(b,frequency))),yFor(s<=2?physical(model.state,index(b,gain)):0)};if(PtInRect(&r,p))count+=b==model.selected?10:1;}return count;};
        if(!dragging)localPanel=collisions(above)<=collisions(below)?above:below;
        MoveWindow(selectedTitle,localPanel.left+8,localPanel.top+3,pw-20,16,TRUE);
        for(std::size_t i=0;i<controls.size();++i) {
            auto& c=controls[i];int x,yy,cw;
            if(i<5){cw=(pw-20)/5;x=localPanel.left+8+int(i)*cw;yy=localPanel.top+20;}
            else {cw=w/3;x=int((i-5)%3)*cw;yy=y+int((i-5)/3)*58;}
            MoveWindow(c.label,x,yy,cw-8,16,TRUE);MoveWindow(c.value,x,yy+17,cw-8,c.combo?200:24,TRUE);
        }
        InvalidateRect(window,nullptr,TRUE);
    }
    void refresh(const EditorViewState&,const StatusSnapshot&) override {bool old=model.advanced;sync();if(old!=model.advanced){model.cancelDrag();scroll=0;resize(width,height);}}
    int xFor(double hz) const {return graph.left+int(std::log(hz/20)/std::log(1000)*(graph.right-graph.left));}
    int yFor(double db) const {return (graph.top+graph.bottom)/2-int(std::clamp(db,-24.0,24.0)/48*(graph.bottom-graph.top));}
    double hzFor(int x) const {return std::clamp(20*std::pow(1000.0,double(x-graph.left)/(graph.right-graph.left)),20.0,20000.0);}
    double dbFor(int y) const {return std::clamp(double((graph.top+graph.bottom)/2-y)*48/(graph.bottom-graph.top),-24.0,24.0);}
    int hit(POINT p) const {
        for(int b=11;b>=0;--b)if(physical(model.state,index(b,enabled))>0.5) {
            int s=int(physical(model.state,index(b,type)));if(std::hypot(double(p.x-xFor(physical(model.state,index(b,frequency)))),double(p.y-yFor(s<=2?physical(model.state,index(b,gain)):0)))<14)return b;
        }return -1;
    }
    void paint() {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT rect{0,0,width,height};HBRUSH bg=CreateSolidBrush(RGB(245,246,248));FillRect(dc,&rect,bg);DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        auto text=[&](int x,int y,const std::wstring& s,COLORREF c){SetTextColor(dc,c);TextOutW(dc,x,y,s.c_str(),int(s.size()));};
        for(int f=0;f<3;++f)text(f*80,6-scroll,wide(fieldStyles[f].label),color(f));
        text(0,29-scroll,L"Double-click: add  Drag: Hz/dB  Wheel: Q  Right-click: field  Static/48 kHz",RGB(95,100,110));
        FillRect(dc,&graph,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HPEN grid=CreatePen(PS_SOLID,1,RGB(215,219,224));auto oldPen=SelectObject(dc,grid);
        for(double d:{-24.,-12.,0.,12.,24.}) {int y=yFor(d);MoveToEx(dc,graph.left,y,nullptr);LineTo(dc,graph.right,y);text(0,y-6,std::to_wstring(int(d)),RGB(110,115,120));}
        for(double hz:{20.,100.,1000.,10000.,20000.}) {int x=xFor(hz);MoveToEx(dc,x,graph.top,nullptr);LineTo(dc,x,graph.bottom);text(std::min(x,graph.right-35),graph.bottom+3,hz>=1000?std::to_wstring(int(hz/1000))+L"k":std::to_wstring(int(hz)),RGB(110,115,120));}
        SelectObject(dc,oldPen);DeleteObject(grid);int saved=SaveDC(dc);IntersectClipRect(dc,graph.left,graph.top,graph.right,graph.bottom);
        for(int f=0;f<3;++f)if(model.active(f)) {
            DWORD dash[]={f==1?8ul:2ul,5ul};LOGBRUSH lb{BS_SOLID,color(f),0};HPEN pen=ExtCreatePen(PS_GEOMETRIC|PS_ENDCAP_FLAT|(f?PS_USERSTYLE:PS_SOLID),2,&lb,f?2:0,f?dash:nullptr);auto old=SelectObject(dc,pen);
            for(int x=graph.left;x<=graph.right;++x){auto db=model.response(hzFor(x));if(x==graph.left)MoveToEx(dc,x,yFor(db[f]),nullptr);else LineTo(dc,x,yFor(db[f]));}SelectObject(dc,old);DeleteObject(pen);
        }
        RestoreDC(dc,saved);
        for(std::size_t b=0;b<bandCount;++b)if(physical(model.state,index(b,enabled))>0.5) {
            int f=int(physical(model.state,index(b,target))),s=int(physical(model.state,index(b,type))),x=xFor(physical(model.state,index(b,frequency))),y=yFor(s<=2?physical(model.state,index(b,gain)):0);
            auto brush=CreateSolidBrush(color(f));auto ob=SelectObject(dc,brush);auto pen=CreatePen(PS_SOLID,b==model.selected?3:1,RGB(255,255,255));auto op=SelectObject(dc,pen);
            if(f==0)Ellipse(dc,x-6,y-6,x+6,y+6);else if(f==1)Rectangle(dc,x-6,y-6,x+6,y+6);else{POINT pts[]={{x,y-8},{x+8,y},{x,y+8},{x-8,y}};Polygon(dc,pts,4);}
            SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
            int lx=std::min(x+9,int(graph.right)-70),ly=y-18;RECT labelRect{lx,ly,lx+70,ly+16},overlap{};
            if(model.panelVisible && IntersectRect(&overlap,&labelRect,&localPanel))ly=y+8;
            text(lx,ly,std::to_wstring(b+1)+L" "+wide(fieldStyles[f].label),color(f));
        }
        if(model.panelVisible){auto b=CreateSolidBrush(RGB(250,250,252));FillRect(dc,&localPanel,b);DeleteObject(b);}
        if(model.advanced){text(0,600-scroll,L"Static curves by field / reference 48 kHz",RGB(95,100,110));text(0,620-scroll,L"Side inactive on mono. Missing external SC: dynamic gain returns to zero.",RGB(95,100,110));}
        EndPaint(window,&ps);
    }
    void edit(Control& c) {
        if(refreshing || (!c.combo && !c.dirty))return;
        c.dirty=false;
        if(c.combo){int choice=int(SendMessageW(c.value,CB_GETCURSEL,0,0));if(c.tag==slope)model.writeSlopeChoice(choice);else model.write(Field(c.tag),choice);}
        else {wchar_t w[128];GetWindowTextW(c.value,w,128);char s[512];WideCharToMultiByte(CP_UTF8,0,w,-1,s,sizeof(s),nullptr,nullptr);
            if(c.tag>=1000){auto i=std::size_t(c.tag-1000);double v;if(parameters[i].parse(s,v))model.writeID(parameters[i].id,v);}
            else model.parse(Field(c.tag),s);
        }sync();
    }
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* v=reinterpret_cast<EqWindowsEditor*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){v=static_cast<EqWindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}
        if(!v)return DefWindowProcW(w,m,wp,lp);
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        switch(m) {
        case WM_PAINT:v->paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_COMMAND: {
            int id=LOWORD(wp);
            if(id>=20 && id<32){v->model.select(id-20);v->sync();}
            else if(id==50){v->model.write(enabled,SendMessageW(v->on,BM_GETCHECK,0,0)==BST_CHECKED?1:0);v->model.panelVisible=true;v->sync();}
            else if(id>=100 && std::size_t(id-100)<v->controls.size()) {
                if(HIWORD(wp)==EN_CHANGE && !v->refreshing)v->controls[id-100].dirty=true;
                if(HIWORD(wp)==CBN_SELCHANGE || HIWORD(wp)==EN_KILLFOCUS)v->edit(v->controls[id-100]);
            }return 0;
        }
        case WM_LBUTTONDOWN:if(PtInRect(&v->graph,p)){SetFocus(w);int b=v->hit(p);if(b>=0){v->model.beginDrag(b);v->resize(v->width,v->height);v->dragging=true;SetCapture(w);v->sync();}else{v->model.hidePanel();v->sync();}}return 0;
        case WM_LBUTTONDBLCLK:if(PtInRect(&v->graph,p)){v->model.create(v->hzFor(p.x));v->sync();}return 0;
        case WM_MOUSEMOVE:if(v->dragging){v->model.drag(v->hzFor(p.x),v->dbFor(p.y));v->sync();}return 0;
        case WM_LBUTTONUP:ReleaseCapture();v->dragging=false;v->model.cancelDrag();v->sync();return 0;
        case WM_CAPTURECHANGED:v->dragging=false;v->model.cancelDrag();v->sync();return 0;
        case WM_RBUTTONUP: {int b=v->hit(p);if(b>=0){v->model.select(b);auto menu=CreatePopupMenu();for(int f=0;f<3;++f)AppendMenuW(menu,MF_STRING,f+1,wide(fieldStyles[f].label).c_str());ClientToScreen(w,&p);int f=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,w,nullptr);DestroyMenu(menu);if(f)v->model.write(target,f-1);v->sync();}return 0;}
        case WM_MOUSEWHEEL:ScreenToClient(w,&p);if(PtInRect(&v->graph,p)){v->model.wheel(GET_WHEEL_DELTA_WPARAM(wp)*.05,(GetKeyState(VK_SHIFT)&0x8000)!=0);v->sync();}else if(v->model.advanced){v->scroll-=GET_WHEEL_DELTA_WPARAM(wp)/3;v->resize(v->width,v->height);}return 0;
        case WM_VSCROLL: {SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(w,SB_VERT,&si);switch(LOWORD(wp)){case SB_THUMBTRACK:v->scroll=si.nTrackPos;break;case SB_LINEUP:v->scroll-=30;break;case SB_LINEDOWN:v->scroll+=30;break;case SB_PAGEUP:v->scroll-=v->height;break;case SB_PAGEDOWN:v->scroll+=v->height;break;}v->resize(v->width,v->height);return 0;}
        case WM_KEYDOWN:if(wp==VK_DELETE || wp==VK_BACK){v->model.write(enabled,0);v->sync();}else if(wp==VK_ESCAPE){ReleaseCapture();v->model.cancelDrag();}return 0;
        }
        return DefWindowProcW(w,m,wp,lp);
    }
};
}
EditorContent* createEqEditor(){return new EqWindowsEditor;}
}
