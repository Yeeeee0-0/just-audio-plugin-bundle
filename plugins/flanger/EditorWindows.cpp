// Native Win32 rendering of the frozen Mac editor contract; Windows host QA is separate.
#include "EditorContent.hpp"
#include "UiModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <map>
#include <mutex>
#include <string>
namespace just::flanger { namespace {
std::wstring wide(const char* text){const int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring value(std::max(1,n),L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,text,-1,value.data(),n);return value;}
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
        if(--classUsers==0)UnregisterClassW(className,instance);
    }
 HWND window=nullptr,advancedWindow=nullptr,clock[2]{},timing=nullptr,warning=nullptr,tooltip=nullptr;HMODULE instance=nullptr;HFONT font=nullptr,smallFont=nullptr;HBRUSH background=nullptr;EditorServices services{};double fontScale=0;bool gray=false;
 std::array<std::unique_ptr<RotaryControl>,16> rotaries;
 std::array<HWND,16> labels{},menus{};std::unique_ptr<AnalysisView> graph;AnalysisCursor cursor;
 std::map<HWND,std::wstring> tips;
 RuntimeTelemetrySnapshot telemetry{};TelemetryAvailability telemetryAvailability=TelemetryAvailability::unavailable;
 AnalysisWindow measurement{};AnalysisAvailability measurementAvailability=AnalysisAvailability::unavailable;bool haveMeasurement=false,highFeedback=false;
 std::uint64_t resumeGeneration=0;bool advanced=false,zh=false,menusLocalized=false;int width=680,height=310,scroll=0;
 static constexpr wchar_t className[]=L"JUST.Flanger.Module.Content.v04";
 const char* local(const char* cn,const char* en)const{return zh?cn:en;}
 bool paused()const{return services.view && services.view->visualsPaused;}
 COLORREF color(int r,int g,int b)const{if(paused()){const int v=(r*30+g*59+b*11)/100;return RGB(v,v,v);}return RGB(r,g,b);}
 double target(ParamID id)const{const auto& p=parameters[registry.index(id)];return p.toPhysical(services.readTarget(services.owner,id));}
 void cancel(){for(auto& rotary:rotaries)if(rotary)rotary->refresh(false);}
 void tip(HWND h,const char* value){if(!tooltip || !h)return;const bool exists=tips.count(h);tips[h]=wide(value);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(h);info.lpszText=tips[h].data();SendMessageW(tooltip,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
 void write(ParamID id,double value){if(!std::isfinite(value) || value<0 || value>1 || value==services.readTarget(services.owner,id))return;if(services.beginEdit(services.owner,id)){services.performEdit(services.owner,id,value);services.endEdit(services.owner,id);}refreshValues();}
 std::wstring enumText(ParamID id,unsigned item,const char* english)const {
  if(!zh)return wide(english);if(id==syncID)return item?L"开启":L"关闭";if(id==modeID)return item?L"手动":L"LFO";
  if(id==shapeID)return item?L"三角":L"正弦";if(id==wetPolarityID)return item?L"反相":L"正常";
  if(id==timeModeID)return item==0?L"自由":item==1?L"跟随传输":L"播放时重置";
  auto result=wide(english);for(const auto& pair:{std::pair{L"bars",L"小节"},std::pair{L"bar",L"小节"},std::pair{L"dotted",L"附点"},std::pair{L"triplet",L"三连音"}}){std::size_t pos=0;while((pos=result.find(pair.first,pos))!=std::wstring::npos){result.replace(pos,wcslen(pair.first),pair.second);pos+=wcslen(pair.second);}}return result;
 }
 void updateFonts(){double scale=win::scale(window);if(font && scale==fontScale)return;fontScale=scale;auto next=CreateFontW(-int(std::lround(14*scale)),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto small=CreateFontW(-int(std::lround(12*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto h:clock)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto h:labels)if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto h:menus)if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(auto h:{timing,warning})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(small),TRUE);if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);font=next;smallFont=small;}
 void layout() {
  if(!graph)return;updateFonts();auto g=ui::layout(width,height,advanced);const int visibleHeight=std::max(1,height-g.controlsTop-24);
  scroll=std::clamp(scroll,0,std::max(0,g.canvasHeight-visibleHeight));graph->resize(g.margin,g.graphTop,width-2*g.margin,g.graphHeight);
  for(unsigned i=0;i<2;++i)win::place(clock[i],width-g.margin-126+63*i,0,63,28);
  win::place(timing,g.margin,4,std::max(1,width-2*g.margin-136),20);win::place(warning,g.margin,height-24,width-2*g.margin,20);
  win::place(advancedWindow,g.margin,g.controlsTop,width-2*g.margin,visibleHeight);ShowWindow(advancedWindow,advanced?SW_SHOW:SW_HIDE);if(advanced)g.gap=std::max(0,(int(win::size(advancedWindow).Width)-g.columns*g.controlWidth)/std::max(1,g.columns-1));
  const int start=(width-(4*g.controlWidth+3*g.gap))/2;
  for(unsigned i=0;i<ui::order.size();++i){const bool division=!advanced && i==10 && target(syncID)==1 && target(modeID)!=1;
   const bool simple=!advanced && (i<4 || division);HWND parent=advanced || (i>=4 && !division)?advancedWindow:window;
   const int x=simple?start+(division?0:i)*(g.controlWidth+g.gap):(i%g.columns)*(g.controlWidth+g.gap),y=simple?g.controlsTop:(i/g.columns)*160-scroll;
   if(rotaries[i]){HWND native=static_cast<HWND>(rotaries[i]->nativeHandle());if(GetParent(native)!=parent)SetParent(native,parent);rotaries[i]->resize(x,y,g.controlWidth,g.controlHeight);ShowWindow(native,!advanced && i==0 && target(syncID)==1 && target(modeID)!=1?SW_HIDE:SW_SHOW);}
   else {if(GetParent(menus[i])!=parent){SetParent(menus[i],parent);SetParent(labels[i],parent);}win::place(labels[i],x,y,g.controlWidth,22);win::place(menus[i],x,y+55,g.controlWidth,240);SetWindowTextW(labels[i],wide(division?local("速率 / 节拍","Rate / Division"):local(ui::labelsZh[i],parameters[registry.index(ui::order[i])].title)).c_str());}
  }
  SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,g.canvasHeight-1,UINT(visibleHeight),scroll,0};SetScrollInfo(advancedWindow,SB_VERT,&si,TRUE);
 }
 void refreshValues() {
  if(gray!=paused()){gray=paused();auto next=CreateSolidBrush(gray?RGB(249,249,249):RGB(245,250,251));DeleteObject(background);background=next;}
  const bool language=services.view && services.view->language==UiLanguage::chinese,languageChanged=!menusLocalized || zh!=language;zh=language;
  const bool manual=target(modeID)==1,sync=target(syncID)==1;BusLayoutSnapshot buses;
  const bool stereo=services.readBusLayout && services.readBusLayout(services.owner,buses) && (buses.validFields&layoutBuses) && buses.outputChannels==2;
  for(unsigned i=0;i<2;++i){SetWindowTextW(clock[i],i?(zh?L"同步":L"Sync"):(zh?L"自由":L"Free"));EnableWindow(clock[i],!manual);InvalidateRect(clock[i],nullptr,TRUE);tip(clock[i],local("使用保存的自由速率，或宿主速度与保存的节拍。缺少宿主时序时，同步会回退。","Use saved Free Rate or host tempo and saved Division. Sync may fall back when host timing is unavailable."));}
  for(unsigned i=0;i<ui::order.size();++i){const auto id=ui::order[i];const auto& p=parameters[registry.index(id)];const bool active=ui::enabled(id,manual,sync,stereo);
   if(rotaries[i]){rotaries[i]->refresh(active);if(id==stereoPhaseID)tip(static_cast<HWND>(rotaries[i]->nativeHandle()),stereo?local("立体声总线：左右采样相同时也生效。","Stereo bus; applies even when L/R samples are equal."):local("单声道或未知总线；保留已保存的立体声相位。","Mono or unknown bus; saved Stereo Phase is retained."));}
   else {if(languageChanged){SendMessageW(menus[i],CB_RESETCONTENT,0,0);for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(menus[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(enumText(id,n,p.enumLabels[n]).c_str()));}SendMessageW(menus[i],CB_SETCURSEL,WPARAM(std::lround(target(id))),0);EnableWindow(menus[i],active);}
  }menusLocalized=true;
  const auto* view=services.view;if(view && resumeGeneration!=view->visualResumeGeneration){cursor={};telemetry={};telemetryAvailability=TelemetryAvailability::unavailable;measurement={};measurementAvailability=AnalysisAvailability::unavailable;haveMeasurement=false;resumeGeneration=view->visualResumeGeneration;}
  if(!paused()){
   telemetryAvailability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,telemetry):TelemetryAvailability::unavailable;
   AnalysisBatch batch;bool got=false;auto available=AnalysisAvailability::unavailable;
   for(unsigned i=0;i<16 && services.readAnalysis;++i){available=services.readAnalysis(services.owner,cursor,batch);if(available!=AnalysisAvailability::fresh || !batch.count)break;measurement=batch.windows[batch.count-1];got=true;}
   if(got){highFeedback=std::abs(target(feedbackID))>=80;haveMeasurement=true;measurementAvailability=AnalysisAvailability::fresh;}else if(available!=AnalysisAvailability::fresh){measurementAvailability=available;haveMeasurement=false;}
  }
  const auto actual=ui::timingReadout(telemetryAvailability,telemetry);char text[768];
  if(manual)std::snprintf(text,sizeof(text),"%s",local("手动 · LFO 停用","Manual · LFO inactive"));
  else if(sync && actual.rateAvailable){if(actual.syncFallback)std::snprintf(text,sizeof(text),local("%.2f Hz · 回退 · 宿主时序不可用","%.2f Hz · Fallback · host timing unavailable"),actual.rateHz);
   else if(actual.syncKnown && actual.tempoAvailable)std::snprintf(text,sizeof(text),local("%.2f Hz · 宿主 %.1f BPM","%.2f Hz · Host %.1f BPM"),actual.rateHz,actual.bpm);else std::snprintf(text,sizeof(text),local("%.2f Hz · 同步来源不可用","%.2f Hz · Sync source unavailable"),actual.rateHz);}
  else std::snprintf(text,sizeof(text),"%s",sync?(telemetryAvailability==TelemetryAvailability::stale?local("同步 · 实测速率已过期","Sync · measured rate stale"):local("同步 · 实测速率不可用","Sync · measured rate unavailable")):local("自由 · 保存的速率","Free · saved Rate"));
  SetWindowTextW(timing,wide(text).c_str());tip(timing,text);
  if(haveMeasurement && (measurement.effectFields&analysisDelay)){
   const auto& m=measurement;std::snprintf(text,sizeof(text),local("实际延时 L/R %.2f / %.2f ms","Actual delay L/R %.2f / %.2f ms"),m.delayMs[0],m.delayMs[1]);std::string status=text;
   if(highFeedback)status+=local(" · 高反馈"," · high feedback");if(std::max(m.channels[2].peak,m.channels[3].peak)>1)status+=local(" · 输出 > 0 dBFS"," · Output > 0 dBFS");
   if(m.header.flags&analysisBypassed)status+=local(" · 旁通"," · Bypass");else if((m.header.flags&analysisTransportKnown) && !(m.header.flags&analysisPlaying))status+=local(" · 宿主停止"," · Host stopped");
   if(std::max(m.channels[0].peak,m.channels[1].peak)==0)status+=std::max(m.channels[2].peak,m.channels[3].peak)>0?local(" · 尾音/输出持续"," · Tail/output active"):local(" · 静音"," · Silence");
   if(m.header.flags&analysisInvalid)status+=local(" · 输入无效"," · Invalid input");if(m.effectFlags&1)status+=local(" · 同步回退"," · Sync fallback");std::snprintf(text,sizeof(text),"%s",status.c_str());
  }else std::snprintf(text,sizeof(text),"%s",measurementAvailability==AnalysisAvailability::stale?local("实测延时已过期","Measured delay stale"):local("实际延时不可用","Actual delay unavailable"));
  SetWindowTextW(warning,wide(text).c_str());tip(warning,text);graph->refresh();layout();InvalidateRect(window,nullptr,FALSE);
 }
 void drawButton(const DRAWITEMSTRUCT& d){const bool selected=(d.hwndItem==clock[1])==(target(syncID)==1);auto brush=CreateSolidBrush(selected?color(205,239,243):color(246,250,251));auto pen=CreatePen(PS_SOLID,1,color(201,222,228));auto oldB=SelectObject(d.hDC,brush),oldP=SelectObject(d.hDC,pen);RoundRect(d.hDC,d.rcItem.left,d.rcItem.top,d.rcItem.right,d.rcItem.bottom,9,9);SelectObject(d.hDC,font);SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,(d.itemState&ODS_DISABLED)?color(151,169,175):color(23,51,60));wchar_t label[32]{};GetWindowTextW(d.hwndItem,label,32);RECT r=d.rcItem;DrawTextW(d.hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d.itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}SelectObject(d.hDC,oldB);SelectObject(d.hDC,oldP);DeleteObject(brush);DeleteObject(pen);}
 static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,msg,wp,lp);
  if(msg==WM_COMMAND){HWND sender=reinterpret_cast<HWND>(lp);if(HIWORD(wp)==BN_CLICKED && (sender==self->clock[0] || sender==self->clock[1])){self->write(syncID,sender==self->clock[1]?1:0);return 0;}
   if(HIWORD(wp)==CBN_SELCHANGE)for(unsigned i=0;i<ui::order.size();++i)if(sender==self->menus[i]){const auto selected=SendMessageW(sender,CB_GETCURSEL,0,0);const auto& p=parameters[registry.index(ui::order[i])];if(selected>=0 && selected<=p.stepCount)self->write(p.id,double(selected)/p.stepCount);return 0;}}
  if(msg==WM_DRAWITEM){self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
  if(msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,self->color(23,51,60));SetBkColor(dc,self->color(245,250,251));return reinterpret_cast<LRESULT>(self->background);}
  if(msg==WM_PAINT || msg==WM_PRINTCLIENT){win::Paint paint(h,true,msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):nullptr);return 0;}
  if(msg==WM_ERASEBKGND){RECT r;GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,self->background);return 1;}
  if(msg==WM_VSCROLL || (msg==WM_MOUSEWHEEL && self->advanced)){const auto g=ui::layout(self->width,self->height,true);const int maximum=std::max(0,g.canvasHeight-(self->height-g.controlsTop-24));SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(self->advancedWindow,SB_VERT,&si);
   if(msg==WM_MOUSEWHEEL)self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*40;else switch(LOWORD(wp)){case SB_LINEUP:self->scroll-=20;break;case SB_LINEDOWN:self->scroll+=20;break;case SB_PAGEUP:self->scroll-=120;break;case SB_PAGEDOWN:self->scroll+=120;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:self->scroll=si.nTrackPos;break;}
   self->scroll=std::clamp(self->scroll,0,maximum);self->layout();return 0;}
  return DefWindowProcW(h,msg,wp,lp);
 }
public:
 ~WindowsContent()override{cancel();for(auto& rotary:rotaries)rotary.reset();graph.reset();if(tooltip)DestroyWindow(tooltip);if(window)DestroyWindow(window);releaseClass();if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);if(background)DeleteObject(background);}
 bool attach(void* parent,const EditorServices& s)override {
  if(!parent || window || classAcquired || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&instance);if(!instance)return false;
  font=CreateFontW(-14,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");smallFont=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");background=CreateSolidBrush(RGB(245,250,251));
  WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=proc;cls.lpszClassName=className;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);if(!acquireClass(cls))return false;
  window=CreateWindowExW(0,className,L"Flanger",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,instance,this);if(!window)return false;
  advancedWindow=CreateWindowExW(0,className,L"",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL,0,0,width,height,window,nullptr,instance,this);if(!advancedWindow)return false;
  tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,instance,nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,440);
  graph=AnalysisView::create(window,s,AnalysisViewMode::spectrum);if(!graph)return false;
  for(unsigned i=0;i<2;++i){clock[i]=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,63,28,window,nullptr,instance,nullptr);SendMessageW(clock[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
  timing=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_RIGHT|SS_ENDELLIPSIS,0,0,1,1,window,nullptr,instance,nullptr);warning=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,0,0,1,1,window,nullptr,instance,nullptr);for(auto h:{timing,warning})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
  for(unsigned i=0;i<ui::order.size();++i){const auto& p=parameters[registry.index(ui::order[i])];HWND targetParent=i<4?window:advancedWindow;
   if(!p.enumLabels){DisplayPolicy policy;policy.labelZh=ui::labelsZh[i];rotaries[i]=RotaryControl::create(targetParent,s,p,policy);if(!rotaries[i])return false;}
   else {labels[i]=CreateWindowExW(0,L"STATIC",wide(p.title).c_str(),WS_CHILD|WS_VISIBLE|SS_CENTER,0,0,1,1,targetParent,nullptr,instance,nullptr);menus[i]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,1,1,targetParent,nullptr,instance,nullptr);for(auto h:{labels[i],menus[i]})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
  }refreshValues();return true;
 }
 void resize(int w,int h)override{width=w;height=h;win::place(window,0,0,w,h);layout();}
 void refresh(const EditorViewState& view,const StatusSnapshot&)override{if(advanced!=view.advanced){cancel();advanced=view.advanced;scroll=0;}refreshValues();}
};
}
EditorContent* createEditorContent(){return new WindowsContent;}
}
