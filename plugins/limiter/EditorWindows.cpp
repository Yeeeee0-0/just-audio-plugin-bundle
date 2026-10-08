#include "Editor.hpp"
#include "Parameters.hpp"
#include "AlgorithmPolicy.hpp"
#include "HistoryModel.hpp"
#include "PreviewLayout.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
namespace just::limiter {
namespace {unsigned editorInstances=0;constexpr wchar_t className[]=L"JUST.Limiter.Content.010";}
class WindowsEditor final:public EditorContent {
 HWND window=nullptr,details=nullptr,outputHold=nullptr,reductionHold=nullptr,tooltip=nullptr,modeButton=nullptr,algorithmPicker=nullptr,algorithmLabel=nullptr,algorithmInfo=nullptr,lookaheadInfo=nullptr;
 std::array<std::unique_ptr<RotaryControl>,10> controls;
 EditorServices services{};HistoryModel history;PreviewLayout layout;BusLayoutSnapshot busLayout;std::uint64_t visualResumeGeneration=0;HFONT font=nullptr;HBRUSH background=nullptr;std::wstring tipText;HINSTANCE instance=nullptr;bool classAcquired=false;double fontScale=0;
 bool advanced=false,bypassed=false,chinese=false,lastPaused=false;int width=1120,height=460,scroll=0;
 static RECT rectangle(PreviewRect r){return {LONG(r.x),LONG(r.y),LONG(r.x+r.width),LONG(r.y+r.height)};}
 const char* tr(const char* zh,const char* en)const{return chinese?zh:en;}
 static void label(HDC dc,const char* value,RECT area,COLORREF color,int size=11){auto font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");auto old=SelectObject(dc,font);SetTextColor(dc,color);auto text=win::wide(value);DrawTextW(dc,text.c_str(),int(text.size()),&area,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);DeleteObject(font);}
 void addTooltip(HWND target){if(!tooltip || !target)return;TOOLINFOW t{};t.cbSize=sizeof(t);t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.hwnd=window;t.uId=reinterpret_cast<UINT_PTR>(target);t.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));}
 const char* help(HWND target)const{if(target==outputHold)return tr("清除实测输出峰值保持","Clear the measured output peak hold");if(target==reductionHold)return tr("清除实测增益衰减保持","Clear the measured gain reduction hold");if(target==algorithmPicker)return tr("切换算法版本；保留输入、输出、限幅上限及其它声音参数。","Change the algorithm version while preserving every other sound parameter.");
  const bool modern=algorithmRegistered()&&transparentVersion(services.readTarget(services.owner,algorithmVersion));return modern?tr("主音频只经过延迟与公共增益。TP使用真实重建峰值检测，触发时保留0.2 dB验证余量；不是模拟峰值的绝对保证。","Audio uses delay and linked gain. TP reconstructs peaks and reserves 0.2 dB after an excursion; it is not an absolute analog-peak guarantee."):tr("保留旧版声音：旧Mix包含滤波和保守峰值保护。","Preserves the old sound: legacy Mix includes its filter and conservative guard.");
 }
 void updateHolds(){char value[100];if(history.outputHeld){if(history.outputHold.outputPeak>0)std::snprintf(value,sizeof(value),tr("峰值 %.1f dBFS","Peak %.1f dBFS"),20*std::log10(history.outputHold.outputPeak));else std::snprintf(value,sizeof(value),"%s",tr("峰值 −∞ dBFS","Peak −∞ dBFS"));}else std::snprintf(value,sizeof(value),"%s",tr("峰值 —","Peak —"));SetWindowTextW(outputHold,win::wide(value).c_str());
  if(history.grHold.reductionValid)std::snprintf(value,sizeof(value),tr("最大衰减 %.1f dB","Max GR %.1f dB"),history.grHold.reductionDb);else std::snprintf(value,sizeof(value),"%s",tr("最大衰减 —","Max GR —"));SetWindowTextW(reductionHold,win::wide(value).c_str());EnableWindow(outputHold,history.current());EnableWindow(reductionHold,history.current());
 }
 void draw(HDC dc){
  SetBkMode(dc,TRANSPARENT);const auto card=rectangle(layout.card),plot=rectangle(layout.plot);auto font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);
  auto brush=CreateSolidBrush(RGB(245,250,251));auto pen=CreatePen(PS_SOLID,1,RGB(211,227,233));auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,card.left,card.top,card.right,card.bottom,22,22);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
  const double ceilingDb=parameters[ceiling].toPhysical(services.readTarget(services.owner,ceiling));char value[128];
  const bool modern=algorithmRegistered()&&transparentVersion(services.readTarget(services.owner,algorithmVersion));const bool tp=services.readTarget(services.owner,mode)>=.5;
  label(dc,modern?(tp?tr("透明保险 · TP重建保护","TRANSPARENT · RECONSTRUCTED TP"):tr("透明保险 · 采样峰值","TRANSPARENT · SAMPLE PEAK")):(tp?tr("旧版兼容 · Mix","LEGACY · MIX"):tr("旧版兼容 · Live","LEGACY · LIVE")),{card.left+20,card.top+14,card.right-165,card.top+40},RGB(124,157,167));
  std::snprintf(value,sizeof(value),tr("限幅上限 %.1f dB","CEILING %.1f dB"),ceilingDb);label(dc,value,{card.right-155,card.top+14,card.right-10,card.top+40},RGB(5,151,168),13);
  const auto& bus=busLayout;if((bus.validFields&layoutSampleRate)&&bus.sampleRate>0){const double requested=parameters[lookahead].toPhysical(services.readTarget(services.owner,lookahead));const double actual=effectiveLookaheadMs(requested,bus.sampleRate,modern,tp);if(modern&&tp)std::snprintf(value,sizeof(value),tr("实际前瞻 %.2f ms · 请求 %.2f · TP最多 %.2f","Effective lookahead %.2f ms · requested %.2f · TP max %.2f"),actual,requested,maximumTpLookaheadMs(bus.sampleRate));else std::snprintf(value,sizeof(value),tr("实际前瞻 %.2f ms · 请求 %.2f","Effective lookahead %.2f ms · requested %.2f"),actual,requested);label(dc,value,{card.left+20,card.top+40,card.right-20,card.top+58},RGB(124,157,167));}
  auto y=[&](double db){return LONG(plot.bottom-(std::clamp(db,-60.,12.)+60)/72*(plot.bottom-plot.top));};
  pen=CreatePen(PS_SOLID,1,RGB(218,233,237));oldPen=SelectObject(dc,pen);
  for(int i=0;i<=6;++i){const int x=plot.left+(plot.right-plot.left)*i/6;MoveToEx(dc,x,plot.top,nullptr);LineTo(dc,x,plot.bottom);}
  for(double db:{-60.,-48.,-36.,-24.,-12.,0.,12.}){MoveToEx(dc,plot.left,y(db),nullptr);LineTo(dc,plot.right,y(db));}SelectObject(dc,oldPen);DeleteObject(pen);
  pen=CreatePen(PS_DASH,1,RGB(11,172,186));oldPen=SelectObject(dc,pen);MoveToEx(dc,plot.left,y(ceilingDb),nullptr);LineTo(dc,plot.right,y(ceilingDb));SelectObject(dc,oldPen);DeleteObject(pen);std::snprintf(value,sizeof(value),"%.1f dB",ceilingDb);label(dc,value,{plot.right-58,y(ceilingDb)-18,plot.right,y(ceilingDb)-3},RGB(124,157,167),10);
  if(history.current() && history.latest().header.sampleRate>0){
   const auto& latest=history.latest();const double span=latest.header.sampleRate*6,end=double(latest.header.endSample);
   const int saved=SaveDC(dc);IntersectClipRect(dc,plot.left,plot.top,plot.right,plot.bottom);
   for(unsigned tap=0;tap<2;++tap){pen=CreatePen(PS_SOLID,tap?2:1,tap?RGB(15,181,196):RGB(158,188,202));oldPen=SelectObject(dc,pen);bool started=false;std::uint64_t next=0;
    for(std::size_t i=0;i<history.count;++i){const auto& item=history.at(i);const auto& h=item.header;if(h.flags&analysisInvalid||!(h.flags&analysisInputAligned)){started=false;continue;}
     const int x=int(plot.right-(end-h.endSample)/span*(plot.right-plot.left));if(x<plot.left){started=false;continue;}
     const int yy=y(20*std::log10(std::max(1e-8,std::max(item.channels[2*tap].peak,item.channels[2*tap+1].peak))));
     if(!started||next!=h.startSample||h.flags&analysisGap)MoveToEx(dc,x,yy,nullptr);else LineTo(dc,x,yy);started=true;next=h.endSample;
    }SelectObject(dc,oldPen);DeleteObject(pen);
   }RestoreDC(dc,saved);auto r=rectangle(layout.readout);
   if(latest.effectFields&analysisReduction)std::snprintf(value,sizeof(value),"GR %.1f dB",latest.reductionDb);else std::snprintf(value,sizeof(value),"GR —");label(dc,value,{r.left,r.top,r.left+(r.right-r.left)*4/10,r.top+24},RGB(5,151,168),17);
   const int rw=r.right-r.left;auto level=[](double peak){char s[40];if(peak>0)std::snprintf(s,sizeof(s),"%.1f",20*std::log10(peak));else std::snprintf(s,sizeof(s),"−∞");return std::string(s);};
   std::snprintf(value,sizeof(value),"IN %s dBFS",level(std::max(latest.channels[0].peak,latest.channels[1].peak)).c_str());label(dc,value,{r.left+int(rw*.43),r.top+3,r.left+int(rw*.72),r.top+23},RGB(124,157,167),11);
   std::snprintf(value,sizeof(value),"OUT %s dBFS",level(std::max(latest.channels[2].peak,latest.channels[3].peak)).c_str());label(dc,value,{r.left+int(rw*.73),r.top+3,r.right,r.top+23},RGB(124,157,167),11);
  }else {label(dc,"GR —",rectangle(layout.readout),RGB(5,151,168),17);label(dc,history.availability==AnalysisAvailability::stale?tr("没有新的音频","No new audio"):tr("等待音频","Waiting for audio"),plot,RGB(124,157,167));}
  std::string activity=bypassed?tr("旁路 · 原始输入","Bypass · raw input"):tr("输入 / 输出 · 6 秒","Input / output · 6 s");if(history.current()&&(history.latest().header.flags&analysisTransportKnown)&&!(history.latest().header.flags&analysisPlaying))activity+=tr(" · 宿主已停止"," · Host stopped");
  const double rate=(busLayout.validFields&layoutSampleRate)?busLayout.sampleRate:0;char pdc[128];if(rate>0)std::snprintf(pdc,sizeof(pdc),tr("延迟 %.2f ms · %.0f 样本","PDC %.2f ms · %.0f samples"),1000*(std::ceil(rate*.005)+48)/rate,std::ceil(rate*.005)+48);else std::snprintf(pdc,sizeof(pdc),"%s",tr("延迟等待宿主采样率","PDC awaiting host rate"));
  auto footer=rectangle(layout.status);const int fw=footer.right-footer.left;
  label(dc,activity.c_str(),{footer.left,footer.top,footer.left+int(fw*.48),footer.bottom},RGB(124,157,167),10);
  label(dc,tr("输出增益在限幅之后","OUTPUT GAIN FOLLOWS THE LIMIT"),{footer.left+int(fw*.34),footer.top,footer.left+int(fw*.72),footer.bottom},RGB(124,157,167),9);
  label(dc,pdc,{footer.left+int(fw*.76),footer.top,footer.right,footer.bottom},RGB(124,157,167),10);SelectObject(dc,oldFont);DeleteObject(font);
 }
 static LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM a,LPARAM b){
  auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(w,message,a,b);
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN){auto dc=reinterpret_cast<HDC>(a);SetBkColor(dc,win::paused(w)?RGB(249,249,249):RGB(245,250,251));SetTextColor(dc,win::paused(w)?RGB(150,150,150):RGB(124,157,167));return reinterpret_cast<LRESULT>(self->background);}
  if(message==WM_NOTIFY && reinterpret_cast<NMHDR*>(b)->code==TTN_GETDISPINFOW){auto* info=reinterpret_cast<NMTTDISPINFOW*>(b);self->tipText=win::wide(self->help(reinterpret_cast<HWND>(info->hdr.idFrom)));info->lpszText=self->tipText.data();return 0;}
  if(message==WM_DRAWITEM){auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(b);if(item->hwndItem==self->outputHold || item->hwndItem==self->reductionHold){FillRect(item->hDC,&item->rcItem,self->background);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,win::paused(w)?RGB(150,150,150):RGB(124,157,167));auto old=SelectObject(item->hDC,self->font);auto text=win::windowText(item->hwndItem);RECT r=item->rcItem;DrawTextW(item->hDC,text.c_str(),int(text.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&r);SelectObject(item->hDC,old);return TRUE;}}
  if(message==WM_PAINT || message==WM_PRINTCLIENT){win::Paint p(w,true,message==WM_PRINTCLIENT?reinterpret_cast<HDC>(a):nullptr);p.graphics().Flush(Gdiplus::FlushIntentionSync);auto dc=p.dc();int saved=SaveDC(dc);int scale=int(std::lround(1000*win::scale(w)));SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,1000,nullptr);SetViewportExtEx(dc,scale,scale,nullptr);if(w==self->window)self->draw(dc);RestoreDC(dc,saved);return 0;}
  if(message==WM_COMMAND&&HIWORD(a)==BN_CLICKED&&(LOWORD(a)==102||LOWORD(a)==103)){if(LOWORD(a)==102)self->history.resetOutput();else self->history.resetGR();self->updateHolds();return 0;}
  if(message==WM_COMMAND&&LOWORD(a)==100&&HIWORD(a)==BN_CLICKED){const auto id=mode;if(self->services.beginEdit(self->services.owner,id)){self->services.performEdit(self->services.owner,id,SendMessageW(self->modeButton,BM_GETCHECK,0,0)==BST_CHECKED?0:1);self->services.endEdit(self->services.owner,id);}return 0;}
  if(message==WM_COMMAND&&LOWORD(a)==101&&HIWORD(a)==CBN_SELCHANGE&&algorithmRegistered()){if(self->services.beginEdit(self->services.owner,algorithmVersion)){self->services.performEdit(self->services.owner,algorithmVersion,SendMessageW(self->algorithmPicker,CB_GETCURSEL,0,0)==1?1:0);self->services.endEdit(self->services.owner,algorithmVersion);}return 0;}
  if((message==WM_MOUSEWHEEL||message==WM_VSCROLL)&&w==self->details){int next=self->scroll;if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(a)/WHEEL_DELTA*40;else switch(LOWORD(a)){case SB_LINEUP:next-=30;break;case SB_LINEDOWN:next+=30;break;case SB_PAGEUP:next-=int(self->layout.details.height);break;case SB_PAGEDOWN:next+=int(self->layout.details.height);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_TRACKPOS;GetScrollInfo(w,SB_VERT,&si);next=si.nTrackPos;break;}}self->scroll=next;self->resize(self->width,self->height);return 0;}
  return DefWindowProcW(w,message,a,b);
 }
public:
 ~WindowsEditor() override {for(auto& c:controls)c.reset();if(tooltip)DestroyWindow(tooltip);if(window)DestroyWindow(window);if(font)DeleteObject(font);if(background)DeleteObject(background);if(classAcquired && !--editorInstances)UnregisterClassW(className,instance);}
 bool attach(void* parent,const EditorServices& s) override {
  if(window||!parent||!s.readTarget||!s.beginEdit||!s.performEdit||!s.endEdit)return false;services=s;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&instance);
  WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=instance;c.lpszClassName=className;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!editorInstances && !RegisterClassW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;++editorInstances;classAcquired=true;
  window=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,reinterpret_cast<HWND>(parent),nullptr,instance,this);if(!window)return false;
  details=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,window,nullptr,instance,this);
  for(unsigned id:{1u,9u,3u,5u,4u,2u,6u,8u}){auto spec=parameters[id];if(id==input)spec.title="Input Gain";if(id==lookahead)spec.title="Requested Lookahead";DisplayPolicy policy;policy.decimals=1;policy.labelZh=id==lookahead?"前瞻请求":controlLabelsZh[id];if(id==1||id==9)policy.style=just::ControlStyle::vertical;controls[id]=RotaryControl::create((id==1||id==9)?window:details,s,spec,policy);if(!controls[id])return false;}
  algorithmLabel=CreateWindowExW(0,L"STATIC",L"Algorithm",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  algorithmPicker=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP,0,0,1,1,details,reinterpret_cast<HMENU>(101),instance,nullptr);
  SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Legacy compatibility"));SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Transparent insurance"));
  algorithmInfo=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  lookaheadInfo=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,details,nullptr,instance,nullptr);
  modeButton=CreateWindowExW(0,L"BUTTON",L"Live Sample Peak",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP,0,0,1,1,details,reinterpret_cast<HMENU>(100),instance,nullptr);
  outputHold=CreateWindowExW(0,L"BUTTON",L"Peak —",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(102),instance,nullptr);reductionHold=CreateWindowExW(0,L"BUTTON",L"Max GR —",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(103),instance,nullptr);
  background=CreateSolidBrush(RGB(245,250,251));INITCOMMONCONTROLSEX init{sizeof(init),ICC_WIN95_CLASSES};InitCommonControlsEx(&init);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,instance,nullptr);if(tooltip)SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,480);for(auto target:{algorithmPicker,modeButton,outputHold,reductionHold})addTooltip(target);
  if(s.view)refresh(*s.view,{});else resize(width,height);return true;
 }
 void resize(int w,int h) override {
  width=w;height=h;win::place(window,0,0,w,h);layout=PreviewLayout::make(w,h,advanced);
  for(unsigned id:{1u,9u})if(controls[id]){auto r=id==1?layout.input:layout.output;controls[id]->resize(int(r.x),int(r.y),int(r.width),int(r.height));}
  auto r=layout.details;win::place(details,r.x,r.y,r.width,r.height);const double dw=win::size(details).Width;
  const unsigned columns=dw<950?4:6;const int cell=int(dw/columns),document=96+int((6+columns-1)/columns)*146+8;scroll=std::clamp(scroll,0,std::max(0,document-int(r.height)));
  SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;si.nMax=document-1;si.nPage=UINT(r.height);si.nPos=scroll;SetScrollInfo(details,SB_VERT,&si,TRUE);
  const unsigned ids[]={3,5,4,2,6,8};for(unsigned i=0;i<6;++i)if(controls[ids[i]])controls[ids[i]]->resize(i%columns*cell,96+int(i/columns)*146-scroll,cell,140);
  win::place(algorithmLabel,8,6-scroll,48,22);win::place(algorithmPicker,58,2-scroll,std::min(250.,dw*.34),200);win::place(modeButton,326,2-scroll,std::max(120.,dw-334),28);win::place(algorithmInfo,8,34-scroll,dw-16,28);win::place(lookaheadInfo,8,64-scroll,dw-16,24);
  auto readout=layout.readout;win::place(outputHold,readout.x+readout.width*.42,readout.y+34,readout.width*.3,24);win::place(reductionHold,readout.x,readout.y+34,readout.width*.4,24);
  if(fontScale!=win::scale(window)){fontScale=win::scale(window);auto next=CreateFontW(-int(std::lround(11*win::scale(window))),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");for(auto control:{algorithmLabel,algorithmPicker,algorithmInfo,lookaheadInfo,modeButton,outputHold,reductionHold})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;}
  InvalidateRect(window,nullptr,FALSE);
 }
 void refresh(const EditorViewState& view,const StatusSnapshot& status) override {
  if(advanced!=view.advanced){for(auto& c:controls)if(c)c->refresh(false);scroll=0;SetFocus(window);}advanced=view.advanced;bypassed=status.bypassProtectionExit;
  const bool languageChanged=chinese!=(view.language==UiLanguage::chinese);chinese=view.language==UiLanguage::chinese;
  if(view.visualsPaused!=lastPaused){if(background)DeleteObject(background);background=CreateSolidBrush(view.visualsPaused?RGB(249,249,249):RGB(245,250,251));lastPaused=view.visualsPaused;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
  ShowWindow(details,advanced?SW_SHOW:SW_HIDE);for(unsigned id=1;id<controls.size();++id)if(controls[id])controls[id]->refresh(id==1||id==9||advanced);
  const bool modern=algorithmRegistered()&&transparentVersion(services.readTarget(services.owner,algorithmVersion));const bool tp=services.readTarget(services.owner,mode)>=.5;
  SetWindowTextW(algorithmLabel,win::wide(tr("算法","Algorithm")).c_str());
  if(languageChanged || SendMessageW(algorithmPicker,CB_GETCOUNT,0,0)!=2){SendMessageW(algorithmPicker,CB_RESETCONTENT,0,0);SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(tr("旧版兼容（保留原声）","Legacy compatibility")).c_str()));SendMessageW(algorithmPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(win::wide(tr("透明保险（新版）","Transparent insurance")).c_str()));}
  SendMessageW(algorithmPicker,CB_SETCURSEL,modern?1:0,0);EnableWindow(algorithmPicker,algorithmRegistered());
  SetWindowTextW(modeButton,win::wide(modern?tr("采样峰值（关闭＝TP重建保护）","Sample peak (off = reconstructed TP)"):tr("旧Live（关闭＝旧Mix）","Legacy Live (off = legacy Mix)")).c_str());
  SetWindowTextW(algorithmInfo,win::wide(modern?tr("透明保险：中性、未触发时保持原波形；Input提响，Output独立后置。","Transparent insurance: neutral, untriggered audio stays intact; Input raises level and Output follows limiting."):tr("旧工程兼容路径。选择“透明保险”可升级算法，保留其他声音参数。","Legacy sound is preserved. Select Transparent insurance to upgrade while keeping other sound parameters.")).c_str());
  busLayout={};if(services.readBusLayout)services.readBusLayout(services.owner,busLayout);const double rate=(busLayout.validFields&layoutSampleRate)?busLayout.sampleRate:0;char timing[256];if(rate>0){const double request=parameters[lookahead].toPhysical(services.readTarget(services.owner,lookahead));const double actual=effectiveLookaheadMs(request,rate,modern,tp);if(modern&&tp)std::snprintf(timing,sizeof(timing),tr("前瞻请求 %.2f ms · 实际 %.2f ms · TP最多 %.2f ms（保留固定延迟）","Lookahead requested %.2f ms · effective %.2f ms · TP maximum %.2f ms (fixed latency)"),request,actual,maximumTpLookaheadMs(rate));else std::snprintf(timing,sizeof(timing),tr("前瞻请求 %.2f ms · 实际 %.2f ms","Lookahead requested %.2f ms · effective %.2f ms"),request,actual);}else std::snprintf(timing,sizeof(timing),"%s",tr("实际前瞻：等待宿主采样率","Effective lookahead: awaiting the host sample rate"));SetWindowTextW(lookaheadInfo,win::wide(timing).c_str());
  SendMessageW(modeButton,BM_SETCHECK,tp?BST_UNCHECKED:BST_CHECKED,0);
  if(visualResumeGeneration!=view.visualResumeGeneration){history={};visualResumeGeneration=view.visualResumeGeneration;}if(!view.visualsPaused)history.refresh(services);
  updateHolds();resize(width,height);
 }
};
EditorContent* createEditor(){return new WindowsEditor;}
}
