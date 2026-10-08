#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "EditorModel.hpp"
#include "FeedbackHistory.hpp"
#include "PreviewLayout.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <cwchar>
namespace just::delay {
namespace {
// One registration per loaded module; instances may close in any order.
// Do not leave a WNDPROC pointing into an unloaded VST3 DLL.
struct ContentClass {
    std::mutex mutex;
    unsigned references=0;
    bool acquire(HMODULE module,WNDPROC procedure,const wchar_t* name) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!references){
            WNDCLASSW definition{};definition.lpfnWndProc=procedure;definition.hInstance=module;
            definition.lpszClassName=name;definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            if(!RegisterClassW(&definition)){
                WNDCLASSW existing{};
                if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS || !GetClassInfoW(module,name,&existing) || existing.lpfnWndProc!=procedure)return false;
            }
        }
        ++references;return true;
    }
    void release(HMODULE module,const wchar_t* name) {
        std::lock_guard<std::mutex> lock(mutex);
        if(references && --references==0)UnregisterClassW(name,module);
    }
};
ContentClass contentClass;
constexpr const wchar_t* contentClassName=L"JUST.Delay.Content.v010";
}
class WindowsContent final:public EditorContent {
    struct Row {HWND label=nullptr,control=nullptr;ParamID id=0;bool simple=false,custom=false;int y=0,x=0;std::unique_ptr<ControlServicesAdapter> adapter;std::unique_ptr<RotaryControl> rotary;std::wstring tooltip;};
    struct Group {std::size_t index=0;int y=0;};
    HWND view=nullptr,viewport=nullptr,note=nullptr,pingButton=nullptr,tooltips=nullptr;
    HMODULE module=nullptr;HFONT font=nullptr;HBRUSH background=nullptr;COLORREF backgroundColor=RGB(245,250,251);double fontScale=0;
    EditorServices services{};std::unique_ptr<Gesture> gesture;
    std::vector<Row> rows;std::vector<Group> groups;
    bool classAcquired=false,advanced=false;PingPongRoute pingRoute;FeedbackHistory analysisHistory;
    std::uint64_t visualResumeGeneration=0;
    RuntimeTelemetrySnapshot presentationTelemetry{};TelemetryAvailability presentationTelemetryAvailability=TelemetryAvailability::unavailable;
    int widthPixels=680,heightPixels=310,scroll=0,horizontal=0,totalHeight=0,viewportWidth=620,viewportHeight=240;
    std::wstring pingHelp,noteHelp;
    std::wstring text(const char* zh,const char* en) const {return win::wide(services.view?localized(*services.view,zh,en):en);}
    COLORREF color(int r,int g,int b) const {if(services.view && services.view->visualsPaused){const int y=(r*54+g*183+b*19)/256;return RGB(y,y,y);}return RGB(r,g,b);}
    void updateBackground(){
        const COLORREF next=color(245,250,251);if(background && next==backgroundColor)return;
        const HBRUSH replacement=CreateSolidBrush(next);if(!replacement)return;
        if(background)DeleteObject(background);background=replacement;backgroundColor=next;
        if(view)RedrawWindow(view,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    void tooltip(HWND target,std::wstring& saved,std::wstring next){
        if(saved==next)return;saved=std::move(next);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=view;info.uId=reinterpret_cast<UINT_PTR>(target);info.lpszText=saved.data();
        SendMessageW(tooltips,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
    }
    void paint(HWND window,HDC printDC=nullptr){
        win::Paint paint(window,true,printDC);auto& dc=paint.graphics();
        if(window==viewport){
            for(const auto& group:groups)win::text(dc,text(chineseGroups[group.index],moduleDefinition().advancedGroups[group.index].name),{float(12-horizontal),float(group.y-scroll),560,22},14,0x17333c,true);
            return;
        }
        if(advanced || heightPixels<180)return;
        const auto card=PreviewLayout::fit(widthPixels,heightPixels).graph;
        const float x=float(card.x),y=float(card.y),w=float(card.width),h=float(card.height);
        win::fill(dc,{x,y,w,h},0xf6fbfd,12);win::stroke(dc,{x,y,w,h},0xd6e6ec,12);
        const float left=x+42,right=x+w-18,first=y+h*.37f,second=y+h*.7f;
        for(int i=0;i<=10;++i){const float at=left+(right-left)*i/10;win::line(dc,at,y+30,at,y+h-24,0xd2e4e9,.6f);}
        for(float at:{first,second})win::line(dc,left,at,right,at,0xd2e4e9,.6f);
        const auto* latest=analysisHistory.latest();auto title=text("L / R 湿声重复 · 数据不可用","L / R wet repeats · data unavailable");
        if(analysisHistory.availability()==AnalysisAvailability::stale)title=text("L / R 湿声重复 · 暂无新音频","L / R wet repeats · no new audio");
        else if(analysisHistory.availability()==AnalysisAvailability::fresh && latest){
            if(!(latest->effectFields&analysisWet))title=text("湿声测量不可用","Wet measurement unavailable");
            else if(!(latest->header.flags&analysisInputAligned))title=text("等待 PDC 对齐","Waiting for PDC alignment");
            else if(latest->header.flags&analysisBypassed)title=text("实测 L / R 湿声重复 · 旁通","Actual L / R wet repeats · bypass");
            else if(latest->header.flags&analysisTransportKnown && !(latest->header.flags&analysisPlaying))title=text("实测 L / R 湿声重复 · 宿主停止","Actual L / R wet repeats · host stopped");
            else if(std::max(latest->channels[0].peak,latest->channels[1].peak)==0){
                bool historyHasSignal=false;analysisHistory.each([&](const AnalysisWindow& item){for(unsigned ch:{0u,1u,4u,5u})historyHasSignal=historyHasSignal || item.channels[ch].peak>0;});
                title=std::max(latest->channels[4].peak,latest->channels[5].peak)>0?text("输入静音 · 回声仍在继续","Input silent · repeats still active"):historyHasSignal?text("实测 L / R 湿声重复 · 历史（当前静音）","Actual L / R wet repeats · history (now silent)"):text("实测 L / R 湿声重复 · 静音","Actual L / R wet repeats · silence");
            }else title=text("实测 L / R 湿声重复 · 输入 / 湿声峰值","Actual L / R wet repeats · input / wet peaks");
        }
        win::text(dc,title,{x+180,y+10,std::max(1.f,w-196),14},9,0x789ea8);
        win::text(dc,text("回声轨迹 · 实测","ECHO TRAIL · measured"),{x+16,y+10,166,14},10,0x789ea8);
        win::text(dc,"L",{x+16,first-6,20,14},10,0x789ea8);win::text(dc,"R",{x+16,second-6,20,14},10,0x789ea8);
        win::text(dc,"−2500 ms",{left,y+h-18,100,14},10,0x789ea8);win::text(dc,text("现在","now"),{right-26,y+h-18,30,14},10,0x789ea8);
        if(latest && analysisHistory.availability()==AnalysisAvailability::fresh){
            const double end=double(latest->header.endSample),span=latest->header.sampleRate*2.5;const float amplitude=std::clamp(h*.13f,4.f,33.f);
            const auto saved=dc.Save();dc.SetClip(Gdiplus::RectF{left,y+30,right-left,std::max(1.f,h-54)});
            for(unsigned lane=0;lane<2;++lane)for(unsigned series=0;series<2;++series)analysisHistory.each([&](const AnalysisWindow& item){const auto& header=item.header;
                if(!(header.flags&analysisInputAligned) || (series && !(item.effectFields&analysisWet)) || (lane && header.outputChannels<2))return;
                const float at=float(right-(end-double(header.endSample))/span*(right-left));if(at<left || at>right)return;
                const unsigned channel=series?4+lane:(header.inputChannels==1?0:lane);const double peak=std::clamp(item.channels[channel].peak,0.,1.);if(peak<=1e-8)return;
                const float row=lane?second:first,a=float(peak*amplitude);win::line(dc,at,row-a,at,row+a,series?0x12b8c7:0x6694a1,series?3.f:2.f);
            });dc.Restore(saved);
        }
    }
    void drawPing(const DRAWITEMSTRUCT& item){
        updateBackground();
        const bool on=value(editorTargets(services),route)==2;HDC dc=item.hDC;const int saved=SaveDC(dc);const double scale=win::scale(pingButton);
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,1000,nullptr);SetViewportExtEx(dc,int(std::lround(scale*1000)),int(std::lround(scale*1000)),nullptr);
        const RECT rect{0,0,int(std::lround(item.rcItem.right/scale)),int(std::lround(item.rcItem.bottom/scale))};SetBkMode(dc,TRANSPARENT);FillRect(dc,&rect,background);
        HFONT drawFont=CreateFontW(-11,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,drawFont);
        auto fill=CreateSolidBrush(on?color(217,245,245):RGB(255,255,255));auto border=CreatePen(PS_SOLID,1,on?color(132,212,217):color(213,226,231));
        auto oldFill=SelectObject(dc,fill),oldPen=SelectObject(dc,border);RoundRect(dc,rect.left+1,rect.top+1,rect.right-1,rect.bottom-1,32,32);SelectObject(dc,oldFill);SelectObject(dc,oldPen);DeleteObject(fill);DeleteObject(border);
        SetTextColor(dc,on?color(0,125,138):color(99,131,143));RECT label{14,0,90,rect.bottom};DrawTextW(dc,L"Ping Pong",-1,&label,DT_SINGLELINE|DT_VCENTER);
        auto track=CreateSolidBrush(on?color(27,185,197):color(214,226,229));oldFill=SelectObject(dc,track);oldPen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,92,7,123,24,17,17);SelectObject(dc,oldFill);DeleteObject(track);
        oldFill=SelectObject(dc,GetStockObject(WHITE_BRUSH));Ellipse(dc,on?109:95,10,on?120:106,21);SelectObject(dc,oldFill);SelectObject(dc,oldPen);
        RECT state{136,0,rect.right-12,rect.bottom};DrawTextW(dc,on?L"ON":L"OFF",-1,&state,DT_SINGLELINE|DT_VCENTER);
        if(item.itemState&ODS_FOCUS){RECT focus=rect;InflateRect(&focus,-3,-3);DrawFocusRect(dc,&focus);}SelectObject(dc,oldFont);DeleteObject(drawFont);RestoreDC(dc,saved);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);
        if(message==WM_PAINT){self->paint(window);return 0;}
        if(message==WM_PRINTCLIENT){self->paint(window,reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_DRAWITEM && wp==40000){self->drawPing(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
        if(message==WM_COMMAND && LOWORD(wp)==40000 && HIWORD(wp)==BN_CLICKED){
            const int next=self->pingRoute.next(editorTargets(self->services));if(self->gesture->begin(route)){self->gesture->update(double(next)/spec(route).stepCount);self->gesture->end();}
            self->refresh(*self->services.view,{});return 0;
        }
        if(message==WM_COMMAND && HIWORD(wp)==CBN_SELCHANGE){
            for(const auto& row:self->rows)if(row.control==reinterpret_cast<HWND>(lp) && editable(row.id,editorTargets(self->services),self->advanced)){
                if(self->gesture->begin(row.id)){self->gesture->update(double(SendMessageW(row.control,CB_GETCURSEL,0,0))/spec(row.id).stepCount);self->gesture->end();}self->refresh(*self->services.view,{});return 0;
            }
        }
        if(self->advanced && (message==WM_VSCROLL || message==WM_HSCROLL || message==WM_MOUSEWHEEL)){
            const bool horizontal=message==WM_HSCROLL;int& position=horizontal?self->horizontal:self->scroll;const int page=horizontal?self->viewportWidth:self->viewportHeight;
            if(message==WM_MOUSEWHEEL)position-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*44;
            else switch(LOWORD(wp)){case SB_LINEUP:position-=44;break;case SB_LINEDOWN:position+=44;break;case SB_PAGEUP:position-=page;break;case SB_PAGEDOWN:position+=page;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(self->viewport,horizontal?SB_HORZ:SB_VERT,&info);position=info.nTrackPos;break;}}
            self->layout();return 0;
        }
        if(message==WM_CTLCOLORSTATIC){self->updateBackground();HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,self->backgroundColor);SetTextColor(dc,self->color(99,131,143));return reinterpret_cast<LRESULT>(self->background);}
        if(message==WM_CAPTURECHANGED && self->gesture)self->gesture->end();
        return DefWindowProcW(window,message,wp,lp);
    }
    HWND child(HWND parent,const wchar_t* kind,const wchar_t* title,DWORD style,int id){
        HWND h=CreateWindowExW(0,kind,title,WS_CHILD|style,0,0,1,1,parent,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;
    }
    bool add(ParamID id,bool simple,int y,int x){
        const auto& p=spec(id);Row row;row.id=id;row.simple=simple;row.y=y;row.x=x;HWND parent=simple?view:viewport;
        if(p.enumLabels){row.label=child(parent,L"STATIC",win::wide(p.title).c_str(),SS_LEFT,0);row.control=child(parent,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,int(id));
            if(!row.label || !row.control)return false;for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(row.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(p.enumLabels[n]).c_str()));
        }else{
            row.adapter=std::make_unique<ControlServicesAdapter>(services,id);auto display=p;if(simple && id==timeL)display.title="Time";
            row.rotary=RotaryControl::create(parent,row.adapter->services(),display,controlPolicy(id,false,simple,services.view));if(!row.rotary)return false;row.control=static_cast<HWND>(row.rotary->nativeHandle());
        }
        rows.push_back(std::move(row));return true;
    }
    void layout(){
        const double scale=win::scale(view);if(fontScale!=scale){fontScale=scale;if(font)DeleteObject(font);font=CreateFontW(-int(std::lround(11*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            for(const auto& row:rows)if(!row.rotary){SendMessageW(row.label,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(row.control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}SendMessageW(note,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
        ShowWindow(viewport,advanced?SW_SHOW:SW_HIDE);ShowWindow(pingButton,advanced?SW_HIDE:SW_SHOW);
        const auto geometry=PreviewLayout::fit(widthPixels,heightPixels);win::place(pingButton,geometry.ping.x,geometry.ping.y,geometry.ping.width,geometry.ping.height);
        viewportWidth=std::max(1,widthPixels-16-int(std::ceil(GetSystemMetrics(SM_CXVSCROLL)/scale)));viewportHeight=std::max(1,heightPixels-66-int(std::ceil(GetSystemMetrics(SM_CYHSCROLL)/scale)));
        win::place(viewport,8,8,std::max(1,widthPixels-16),std::max(1,heightPixels-66));
        horizontal=std::clamp(horizontal,0,std::max(0,620-viewportWidth));scroll=std::clamp(scroll,0,std::max(0,totalHeight-viewportHeight));
        SCROLLINFO verticalInfo{sizeof(verticalInfo),SIF_RANGE|SIF_PAGE|SIF_POS,0,totalHeight-1,UINT(viewportHeight),scroll,0};SetScrollInfo(viewport,SB_VERT,&verticalInfo,TRUE);
        SCROLLINFO horizontalInfo{sizeof(horizontalInfo),SIF_RANGE|SIF_PAGE|SIF_POS,0,619,UINT(viewportWidth),horizontal,0};SetScrollInfo(viewport,SB_HORZ,&horizontalInfo,TRUE);
        for(auto& row:rows){const bool visible=row.simple!=advanced;ShowWindow(row.control,visible?SW_SHOW:SW_HIDE);if(row.label)ShowWindow(row.label,visible?SW_SHOW:SW_HIDE);if(!visible)continue;
            if(row.simple){const auto& cell=geometry.cells[row.x];row.rotary->resize(cell.x,cell.y,cell.width,cell.height);}
            else{const int x=12+row.x*300-horizontal,y=row.y-scroll;if(row.rotary)row.rotary->resize(x,y,270,136);else {win::place(row.label,x,y,280,19);win::place(row.control,x,y+22,270,260);}}
        }
        ShowWindow(note,!advanced && geometry.note.height==0?SW_HIDE:SW_SHOW);
        if(advanced)win::place(note,20,std::max(0,heightPixels-50),std::max(1,widthPixels-40),40);else win::place(note,geometry.note.x,geometry.note.y,geometry.note.width,geometry.note.height);
        InvalidateRect(view,nullptr,FALSE);InvalidateRect(viewport,nullptr,FALSE);
    }
    void close(){
        if(gesture)gesture->end();rows.clear();groups.clear();
        if(tooltips)DestroyWindow(tooltips);tooltips=nullptr;
        if(view)DestroyWindow(view);view=viewport=note=pingButton=nullptr;
        if(font)DeleteObject(font);font=nullptr;fontScale=0;
        if(background)DeleteObject(background);background=nullptr;
        if(classAcquired){contentClass.release(module,contentClassName);classAcquired=false;}
        gesture.reset();pingHelp.clear();noteHelp.clear();
    }
    bool failAttach(){close();return false;}
public:
    ~WindowsContent() override {close();}
    bool attach(void* parent,const EditorServices& s) override {
        if(!parent || view || classAcquired || !s.view || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;gesture=std::make_unique<Gesture>(s);
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);module=win::moduleAt(reinterpret_cast<const void*>(&procedure));updateBackground();
        if(!module || !contentClass.acquire(module,procedure,contentClassName))return failAttach();classAcquired=true;
        view=CreateWindowExW(0,contentClassName,L"JUST Delay",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,widthPixels,heightPixels,static_cast<HWND>(parent),nullptr,module,this);if(!view)return failAttach();
        viewport=CreateWindowExW(0,contentClassName,L"Advanced",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,0,0,620,240,view,nullptr,module,this);if(!viewport)return failAttach();
        tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,view,nullptr,module,nullptr);if(!tooltips)return failAttach();SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,460);
        int i=0;for(auto id:{timeL,feedback,mix})if(!add(id,true,0,i++))return failAttach();
        int y=18;for(std::size_t group=0;group<moduleDefinition().advancedGroupCount;++group){const auto& g=moduleDefinition().advancedGroups[group];groups.push_back({group,y});y+=30;
            for(std::size_t j=0;j<g.count;++j)if(!add(g.parameters[j],false,y+int(j/2)*148,int(j%2)))return failAttach();y+=int((g.count+1)/2)*148+20;}
        totalHeight=y;note=child(view,L"STATIC",L"",SS_LEFT,0);pingButton=child(view,L"BUTTON",L"Ping Pong",BS_OWNERDRAW|WS_TABSTOP,40000);
        if(!note || !pingButton)return failAttach();refresh(*s.view,{});return true;
    }
    void resize(int w,int h) override {widthPixels=w;heightPixels=h;win::place(view,0,0,w,h);layout();}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {
        updateBackground();
        if(advanced!=state.advanced){gesture->end();scroll=horizontal=0;}advanced=state.advanced;const auto targets=editorTargets(services);pingRoute.observe(targets);
        if(visualResumeGeneration!=state.visualResumeGeneration){analysisHistory={};presentationTelemetry={};presentationTelemetryAvailability=TelemetryAvailability::unavailable;visualResumeGeneration=state.visualResumeGeneration;}
        if(!state.visualsPaused)analysisHistory.poll(services);
        SetWindowTextW(pingButton,value(targets,route)==2?L"Ping Pong  ON":L"Ping Pong  OFF");InvalidateRect(pingButton,nullptr,TRUE);
        tooltip(pingButton,pingHelp,text("编辑现有路由参数。关闭恢复先前的立体声或双单声道路由，其余声音参数保持。","Edits the existing Route parameter. Off restores the previous Stereo or Dual Mono route; other sound targets stay saved."));
        for(auto& row:rows){const bool visible=row.simple!=advanced,enabled=visible && editable(row.id,targets,advanced);
            if(row.label)SetWindowTextW(row.label,text(chineseLabel(row.id),spec(row.id).title).c_str());
            if(row.rotary){const bool custom=row.simple && row.id==timeL && !simpleTimeEditable(targets);
                if(custom!=row.custom){auto display=spec(row.id);display.title="Time";if(custom)display.unit="";
                    auto replacement=RotaryControl::create(view,row.adapter->services(),display,controlPolicy(row.id,custom,row.simple,services.view));
                    if(replacement){TOOLINFOW info{sizeof(info)};info.hwnd=view;info.uId=reinterpret_cast<UINT_PTR>(row.control);SendMessageW(tooltips,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&info));row.rotary=std::move(replacement);row.control=static_cast<HWND>(row.rotary->nativeHandle());row.custom=custom;row.tooltip.clear();}}
                if(row.rotary)row.rotary->refresh(enabled);
            }else{EnableWindow(row.control,enabled);SendMessageW(row.control,CB_SETCURSEL,std::size_t(value(targets,row.id)),0);}
            if(row.simple && row.custom)tooltip(row.control,row.tooltip,text("时间：自定义。请在 Advanced 编辑独立时间或同步。","Time: Custom. Edit independent times or Sync in Advanced."));
            else if(row.id==crossfeed && !enabled)tooltip(row.control,row.tooltip,text("Ping-Pong 使用 100% 交叉反馈，已保存的值保留。","Ping-Pong uses 100% cross feedback. Stored value is preserved."));
            else tooltip(row.control,row.tooltip,text("向上拖动增大；Shift 精调；双击复位。点击数值输入。","Drag up to increase; Shift refines; double click resets. Click the value to type."));
        }
        if(!state.visualsPaused)presentationTelemetryAvailability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,presentationTelemetry):TelemetryAvailability::unavailable;
        std::wstring tempo=text("宿主速度数据不可用","Host tempo unavailable");
        if(presentationTelemetryAvailability==TelemetryAvailability::fresh && (presentationTelemetry.validFields&telemetryTempo)){wchar_t valueText[96];std::swprintf(valueText,std::size(valueText),text("宿主速度 %.1f BPM","Host tempo %.1f BPM").c_str(),presentationTelemetry.bpm);tempo=valueText;}
        std::wstring message;
        if(advanced)message=text("Ping-Pong 将立体声输入汇入起始侧。","Ping-Pong sums stereo input to the start side. ")+tempo+text("。冻结不保存录音内容。",". Freeze does not save recorded audio.");
        else{message=L"L: ";message+=value(targets,syncL)?win::wide(notes[int(value(targets,noteL))]):text("自由 ms","Free ms");message+=L" · R: ";message+=value(targets,syncR)?win::wide(notes[int(value(targets,noteR))]):text("自由 ms","Free ms");message+=L" · "+tempo;if(!simpleTimeEditable(targets))message+=text(" · 时间：自定义 — Advanced"," · Time: Custom — Advanced");}
        const auto* measured=analysisHistory.latest();if(analysisHistory.availability()==AnalysisAvailability::fresh && measured && (measured->effectFields&analysisDelay)){wchar_t actual[96];std::swprintf(actual,std::size(actual),text(" · 实测 L/R %.1f / %.1f ms"," · Actual L/R %.1f / %.1f ms").c_str(),measured->delayMs[0],measured->delayMs[1]);message+=actual;}
        SetWindowTextW(note,message.c_str());wchar_t fallback[192];std::swprintf(fallback,std::size(fallback),text("已保存备用速度：%.1f BPM。仅在处理器数据可用时显示实测数值。","Saved Fallback Tempo target: %.1f BPM. Actual values are shown only when available from processing.").c_str(),value(targets,localTempo));tooltip(note,noteHelp,fallback);
        layout();
    }
};
EditorContent* createEditorContent(){return new WindowsContent;}
}
#endif
