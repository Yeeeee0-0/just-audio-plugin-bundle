#include <windows.h>
#include "Editor.hpp"
#include "Parameters.hpp"
#include "AlgorithmPolicy.hpp"
#include "HistoryModel.hpp"
#include "PreviewLayout.hpp"
#include "common/ui/Controls.hpp"
#include <cstdio>
#include <cwchar>
namespace just::limiter {
class WindowsEditor final:public EditorContent {
 HWND window=nullptr,details=nullptr,modeButton=nullptr,algorithmPicker=nullptr,algorithmLabel=nullptr,algorithmInfo=nullptr,lookaheadInfo=nullptr;
 std::array<std::unique_ptr<RotaryControl>,10> controls;
 EditorServices services{};HistoryModel history;PreviewLayout layout;
 bool advanced=false,bypassed=false;int width=1120,height=460,scroll=0;
 static RECT rectangle(PreviewRect r){return {LONG(r.x),LONG(r.y),LONG(r.x+r.width),LONG(r.y+r.height)};}
 static void label(HDC dc,const char* value,RECT area,COLORREF color){SetTextColor(dc,color);DrawTextA(dc,value,-1,&area,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);}
 void draw(HDC dc){
  SetBkMode(dc,TRANSPARENT);const auto card=rectangle(layout.card),plot=rectangle(layout.plot);auto font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);
  auto brush=CreateSolidBrush(RGB(245,250,251));auto pen=CreatePen(PS_SOLID,1,RGB(211,227,233));auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,card.left,card.top,card.right,card.bottom,22,22);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
  const double ceilingDb=parameters[ceiling].toPhysical(services.readTarget(services.owner,ceiling));char value[128];
  const bool modern=algorithmRegistered()&&transparentVersion(services.readTarget(services.owner,algorithmVersion));const bool tp=services.readTarget(services.owner,mode)>=.5;
  label(dc,modern?(tp?"TRANSPARENT - RECONSTRUCTED TP":"TRANSPARENT - SAMPLE PEAK"):(tp?"LEGACY - MIX":"LEGACY - LIVE"),{card.left+20,card.top+14,card.right-165,card.top+40},RGB(124,157,167));
  std::snprintf(value,sizeof(value),"CEILING %.1f dB",ceilingDb);label(dc,value,{card.right-155,card.top+14,card.right-10,card.top+40},RGB(5,151,168));
  BusLayoutSnapshot bus;if(services.readBusLayout&&services.readBusLayout(services.owner,bus)&&(bus.validFields&layoutSampleRate)&&bus.sampleRate>0){const double requested=parameters[lookahead].toPhysical(services.readTarget(services.owner,lookahead));const double actual=effectiveLookaheadMs(requested,bus.sampleRate,modern,tp);if(modern&&tp)std::snprintf(value,sizeof(value),"Lookahead effective %.2f ms | requested %.2f | TP max %.2f",actual,requested,maximumTpLookaheadMs(bus.sampleRate));else std::snprintf(value,sizeof(value),"Lookahead effective %.2f ms | requested %.2f",actual,requested);label(dc,value,{card.left+20,card.top+40,card.right-20,card.top+58},RGB(124,157,167));}
  auto y=[&](double db){return LONG(plot.bottom-(std::clamp(db,-60.,12.)+60)/72*(plot.bottom-plot.top));};
  pen=CreatePen(PS_SOLID,1,RGB(218,233,237));oldPen=SelectObject(dc,pen);
  for(int i=0;i<=6;++i){const int x=plot.left+(plot.right-plot.left)*i/6;MoveToEx(dc,x,plot.top,nullptr);LineTo(dc,x,plot.bottom);}
  for(double db:{-60.,-48.,-36.,-24.,-12.,0.,12.}){MoveToEx(dc,plot.left,y(db),nullptr);LineTo(dc,plot.right,y(db));}SelectObject(dc,oldPen);DeleteObject(pen);
  pen=CreatePen(PS_DASH,1,RGB(11,172,186));oldPen=SelectObject(dc,pen);MoveToEx(dc,plot.left,y(ceilingDb),nullptr);LineTo(dc,plot.right,y(ceilingDb));SelectObject(dc,oldPen);DeleteObject(pen);
  if(history.current()){
   const auto& latest=history.latest();const double span=latest.header.sampleRate*6,end=double(latest.header.endSample);
   const int saved=SaveDC(dc);IntersectClipRect(dc,plot.left,plot.top,plot.right,plot.bottom);
   for(unsigned tap=0;tap<2;++tap){pen=CreatePen(PS_SOLID,tap?2:1,tap?RGB(15,181,196):RGB(158,188,202));oldPen=SelectObject(dc,pen);bool started=false;std::uint64_t next=0;
    for(std::size_t i=0;i<history.count;++i){const auto& item=history.at(i);const auto& h=item.header;if(h.flags&analysisInvalid||!(h.flags&analysisInputAligned)){started=false;continue;}
     const int x=int(plot.right-(end-h.endSample)/span*(plot.right-plot.left));if(x<plot.left){started=false;continue;}
     const int yy=y(20*std::log10(std::max(1e-8,std::max(item.channels[2*tap].peak,item.channels[2*tap+1].peak))));
     if(!started||next!=h.startSample||h.flags&analysisGap)MoveToEx(dc,x,yy,nullptr);else LineTo(dc,x,yy);started=true;next=h.endSample;
    }SelectObject(dc,oldPen);DeleteObject(pen);
   }RestoreDC(dc,saved);auto r=rectangle(layout.readout);
   if(latest.effectFields&analysisReduction)std::snprintf(value,sizeof(value),"GR %.1f dB",latest.reductionDb);else std::snprintf(value,sizeof(value),"GR --");label(dc,value,{r.left,r.top,r.left+(r.right-r.left)*4/10,r.top+24},RGB(5,151,168));
   const auto db=[](double x){return 20*std::log10(std::max(1e-8,x));};std::snprintf(value,sizeof(value),"IN %.1f dBFS   OUT %.1f dBFS",db(std::max(latest.channels[0].peak,latest.channels[1].peak)),db(std::max(latest.channels[2].peak,latest.channels[3].peak)));label(dc,value,{r.left+(r.right-r.left)*4/10,r.top,r.right,r.top+24},RGB(124,157,167));
  }else label(dc,history.availability==AnalysisAvailability::stale?"No new audio":"Waiting for audio",plot,RGB(124,157,167));
  label(dc,bypassed?"Bypass - delayed raw input":"Input / output - 6 s | Output Gain follows the limit",rectangle(layout.status),RGB(124,157,167));SelectObject(dc,oldFont);DeleteObject(font);
 }
 static LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM a,LPARAM b){
  auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(w,message,a,b);
  if(message==WM_PAINT&&w==self->window){PAINTSTRUCT p;auto dc=BeginPaint(w,&p);self->draw(dc);EndPaint(w,&p);return 0;}
  if(message==WM_COMMAND&&LOWORD(a)==100&&HIWORD(a)==BN_CLICKED){const auto id=mode;if(self->services.beginEdit(self->services.owner,id)){self->services.performEdit(self->services.owner,id,SendMessageW(self->modeButton,BM_GETCHECK,0,0)==BST_CHECKED?0:1);self->services.endEdit(self->services.owner,id);}return 0;}
  if(message==WM_COMMAND&&LOWORD(a)==101&&HIWORD(a)==CBN_SELCHANGE&&algorithmRegistered()){if(self->services.beginEdit(self->services.owner,algorithmVersion)){self->services.performEdit(self->services.owner,algorithmVersion,SendMessageW(self->algorithmPicker,CB_GETCURSEL,0,0)==1?1:0);self->services.endEdit(self->services.owner,algorithmVersion);}return 0;}
  if(message==WM_MOUSEWHEEL&&w==self->details){self->scroll-=GET_WHEEL_DELTA_WPARAM(a)/3;self->resize(self->width,self->height);return 0;}
  return DefWindowProcW(w,message,a,b);
 }
public:
 ~WindowsEditor() override {for(auto& c:controls)c.reset();if(window)DestroyWindow(window);}
 bool attach(void* parent,const EditorServices& s) override {
  if(!parent||!s.readTarget||!s.beginEdit||!s.performEdit||!s.endEdit)return false;services=s;HINSTANCE instance=nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&instance);
  WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=instance;c.lpszClassName=L"JUSTLimiterPreview03";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&c);
  window=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,reinterpret_cast<HWND>(parent),nullptr,instance,this);if(!window)return false;
  details=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,window,nullptr,instance,this);
  for(unsigned id:{1u,9u,3u,5u,4u,2u,6u,8u}){auto spec=parameters[id];if(id==input)spec.title="Input Gain";if(id==lookahead)spec.title="Requested Lookahead";DisplayPolicy policy;policy.decimals=1;policy.labelZh=id==lookahead?"前瞻请求":controlLabelsZh[id];if(id==1||id==9)policy.style=just::ControlStyle::vertical;controls[id]=RotaryControl::create((id==1||id==9)?window:details,s,spec,policy);}
  algorithmLabel=CreateWindowExW(0,L"STATIC",L"Algorithm",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  algorithmPicker=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP,0,0,1,1,details,reinterpret_cast<HMENU>(101),instance,nullptr);
  SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Legacy compatibility"));SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Transparent insurance"));
  algorithmInfo=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  lookaheadInfo=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  modeButton=CreateWindowExW(0,L"BUTTON",L"Live Sample Peak",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP,0,0,1,1,details,reinterpret_cast<HMENU>(100),instance,nullptr);return true;
 }
 void resize(int w,int h) override {
  width=w;height=h;MoveWindow(window,0,0,w,h,TRUE);layout=PreviewLayout::make(w,h,advanced);
  for(unsigned id:{1u,9u})if(controls[id]){auto r=id==1?layout.input:layout.output;controls[id]->resize(int(r.x),int(r.y),int(r.width),int(r.height));}
  auto r=layout.details;MoveWindow(details,int(r.x),int(r.y),int(r.width),int(r.height),TRUE);
  const unsigned columns=r.width<950?4:6;const int cell=int(r.width/columns);scroll=std::clamp(scroll,0,std::max(0,96+int((6+columns-1)/columns)*146+8-int(r.height)));const unsigned ids[]={3,5,4,2,6,8};for(unsigned i=0;i<6;++i)if(controls[ids[i]])controls[ids[i]]->resize(i%columns*cell,96+int(i/columns)*146-scroll,cell,140);
  MoveWindow(algorithmLabel,8,6-scroll,48,22,TRUE);MoveWindow(algorithmPicker,58,2-scroll,std::min(250,int(r.width*.34)),200,TRUE);MoveWindow(modeButton,326,2-scroll,std::max(120,int(r.width)-334),28,TRUE);MoveWindow(algorithmInfo,8,34-scroll,int(r.width)-16,28,TRUE);MoveWindow(lookaheadInfo,8,64-scroll,int(r.width)-16,24,TRUE);InvalidateRect(window,nullptr,TRUE);
 }
 void refresh(const EditorViewState& view,const StatusSnapshot& status) override {
  advanced=view.advanced;bypassed=status.bypassProtectionExit;ShowWindow(details,advanced?SW_SHOW:SW_HIDE);for(unsigned id=1;id<controls.size();++id)if(controls[id])controls[id]->refresh(id==1||id==9||advanced);
  const bool modern=algorithmRegistered()&&transparentVersion(services.readTarget(services.owner,algorithmVersion));const bool tp=services.readTarget(services.owner,mode)>=.5;SendMessageW(algorithmPicker,CB_SETCURSEL,modern?1:0,0);EnableWindow(algorithmPicker,algorithmRegistered());
  SetWindowTextW(modeButton,modern?L"Sample peak (off = reconstructed TP)":L"Legacy Live (off = legacy Mix)");SetWindowTextW(algorithmInfo,modern?L"Transparent insurance: neutral untriggered audio stays intact. TP uses a 0.2 dB reserve after an excursion.":L"Legacy sound is preserved. Select Transparent insurance to upgrade while retaining other sound parameters.");
  BusLayoutSnapshot bus;wchar_t timing[180];if(services.readBusLayout&&services.readBusLayout(services.owner,bus)&&(bus.validFields&layoutSampleRate)&&bus.sampleRate>0){const double request=parameters[lookahead].toPhysical(services.readTarget(services.owner,lookahead));const double actual=effectiveLookaheadMs(request,bus.sampleRate,modern,tp);if(modern&&tp)std::swprintf(timing,180,L"Lookahead requested %.2f ms | effective %.2f ms | TP max %.2f ms (fixed latency)",request,actual,maximumTpLookaheadMs(bus.sampleRate));else std::swprintf(timing,180,L"Lookahead requested %.2f ms | effective %.2f ms",request,actual);SetWindowTextW(lookaheadInfo,timing);}else SetWindowTextW(lookaheadInfo,L"Effective lookahead: awaiting the host sample rate");
  SendMessageW(modeButton,BM_SETCHECK,services.readTarget(services.owner,mode)<.5?BST_CHECKED:BST_UNCHECKED,0);history.refresh(services);resize(width,height);
 }
};
EditorContent* createEditor(){return new WindowsEditor;}
}
