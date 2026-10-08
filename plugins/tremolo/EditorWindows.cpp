#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cwchar>
#include <vector>
#include <string>
#include <mutex>
#include "Editor.hpp"
#include "EditorModel.hpp"
#include "AmountDisplay.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"

namespace just::tremolo {
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
constexpr const wchar_t* contentClassName=L"JUST.Tremolo.Content.v010";
std::wstring wide(const char* text) {
    const int size=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    if(size<1)return {};
    std::wstring result(std::size_t(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),size);result.resize(std::size_t(size-1));return result;
}
const char* translation(ParamID id) {
    switch(id){case rateHz:return "自由速率";case depth:return "深度";case mix:return "混合";
        case phase:return "相位";case stereoPhase:return "立体声相位";case duty:return "占空比";
        case edgeMs:return "边缘";case inputGain:return "输入增益";case outputGain:return "输出增益";
        case shape:return "波形";case sync:return "同步";case division:return "拍分";case timeMode:return "时间模式";default:return "";}
}
bool rateParse(const char* text,double& normalized) {
    if(spec(rateHz).parse(text,normalized))return true;
    char* end=nullptr;double ms=std::strtod(text,&end);while(end && (*end==' ' || *end=='\t'))++end;
    if(end==text || !end || std::strcmp(end,"ms") || !std::isfinite(ms) || ms<=0)return false;
    const double hz=1000/ms;if(hz<spec(rateHz).minimum || hz>spec(rateHz).maximum)return false;
    normalized=spec(rateHz).toNormalized(hz);return true;
}
void phaseFormat(double n,DisplayContext context,char* text,std::size_t size) {
    const double value=spec(phase).toPhysical(n);
    if(context==DisplayContext::editing)std::snprintf(text,size,"%.17g",value);
    else std::snprintf(text,size,"%.1f",fraction(value/360)*360);
}
struct Rotary {ParamID id=0;int primary=-1;unsigned slot=0;std::unique_ptr<RotaryControl> control;std::wstring tooltip;};
struct Choice {ParamID id=0;unsigned slot=0;HWND label=nullptr,control=nullptr;};
class WindowsEditor final:public EditorContent {
    std::unique_ptr<EditorModel> model;
    std::unique_ptr<AmountDisplay> amount;
    HWND window=nullptr,timing=nullptr,tooltips=nullptr;
    HMODULE module=nullptr;HFONT font=nullptr,controlFont=nullptr;double fontScale=0;HBRUSH background=nullptr;COLORREF backgroundColor=RGB(245,250,251);
    std::vector<Rotary> rotaries;std::vector<Choice> choices;
    unsigned advancedSlots=0;
    bool classAcquired=false,advanced=false;
    int width=1120,height=460,offset=0,documentHeight=460;
    RECT graph{};int primaryTop=0,advancedTop=0,documentWidth=1120;
    UiLanguage language=UiLanguage::english;bool languageInitialized=false;
    std::array<AnalysisWindow,600> history{};std::size_t count=0,write=0;
    AnalysisCursor cursor{};AnalysisAvailability availability=AnalysisAvailability::unavailable;
    std::uint64_t resumeGeneration=0;
    bool paused() const {const auto* view=model->editorServices().view;return view && view->visualsPaused;}
    COLORREF color(int r,int g,int b) const {if(paused()){const int y=(r*54+g*183+b*19)/256;return RGB(y,y,y);}return RGB(r,g,b);}
    void updateBackground(){
        const COLORREF next=color(245,250,251);if(background && next==backgroundColor)return;
        const HBRUSH replacement=CreateSolidBrush(next);if(!replacement)return;
        if(background)DeleteObject(background);background=replacement;backgroundColor=next;
        if(window)RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    std::wstring text(const char* zh,const char* en) const {const auto* v=model->editorServices().view;return wide(v?localized(*v,zh,en):en);}
    const AnalysisWindow* latest() const {return count?&history[(write+history.size()-1)%history.size()]:nullptr;}
    void poll() {
        const auto& services=model->editorServices();const auto* state=services.view;
        if(state && resumeGeneration!=state->visualResumeGeneration){count=write=0;cursor={};availability=AnalysisAvailability::unavailable;resumeGeneration=state->visualResumeGeneration;}
        if(paused())return;
        availability=AnalysisAvailability::unavailable;
        for(unsigned attempt=0;attempt<16 && services.readAnalysis;++attempt){
            AnalysisBatch batch{};availability=services.readAnalysis(services.owner,cursor,batch);
            if(availability!=AnalysisAvailability::fresh || !batch.count)break;
            for(unsigned n=0;n<batch.count;++n){const auto& item=batch.windows[n];if(!validAnalysisHeader(item.header))continue;
                if(const auto* old=latest())if(old->header.session!=item.header.session || old->header.epoch!=item.header.epoch)count=write=0;
                history[write]=item;write=(write+1)%history.size();count=std::min(count+1,history.size());
            }
        }
    }
    bool editable(ParamID id) const {
        if(id==rateHz)return model->frequencyEditable();if(id==division)return !model->frequencyEditable();
        if(id==stereoPhase)return model->separationEditable();if(id==duty)return model->value(shape)==2;
        if(id==edgeMs)return model->value(shape)>=2;return true;
    }
    void drawText(HDC dc,const std::wstring& value,RECT rect,UINT flags=DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS) {
        DrawTextW(dc,value.c_str(),-1,&rect,flags);
    }
    void paint(HDC printDC=nullptr) {
        win::Paint paint(window,true,printDC);auto& dc=paint.graphics();
        const float x=float(graph.left),y=float(graph.top),w=float(graph.right-graph.left),h=float(graph.bottom-graph.top);
        win::fill(dc,{x+1,y+1,w-2,h-2},0xf5fbfc,12);win::stroke(dc,{x+1,y+1,w-2,h-2},0xd4e6eb,12);
        win::text(dc,text("立体声振幅包络","STEREO AMPLITUDE ENVELOPE"),{x+18,y+15,w-188,16},10,0x6e96a3);
        const Gdiplus::RectF plot{x+24,y+48,std::max(1.f,w-48),std::max(1.f,h-78)};
        for(unsigned n=0;n<=4;++n){const float at=plot.Y+plot.Height*n/4;win::line(dc,plot.X,at,plot.GetRight(),at,0xd8ebef);}
        for(unsigned n=0;n<=12;++n){const float at=plot.X+plot.Width*n/12;win::line(dc,at,plot.Y,at,plot.GetBottom(),0xd8ebef);}
        const auto* newest=latest();std::wstring state=L"L / R · 0–1× · 6 s";
        if(availability!=AnalysisAvailability::fresh || !newest || !(newest->effectFields&analysisModulation)) {
            win::text(dc,availability==AnalysisAvailability::stale?text("暂无新音频","No new audio"):text("音频反馈不可用","Audio feedback unavailable"),{plot.X+8,plot.Y+plot.Height/2-6,plot.Width-8,16},10,0x6e96a3);
        } else {
            bool above=false;const double span=newest->header.sampleRate*6.;const auto saved=dc.Save();dc.SetClip(plot);
            for(unsigned channel=0;channel<std::min(2u,unsigned(newest->header.outputChannels));++channel){
                Gdiplus::GraphicsPath path;bool started=false;std::uint64_t next=0;Gdiplus::PointF previous;
                for(std::size_t i=0;i<count;++i){const auto& item=history[(write+history.size()-count+i)%history.size()];
                    if(!(item.effectFields&analysisModulation)){started=false;continue;}
                    const float at=float(plot.GetRight()-(double(newest->header.endSample)-double(item.header.endSample))/span*plot.Width);
                    if(at<plot.X || at>plot.GetRight()){started=false;continue;}
                    above|=item.modulationMax[channel]>1.;
                    auto level=[&](double gain){return plot.Y+float((1-std::clamp(gain,0.,1.))*plot.Height);};
                    const Gdiplus::PointF high{at,level(item.modulationMax[channel])},low{at,level(item.modulationMin[channel])};
                    if(!started || next!=item.header.startSample || (item.header.flags&analysisGap))path.StartFigure();else path.AddLine(previous,high);
                    path.AddLine(high,low);previous=low;started=true;next=item.header.endSample;
                }
                Gdiplus::Pen pen(win::color(channel?0x82bac7:0x0fb5c4),channel?1.3f:2.f);if(channel){const float dash[]={5,3};pen.SetDashPattern(dash,2);}dc.DrawPath(&pen,&path);
            }
            dc.Restore(saved);
            if(newest->header.outputChannels==1)state=L"L · 0–1× · 6 s";
            if(above)state+=text(" · 超出刻度"," · above scale");
            if(newest->header.flags&analysisBypassed)state+=text(" · 旁路"," · Bypass");
            else if(!(newest->header.flags&analysisPlaying) && (newest->header.flags&analysisTransportKnown))state+=text(" · 停止"," · Stopped");
            if(newest->channels[0].peak==0 && newest->channels[1].peak==0)state+=text(" · 静音"," · Silence");
        }
        win::text(dc,state,{x+24,y+h-23,w-48,16},10,0x6e96a3);
        std::wstring readout;
        if(!model->frequencyEditable()) {
            if(availability==AnalysisAvailability::fresh && newest && (newest->effectFields&analysisModulation) && newest->effectiveHz>0){
                const double hz=newest->effectiveHz;const int decimals=hz<.1?std::min(8,int(std::ceil(-std::log10(hz)))+1):hz<1?2:1;
                wchar_t value[64];std::swprintf(value,std::size(value),L"%.*f Hz",decimals,hz);readout=value;
                if(newest->effectFlags&1)readout+=text(" · 速度回退"," · Tempo fallback");else if(newest->effectFlags&2)readout+=text(" · 位置不可用"," · Position unavailable");
            } else readout=text("同步 · 等待音频","Sync · waiting for audio");
        }else if(model->value(timeMode)==1)readout=text("Transport 需要同步","Transport needs Sync");
        const float readoutWidth=float(std::max(80,std::min(210,documentWidth-470)));
        win::text(dc,readout,{documentWidth-194-readoutWidth,y+16,readoutWidth,18},10,0x6e96a3,false,Gdiplus::StringAlignmentFar);
        if(advanced)win::text(dc,text("进阶控制","ADDITIONAL CONTROLS"),{28,float(advancedTop-26-offset),float(documentWidth-56),20},10,0x6e96a3);
    }
    void drawTiming(const DRAWITEMSTRUCT& item) {
        updateBackground();
        HDC dc=item.hDC;const int saved=SaveDC(dc);const double scale=win::scale(timing);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,1000,nullptr);SetViewportExtEx(dc,int(std::lround(scale*1000)),int(std::lround(scale*1000)),nullptr);RECT rect{0,0,int(std::lround(item.rcItem.right/scale)),int(std::lround(item.rcItem.bottom/scale))};const bool synced=!model->frequencyEditable();
        auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);FillRect(dc,&rect,background);
        for(int n=0;n<2;++n){RECT half{n*(rect.right/2),0,(n+1)*(rect.right/2),rect.bottom};const bool on=n==int(synced);
            auto brush=CreateSolidBrush(on?color(217,245,245):RGB(255,255,255));auto pen=CreatePen(PS_SOLID,1,color(212,230,235));
            auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,half.left,half.top,half.right,half.bottom,10,10);
            SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
            SetTextColor(dc,on?color(0,125,138):color(99,131,143));drawText(dc,n?text("同步","Sync"):text("自由","Free"),half,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        if(item.itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);DrawFocusRect(dc,&rect);}SelectObject(dc,oldFont);RestoreDC(dc,saved);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,w,l);
        if(msg==WM_PAINT){self->paint();return 0;}
        if(msg==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(w));return 0;}
        if(msg==WM_DRAWITEM && w==40000){self->drawTiming(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(msg==WM_COMMAND && LOWORD(w)==40000 && HIWORD(w)==BN_CLICKED){
            // Native keyboard activation toggles; the subclass selects the
            // actual mouse segment and suppresses its duplicate notification.
            const int next=self->model->frequencyEditable()?1:0;
            self->model->writePhysical(sync,next);self->model->cancel();self->refreshValues();return 0;
        }
        if(msg==WM_COMMAND && HIWORD(w)==CBN_SELCHANGE){
            for(const auto& choice:self->choices)if(choice.control==reinterpret_cast<HWND>(l) && self->editable(choice.id)){
                self->model->writePhysical(choice.id,double(SendMessageW(choice.control,CB_GETCURSEL,0,0)));self->model->cancel();self->refreshValues();return 0;
            }
        }
        if(msg==WM_VSCROLL || msg==WM_MOUSEWHEEL){
            if(msg==WM_MOUSEWHEEL)self->offset-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*44;
            else switch(LOWORD(w)){case SB_LINEUP:self->offset-=44;break;case SB_LINEDOWN:self->offset+=44;break;case SB_PAGEUP:self->offset-=self->height;break;case SB_PAGEDOWN:self->offset+=self->height;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&si);self->offset=si.nTrackPos;break;}}
            self->offset=std::clamp(self->offset,0,std::max(0,self->documentHeight-self->height));self->layout();return 0;
        }
        if(msg==WM_CTLCOLORSTATIC){self->updateBackground();HDC dc=reinterpret_cast<HDC>(w);SetBkColor(dc,self->backgroundColor);SetTextColor(dc,self->color(23,51,61));return reinterpret_cast<LRESULT>(self->background);}
        return DefWindowProcW(hwnd,msg,w,l);
    }
    static LRESULT CALLBACK timingProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<WindowsEditor*>(data);
        if(msg==WM_LBUTTONUP && GetCapture()==hwnd){RECT rect;GetClientRect(hwnd,&rect);const int next=GET_X_LPARAM(l)>=rect.right/2;
            if(GET_X_LPARAM(l)>=0 && GET_X_LPARAM(l)<rect.right && GET_Y_LPARAM(l)>=0 && GET_Y_LPARAM(l)<rect.bottom){
                self->model->writePhysical(sync,next);self->model->cancel();self->refreshValues();
                // Let the native button release capture, suppressing its toggle
                // notification because the desired segment was already set.
                SetWindowLongPtrW(hwnd,GWLP_ID,40001);const auto result=DefSubclassProc(hwnd,msg,w,l);SetWindowLongPtrW(hwnd,GWLP_ID,40000);return result;
            }
        }
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,timingProcedure,1);
        return DefSubclassProc(hwnd,msg,w,l);
    }
    HWND child(const wchar_t* kind,const wchar_t* title,DWORD style,int id) {
        HWND h=CreateWindowExW(0,kind,title,WS_CHILD|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;
    }
    bool addControl(ParamID id,const char* title,int primary) {
        auto display=spec(id);if(title)display.title=title;if(id==phase || id==stereoPhase)display.unit="°";
        DisplayPolicy policy;policy.labelZh=primary==0?(id==division?"节拍":"频率"):primary==2?"立体声分离":translation(id);
        if(id==rateHz)policy.parse=rateParse;if(id==phase)policy.format=phaseFormat;
        auto control=RotaryControl::create(window,model->editorServices(),display,policy);if(!control)return false;
        rotaries.push_back({id,primary,primary<0?advancedSlots++:0,std::move(control),{}});return true;
    }
    bool addAmount() {
        amount=std::make_unique<AmountDisplay>(model->editorServices());DisplayPolicy policy;policy.labelZh="幅度";policy.context=amount.get();
        policy.formatWithContext=[](void* p,double n,DisplayContext c,char* out,std::size_t size){static_cast<AmountDisplay*>(p)->format(n,c,out,size);};
        policy.parseWithContext=[](void* p,const char* text,double& n){return static_cast<AmountDisplay*>(p)->parse(text,n);};
        auto control=RotaryControl::create(window,amount->controlServices(),amount->controlSpec(),policy);if(!control)return false;
        rotaries.push_back({depth,1,0,std::move(control),{}});return true;
    }
    bool addChoice(ParamID id) {
        Choice choice{id,advancedSlots++};choice.label=child(L"STATIC",L"",SS_CENTER,0);
        choice.control=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,int(id));
        if(!choice.label || !choice.control)return false;choices.push_back(choice);return true;
    }
    void updateTooltip(Rotary& item,const std::wstring& next) {
        if(item.tooltip==next)return;item.tooltip=next;
        TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(item.control->nativeHandle());info.lpszText=item.tooltip.data();
        SendMessageW(tooltips,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
    }
    void refreshValues() {
        updateBackground();
        const bool synced=!model->frequencyEditable();const auto* state=model->editorServices().view;
        const bool relabel=!languageInitialized || (state && language!=state->language);
        if(state)language=state->language;languageInitialized=true;
        for(auto& item:rotaries){
            const bool visible=item.primary>=0?!(item.primary==0 && ((item.id==division)!=synced)):advanced;
            const bool enabled=visible && (item.primary==1?model->amountEditable():editable(item.id));
            ShowWindow(static_cast<HWND>(item.control->nativeHandle()),visible?SW_SHOW:SW_HIDE);item.control->refresh(enabled);
            if(item.primary==1 && !enabled)updateTooltip(item,text("Mix 为 0%；已保存 Depth 保留","Mix is 0%; saved Depth is retained"));
            else if(item.id==stereoPhase && !model->separationEditable())updateTooltip(item,text("单声道或声道未知；已保存相位保留","Mono or unknown bus; saved phase is retained"));
            else updateTooltip(item,text("向上拖动增大；Shift 精调；双击复位。点击数值输入。","Drag up to increase; Shift refines; double click resets. Click the value to type."));
        }
        for(auto& choice:choices){ShowWindow(choice.label,advanced?SW_SHOW:SW_HIDE);ShowWindow(choice.control,advanced?SW_SHOW:SW_HIDE);EnableWindow(choice.control,advanced && editable(choice.id));
            if(relabel){
                SetWindowTextW(choice.label,text(translation(choice.id),spec(choice.id).title).c_str());SendMessageW(choice.control,CB_RESETCONTENT,0,0);
                for(unsigned n=0;n<=spec(choice.id).stepCount;++n){const char* zh=spec(choice.id).enumLabels[n];
                    static constexpr const char* waveformZh[]={"正弦","三角","方波","上行锯齿","下行锯齿"};static constexpr const char* timeZh[]={"自由","跟随播放位置","播放时重触发"};
                    if(choice.id==shape)zh=waveformZh[n];else if(choice.id==sync)zh=n?"开启":"关闭";else if(choice.id==timeMode)zh=timeZh[n];
                    SendMessageW(choice.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text(zh,spec(choice.id).enumLabels[n]).c_str()));
                }
            }
            SendMessageW(choice.control,CB_SETCURSEL,int(model->value(choice.id)),0);
        }
        SetWindowTextW(timing,text("自由 / 同步","Free / Sync").c_str());InvalidateRect(timing,nullptr,TRUE);layout();
    }
    void layout() {
        const double currentScale=win::scale(window);
        if(currentScale!=fontScale){fontScale=currentScale;if(controlFont)DeleteObject(controlFont);controlFont=CreateFontW(-int(std::lround(12*fontScale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto& choice:choices){SendMessageW(choice.label,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);SendMessageW(choice.control,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);}}
        documentWidth=std::max(1,width-(advanced?int(std::ceil(GetSystemMetrics(SM_CXVSCROLL)/win::scale(window))):0));const int margin=height>=400?24:12;
        const int graphHeight=std::max(96,std::min(244,height-184));primaryTop=margin+graphHeight+16;
        const unsigned columns=std::max(2,(documentWidth-48)/166);const int cell=(documentWidth-48)/int(columns);
        advancedTop=primaryTop+144+44;documentHeight=advanced?std::max(height,advancedTop+int((advancedSlots+columns-1)/columns)*160+16):height;
        offset=std::clamp(offset,0,std::max(0,documentHeight-height));graph={24,margin-offset,documentWidth-24,margin+graphHeight-offset};
        win::place(timing,documentWidth-184,margin+12-offset,146,26);
        const int gap=std::min(100,std::max(24,(documentWidth-3*156)/4)),left=(documentWidth-(3*156+2*gap))/2;
        for(auto& item:rotaries)if(item.primary>=0)item.control->resize(left+item.primary*(156+gap),primaryTop-offset,156,144);
        else item.control->resize(24+(item.slot%columns)*cell,advancedTop+int(item.slot/columns)*160-offset,cell-8,148);
        for(auto& choice:choices){const int x=24+(choice.slot%columns)*cell,y=advancedTop+int(choice.slot/columns)*160-offset;
            win::place(choice.label,x,y,cell-8,20);win::place(choice.control,x+4,y+54,cell-16,260);}
        ShowScrollBar(window,SB_VERT,advanced);SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,documentHeight-1),UINT(std::max(1,height)),offset,0};SetScrollInfo(window,SB_VERT,&si,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void close(){
        if(model)model->cancel();rotaries.clear();choices.clear();
        if(tooltips)DestroyWindow(tooltips);tooltips=nullptr;
        if(window)DestroyWindow(window);window=timing=nullptr;
        if(font)DeleteObject(font);font=nullptr;
        if(controlFont)DeleteObject(controlFont);controlFont=nullptr;fontScale=0;
        if(background)DeleteObject(background);background=nullptr;
        if(classAcquired){contentClass.release(module,contentClassName);classAcquired=false;}
        amount.reset();model.reset();advancedSlots=0;languageInitialized=false;
    }
    bool failAttach(){close();return false;}
public:
    ~WindowsEditor() override {close();}
    bool attach(void* parent,const EditorServices& services) override {
        if(!parent || window || classAcquired || !services.readTarget || !services.beginEdit || !services.performEdit || !services.endEdit)return false;
        model=std::make_unique<EditorModel>(services);
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&module);
        if(!module || !contentClass.acquire(module,procedure,contentClassName))return failAttach();classAcquired=true;
        window=CreateWindowExW(0,contentClassName,L"JUST Tremolo",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);if(!window)return failAttach();
        font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");updateBackground();
        tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,window,nullptr,module,nullptr);if(!tooltips)return failAttach();SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,420);
        timing=child(L"BUTTON",L"Free / Sync",WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,40000);if(!timing || !SetWindowSubclass(timing,timingProcedure,1,reinterpret_cast<DWORD_PTR>(this)))return failAttach();
        if(!addControl(rateHz,"Frequency",0) || !addControl(division,"Rate",0) || !addAmount() || !addControl(stereoPhase,"Stereo Separation",2))return failAttach();
        if(!addChoice(shape) || !addControl(phase,nullptr,-1) || !addControl(stereoPhase,nullptr,-1) || !addControl(duty,nullptr,-1) || !addControl(edgeMs,nullptr,-1) || !addControl(rateHz,"Free Rate",-1))return failAttach();
        if(!addChoice(sync) || !addChoice(division) || !addChoice(timeMode) || !addControl(depth,nullptr,-1) || !addControl(mix,nullptr,-1) || !addControl(inputGain,nullptr,-1) || !addControl(outputGain,nullptr,-1))return failAttach();
        refreshValues();return true;
    }
    void resize(int w,int h) override {width=w;height=h;win::place(window,0,0,w,h);layout();}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {
        if(advanced!=state.advanced){model->cancel();advanced=state.advanced;offset=0;}poll();refreshValues();
    }
};
}
EditorContent* createEditorContent(){return new WindowsEditor;}
}
