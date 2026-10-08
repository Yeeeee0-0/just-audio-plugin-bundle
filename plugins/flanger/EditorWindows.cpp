#include "EditorContent.hpp"
#include "UiModel.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
#include <windows.h>
#include <commctrl.h>
#include <string>
namespace just::flanger {
namespace {
std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring value(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,value.data(),n);return value;}
class WindowsContent final:public EditorContent {
 HWND window=nullptr,advancedWindow=nullptr,clock=nullptr,timing=nullptr,warning=nullptr;HMODULE instance=nullptr;EditorServices services{};
 std::array<std::unique_ptr<RotaryControl>,16> rotaries;
 std::array<HWND,16> labels{},menus{};std::unique_ptr<AnalysisView> graph;AnalysisCursor cursor;
 bool advanced=false;int width=680,height=310,scroll=0;
 static constexpr wchar_t className[]=L"JUST.Flanger.Module.Content.v03";
 double target(ParamID id)const{const auto& p=parameters[registry.index(id)];return p.toPhysical(services.readTarget(services.owner,id));}
 void cancel(){for(auto& rotary:rotaries)if(rotary)rotary->refresh(false);}
 void write(ParamID id,double value){if(value==services.readTarget(services.owner,id))return;if(services.beginEdit(services.owner,id)){services.performEdit(services.owner,id,value);services.endEdit(services.owner,id);}refreshValues();}
 void layout() {
  const auto g=ui::layout(width,height,advanced);const int visibleHeight=std::max(1,height-g.controlsTop-24);
  graph->resize(g.margin,g.graphTop,width-2*g.margin,g.graphHeight);
  MoveWindow(clock,width-g.margin-126,0,126,220,TRUE);MoveWindow(timing,g.margin,4,width-2*g.margin-136,20,TRUE);MoveWindow(warning,g.margin,height-24,width-2*g.margin,20,TRUE);
  MoveWindow(advancedWindow,g.margin,g.controlsTop,width-2*g.margin,visibleHeight,TRUE);ShowWindow(advancedWindow,advanced?SW_SHOW:SW_HIDE);
  const int start=(width-(4*g.controlWidth+3*g.gap))/2;
  for(unsigned i=0;i<ui::order.size();++i){const bool division=!advanced && i==10 && target(syncID)==1 && target(modeID)!=1;
   const bool simple=!advanced && (i<4 || division);HWND parent=advanced || (i>=4 && !division)?advancedWindow:window;
   const int x=simple?start+(division?0:i)*(g.controlWidth+g.gap):(i%g.columns)*(g.controlWidth+g.gap);
   const int y=simple?g.controlsTop:(i/g.columns)*160-scroll;
   if(rotaries[i]){HWND native=static_cast<HWND>(rotaries[i]->nativeHandle());if(GetParent(native)!=parent)SetParent(native,parent);rotaries[i]->resize(x,y,g.controlWidth,g.controlHeight);ShowWindow(native,!advanced && i==0 && target(syncID)==1 && target(modeID)!=1?SW_HIDE:SW_SHOW);}
   else {if(GetParent(menus[i])!=parent){SetParent(menus[i],parent);SetParent(labels[i],parent);}MoveWindow(labels[i],x,y,g.controlWidth,22,TRUE);MoveWindow(menus[i],x,y+55,g.controlWidth,240,TRUE);SetWindowTextW(labels[i],division?L"Rate / Division":wide(parameters[registry.index(ui::order[i])].title).c_str());}
  }
  SetScrollRange(advancedWindow,SB_VERT,0,advanced?std::max(0,g.canvasHeight-visibleHeight):0,FALSE);SetScrollPos(advancedWindow,SB_VERT,scroll,TRUE);
 }
 void refreshValues() {
  const bool manual=target(modeID)==1,sync=target(syncID)==1;BusLayoutSnapshot buses;
  const bool stereo=services.readBusLayout && services.readBusLayout(services.owner,buses) && (buses.validFields&layoutBuses) && buses.outputChannels==2;
  SendMessageW(clock,CB_SETCURSEL,sync?1:0,0);EnableWindow(clock,!manual);
  for(unsigned i=0;i<ui::order.size();++i){const auto id=ui::order[i];const auto& p=parameters[registry.index(id)];const bool active=ui::enabled(id,manual,sync,stereo);
   if(rotaries[i])rotaries[i]->refresh(active);else {SendMessageW(menus[i],CB_SETCURSEL,WPARAM(std::lround(target(id))),0);EnableWindow(menus[i],active);}
  }
  RuntimeTelemetrySnapshot runtime;const auto availability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,runtime):TelemetryAvailability::unavailable;
  const auto actual=ui::timingReadout(availability,runtime);char text[256];
  if(manual)std::snprintf(text,sizeof(text),"Manual · LFO inactive");
  else if(sync && actual.rateAvailable){if(actual.syncFallback)std::snprintf(text,sizeof(text),"%.2f Hz · Fallback: host timing unavailable",actual.rateHz);
   else if(actual.syncKnown && actual.tempoAvailable)std::snprintf(text,sizeof(text),"%.2f Hz · Host %.1f BPM",actual.rateHz,actual.bpm);else std::snprintf(text,sizeof(text),"%.2f Hz · Sync source unavailable",actual.rateHz);}
  else std::snprintf(text,sizeof(text),sync?(availability==TelemetryAvailability::stale?"Sync · measured rate stale":"Sync · measured rate unavailable"):"Free · saved Rate");
  SetWindowTextW(timing,wide(text).c_str());
  AnalysisBatch batch;AnalysisWindow measured{};bool got=false;auto available=AnalysisAvailability::unavailable;
  for(unsigned i=0;i<16 && services.readAnalysis;++i){available=services.readAnalysis(services.owner,cursor,batch);if(available!=AnalysisAvailability::fresh || !batch.count)break;measured=batch.windows[batch.count-1];got=true;}
  if(got && (measured.effectFields&analysisDelay))std::snprintf(text,sizeof(text),"Actual delay L/R %.2f / %.2f ms%s%s%s",measured.delayMs[0],measured.delayMs[1],std::abs(target(feedbackID))>=80?" · high feedback":"",measured.effectFlags&1?" · Sync fallback":"",measured.header.flags&analysisBypassed?" · Bypass":"");
  else if(available!=AnalysisAvailability::fresh)std::snprintf(text,sizeof(text),available==AnalysisAvailability::stale?"Measured delay stale":"Actual delay unavailable");else text[0]=0;
  SetWindowTextW(warning,wide(text).c_str());graph->refresh();layout();
 }
 static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(msg==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,msg,wp,lp);
  if(msg==WM_COMMAND && HIWORD(wp)==CBN_SELCHANGE){HWND sender=reinterpret_cast<HWND>(lp);if(sender==self->clock){self->write(syncID,double(SendMessageW(sender,CB_GETCURSEL,0,0)));return 0;}
   for(unsigned i=0;i<ui::order.size();++i)if(sender==self->menus[i]){const auto& p=parameters[registry.index(ui::order[i])];self->write(p.id,double(SendMessageW(sender,CB_GETCURSEL,0,0))/p.stepCount);return 0;}}
  if(msg==WM_VSCROLL){const auto g=ui::layout(self->width,self->height,true);const int maximum=std::max(0,g.canvasHeight-(self->height-g.controlsTop-24));int action=LOWORD(wp);
   if(action==SB_LINEUP)self->scroll-=20;else if(action==SB_LINEDOWN)self->scroll+=20;else if(action==SB_THUMBPOSITION || action==SB_THUMBTRACK)self->scroll=HIWORD(wp);self->scroll=std::clamp(self->scroll,0,maximum);self->layout();return 0;}
  return DefWindowProcW(h,msg,wp,lp);
 }
public:
 ~WindowsContent()override{cancel();for(auto& rotary:rotaries)rotary.reset();graph.reset();if(window)DestroyWindow(window);}
 bool attach(void* parent,const EditorServices& s)override {
  if(!parent || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&instance);
  WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=proc;cls.lpszClassName=className;cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
  window=CreateWindowExW(0,className,L"Flanger",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,static_cast<HWND>(parent),nullptr,instance,this);if(!window)return false;
  advancedWindow=CreateWindowExW(0,className,L"",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL,0,0,width,height,window,nullptr,instance,this);if(!advancedWindow)return false;
  graph=AnalysisView::create(window,s,AnalysisViewMode::spectrum);
  clock=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,0,0,126,220,window,nullptr,instance,nullptr);for(auto* mode:{L"Free",L"Sync"})SendMessageW(clock,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));
  timing=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_RIGHT,0,0,1,1,window,nullptr,instance,nullptr);warning=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,instance,nullptr);
  for(unsigned i=0;i<ui::order.size();++i){const auto& p=parameters[registry.index(ui::order[i])];HWND targetParent=i<4?window:advancedWindow;
   if(!p.enumLabels){DisplayPolicy policy;policy.labelZh=ui::labelsZh[i];rotaries[i]=RotaryControl::create(targetParent,s,p,policy);}
   else {labels[i]=CreateWindowExW(0,L"STATIC",wide(p.title).c_str(),WS_CHILD|WS_VISIBLE|SS_CENTER,0,0,1,1,targetParent,nullptr,instance,nullptr);
    menus[i]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,1,1,targetParent,nullptr,instance,nullptr);for(unsigned n=0;n<=p.stepCount;++n)SendMessageW(menus[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.enumLabels[n]).c_str()));}
  }
  refreshValues();return true;
 }
 void resize(int w,int h)override{width=w;height=h;MoveWindow(window,0,0,w,h,TRUE);layout();}
 void refresh(const EditorViewState& view,const StatusSnapshot&)override{if(advanced!=view.advanced){cancel();advanced=view.advanced;scroll=0;}refreshValues();}
};
}
EditorContent* createEditorContent(){return new WindowsContent;}
}
