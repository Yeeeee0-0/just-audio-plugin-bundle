#include "EditorModel.hpp"
#include "EditorLayout.hpp"
#include "PanelPlacement.hpp"
#include "common/ui/PresentationClock.hpp"
#include "common/ui/VisualAssetsWindows.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <chrono>
#include <memory>
#include <mutex>
#ifdef _MSC_VER
#pragma comment(lib,"gdiplus.lib")
#endif

namespace just {
namespace {
using namespace eq;
using Gdiplus::Color;
using Gdiplus::PointF;
using Gdiplus::RectF;
static double eqMilliseconds() {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::wstring eqWide(const char* text) {
    if(!text || !*text)return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),n);result.pop_back();return result;
}
static DisplayPolicy eqPanelPolicy() {DisplayPolicy p;p.dark=true;p.valueFontSize=20;p.labelFontSize=13;p.valueFieldHeight=28;return p;}
static PanelRect united(PanelRect a,PanelRect b) {double x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.right(),b.right())-x,std::max(a.bottom(),b.bottom())-y};}
static bool inside(PanelRect r,POINT p) {return p.x>=r.x && p.x<r.right() && p.y>=r.y && p.y<r.bottom();}
static RectF gdipRect(PanelRect r) {return {float(r.x),float(r.y),float(r.w),float(r.h)};}

// This registry lives in the EQ DLL, not in a header shared by other effects.
// Keep the class alive until every editor using its callback has destroyed its
// HWND. A failed CreateWindowEx releases the same reference as a normal close.
constexpr wchar_t eqWindowClassName[]=L"JUST.EQ.Content.v010";
struct EqWindowClassRegistry {
    std::mutex mutex;
    HINSTANCE instance=nullptr;
    WNDPROC procedure=nullptr;
    unsigned references=0;
    bool registered=false;
};
EqWindowClassRegistry eqWindowClassRegistry;
static bool retainEqWindowClass(const WNDCLASSW& c) {
    auto& state=eqWindowClassRegistry;
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.registered){
        if(state.instance!=c.hInstance || state.procedure!=c.lpfnWndProc)return false;
    }else{
        // Do not adopt an unknown/stale class on ERROR_CLASS_ALREADY_EXISTS.
        if(!RegisterClassW(&c))return false;
        state.instance=c.hInstance;state.procedure=c.lpfnWndProc;state.registered=true;
    }
    ++state.references;return true;
}
static void releaseEqWindowClass() {
    auto& state=eqWindowClassRegistry;
    std::lock_guard<std::mutex> lock(state.mutex);
    if(!state.references)return;
    if(--state.references==0 && UnregisterClassW(eqWindowClassName,state.instance)){
        state.registered=false;state.instance=nullptr;state.procedure=nullptr;
    }
    // An unexpected surviving HWND makes UnregisterClass fail. Retain the known
    // registration rather than claiming success or adopting a different class.
}

// Windows adapter of the same model, placement and presentation contracts used
// by EditorMac.mm. Drawing and hit testing use logical content coordinates.
// The floating card is composited as one surface so its controls fade together.
struct EqWindowsEditor final:EditorContent {
    EditorModel model;
    PanelPresentation presentation;
    PresentationClock presentationClock;
    PanelPlacement placement;
    HWND window=nullptr,valueEdit=nullptr,tooltips=nullptr;
    HMODULE module=nullptr;
    HFONT editFont=nullptr;
    HBRUSH editBackground=nullptr;
    ULONG_PTR graphicsToken=0;
    int width=680,height=460,scroll=0,documentHeight=460,mainHeight=460;
    PanelRect graph{},panel{};
    bool panelAvailable=false,compact=false,dragging=false,soloHeld=false,paused=false,trackingMouse=false;
    bool inLayout=false,finishingText=false,foregroundWasActive=false;
    bool classReference=false,attached=false;
    POINT lastDrag{},pointer{-1000,-1000};
    double dragHz=1000,dragDb=0,rangeDb=18,hoverAlpha=.72,hoverFrom=.72,hoverTarget=.72,hoverAt=0,soloRenewAt=0,fontScale=0;
    int knob=-1,focusKnob=-1,editKnob=-1;
    ParamID knobID=0;std::size_t editIndex=0;
    RotaryDrag rotaryDrag;
    TextEditSession textSession;
    std::size_t controlsBand=bandCount;
    std::vector<std::unique_ptr<RotaryControl>> advancedControls;
    std::array<std::wstring,6> tooltipText;
    static constexpr int advancedTags[]={range,threshold,knee,attack,release,1001,1002};
    static constexpr const char* shapeEn[]={"Bell","Low Shelf","High Shelf","Low Cut","High Cut","Notch"};
    static constexpr const char* shapeZh[]={"钟形","低架","高架","低切","高切","陷波"};
    static constexpr const char* fieldZh[]={"立体声","中置 Mid","侧边 Side"};

    EqWindowsEditor(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphicsToken,&input,nullptr);}
    ~EqWindowsEditor() override {
        cancel();finishText(false);advancedControls.clear();
        if(tooltips){DestroyWindow(tooltips);tooltips=nullptr;}
        if(window){DestroyWindow(window);window=nullptr;}
        attached=false;
        if(classReference){releaseEqWindowClass();classReference=false;}
        if(editFont)DeleteObject(editFont);if(editBackground)DeleteObject(editBackground);
        if(graphicsToken)Gdiplus::GdiplusShutdown(graphicsToken);
    }
    std::wstring word(const char* zh,const char* en) const {auto* v=model.editorServices().view;return eqWide(v?localized(*v,zh,en):en);}
    double now() const {return presentationClock.now(eqMilliseconds());}
    bool activeWindow() const {return window && GetAncestor(window,GA_ROOT)==GetAncestor(GetForegroundWindow(),GA_ROOT);}
    Color color(unsigned rgb,double alpha=1) const {
        BYTE r=BYTE(rgb>>16),g=BYTE(rgb>>8),b=BYTE(rgb);
        if(paused)r=g=b=BYTE((unsigned(r)*2126+unsigned(g)*7152+unsigned(b)*722)/10000);
        return Color(BYTE(std::clamp(alpha,0.,1.)*255),r,g,b);
    }
    Color fieldColor(int f,double alpha=1) const {return color(fieldStyles[std::clamp(f,0,2)].rgb,alpha);}
    double xFor(double hz) const {return graph.x+std::log(hz/20)/std::log(1000)*graph.w;}
    double yFor(double db) const {return graph.y+graph.h/2-std::clamp(db,-rangeDb,rangeDb)/(2*rangeDb)*graph.h;}
    double hzFor(double x) const {return std::clamp(20*std::pow(1000.,(x-graph.x)/graph.w),20.,20000.);}
    PointF node(std::size_t b) const {return {float(xFor(physical(model.state,index(b,frequency)))),float(yFor(int(physical(model.state,index(b,type)))<=2?physical(model.state,index(b,gain)):0))};}
    PanelRect selectedBadge() const {auto p=node(model.selected);double w=int(model.value(type))<=2?128:86;return {std::clamp(double(p.X)-w/2,graph.x,graph.right()-w),std::max(graph.y,double(p.Y)-44),w,26};}
    PanelRect modeBadge(PointF p,int f) const {double x=f==1?p.X-34:p.X+14;if(x<graph.x)x=p.X+14;else if(x+20>graph.right())x=p.X-34;return {x,std::clamp(double(p.Y)-9,graph.y,graph.bottom()-18),20,18};}
    PanelRect relative(double x,double y,double w,double h) const {return {panel.x+x,panel.y+y,w,h};}
    bool acceptsPanel() const {return model.panelVisible && panelAvailable && presentation.acceptsInput(now());}
    bool enabledKnob(int i) const {return i!=1 || int(model.value(type))<=2;}
    std::size_t knobIndex(int i) const {return index(model.selected,i==0?frequency:i==1?gain:q);}
    PanelRect knobCell(int i) const {return relative(12+i*120,32,112,96);}
    PanelRect knobValue(int i) const {auto r=knobCell(i);auto g=ControlGeometry::layout(r.w,r.h,eqPanelPolicy());return {r.x,r.y+g.valueTop,r.w-30,g.valueHeight};}
    void showSelection() {presentation.select(now());placement.reset();focusKnob=-1;}
    void buildAdvanced() {
        if(controlsBand==model.selected)return;
        advancedControls.clear();controlsBand=model.selected;
        const char* titles[]={"Range","Threshold","Knee","Attack","Release","Input","Output"};
        const char* zh[]={"范围","阈值","拐点","起音","释放","输入","输出"};
        for(int i=0;i<7;++i){int tag=advancedTags[i];auto idx=tag>=1000?std::size_t(tag-1000):index(model.selected,Field(tag));auto spec=parameters[idx];spec.title=titles[i];DisplayPolicy p;p.labelZh=zh[i];advancedControls.push_back(RotaryControl::create(window,model.editorServices(),spec,p));}
    }
    bool attach(void* parent,const EditorServices& services) override {
        if(attached || classReference || window || !parent || !graphicsToken)return false;model.connect(services);
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&proc),&module))return false;
        WNDCLASSW c{};c.style=CS_DBLCLKS;c.lpfnWndProc=proc;c.hInstance=module;c.lpszClassName=eqWindowClassName;c.hCursor=LoadCursor(nullptr,IDC_ARROW);
        if(!retainEqWindowClass(c))return false;classReference=true;
        window=CreateWindowExW(0,c.lpszClassName,L"JUST EQ",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN|WS_VSCROLL,0,0,width,height,static_cast<HWND>(parent),nullptr,module,this);
        if(!window){releaseEqWindowClass();classReference=false;return false;}
        attached=true;
        INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_WIN95_CLASSES};InitCommonControlsEx(&ic);
        tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,module,nullptr);
        if(tooltips){SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,430);for(UINT_PTR i=1;i<=tooltipText.size();++i){TOOLINFOW t{sizeof(t)};t.uFlags=TTF_SUBCLASS;t.hwnd=window;t.uId=i;t.lpszText=const_cast<wchar_t*>(L"");SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));}}
        editBackground=CreateSolidBrush(RGB(15,60,73));foregroundWasActive=activeWindow();
        if(model.panelVisible)showSelection();SetTimer(window,1,33,nullptr);sync();resize(width,height);return true;
    }
    void setVisualsPaused(bool p) override {
        presentationClock.setPaused(p,eqMilliseconds());paused=p;
        if(editBackground)DeleteObject(editBackground);editBackground=CreateSolidBrush(p?RGB(51,51,51):RGB(15,60,73));
        if(window){layout();InvalidateRect(window,nullptr,FALSE);}
    }
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {
        if(paused!=view.visualsPaused)setVisualsPaused(view.visualsPaused);const bool old=model.advanced;sync();
        if(old!=model.advanced){cancel();finishText(true);scroll=0;resize(width,height);}
    }
    void sync() {
        model.refresh();if(soloHeld && !model.soloToken)stopSolo();
        if(controlsBand!=model.selected){finishText(true);buildAdvanced();}
        if(editKnob>=0 && !enabledKnob(editKnob))finishText(false);
        if(!model.panelVisible)presentation.hide();
        for(std::size_t i=0;i<advancedControls.size();++i)if(advancedControls[i])advancedControls[i]->refresh(i>=5 || int(model.value(type))<=2);
        layout();if(window)InvalidateRect(window,nullptr,FALSE);
    }
    void resize(int w,int h) override {
        width=w;height=h;if(window)win::place(window,0,0,w,h);layout();
    }
    void layout() {
        if(!window || inLayout)return;inLayout=true;
        if(fontScale!=win::scale(window)){fontScale=win::scale(window);HFONT previous=editFont;editFont=CreateFontW(-int(std::lround(20*fontScale)),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");if(valueEdit)SendMessageW(valueEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);if(previous)DeleteObject(previous);}
        mainHeight=mainViewHeight(height,model.advanced);const int columns=width<600?3:4,rows=width<600?3:2;
        documentHeight=model.advanced?std::max(height,mainHeight+130+rows*150):mainHeight;
        if(!model.advanced)scroll=0;scroll=std::clamp(scroll,0,std::max(0,documentHeight-height));
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,documentHeight-1,UINT(height),scroll,0};SetScrollInfo(window,SB_VERT,&si,TRUE);
        graph={40,20,double(std::max(280,width-92)),double(mainHeight-52)};
        auto p=node(model.selected);PanelRect reserved{p.X-12,p.Y-12,24,24};if(int(model.value(target)))reserved=united(reserved,modeBadge(p,int(model.value(target))));
        reserved={reserved.x-PanelPlacement::nodeClearance,reserved.y-PanelPlacement::nodeClearance,reserved.w+2*PanelPlacement::nodeClearance,reserved.h+2*PanelPlacement::nodeClearance};reserved=united(reserved,selectedBadge());
        auto placed=placement.place(graph,p.X,reserved,dragging,presentation.layoutLocked(),presentation.choice());presentation.automaticForm(placed.form);panelAvailable=placed.available;compact=placed.form==PanelPresentation::Form::compact;if(placed.available)panel=placed.rect;
        if(!acceptsPanel() && valueEdit)finishText(false);
        if(valueEdit){auto r=knobValue(editKnob);win::place(valueEdit,r.x,r.y-scroll,r.w,r.h);}
        const double cellWidth=(width-32.)/columns;
        for(std::size_t i=0;i<advancedControls.size();++i)if(advancedControls[i]){advancedControls[i]->resize(int(16+(i%columns)*cellWidth),mainHeight+124+int(i/columns)*150-scroll,int(cellWidth-10),140);ShowWindow(static_cast<HWND>(advancedControls[i]->nativeHandle()),model.advanced?SW_SHOWNA:SW_HIDE);}
        updateTooltips(placed.temporarilyCoversNode);inLayout=false;
    }
    void updateTooltips(bool covers) {
        char analyzerInfo[256];const auto& s=model.analyzer.settings;
        std::snprintf(analyzerInfo,sizeof(analyzerInfo),"%u dB range · %u samples · %.1f dB/oct tilt (1 kHz pivot). Display preferences only. 0 tilt shows calibrated dBFS; Hann off-bin tones may read up to 1.42 dB lower.",unsigned(s.rangeDb),unsigned(s.fftSize),s.tiltDbPerOctave);
        tooltipText[0]=eqWide(analyzerInfo);
        tooltipText[1]=word("按住并左右拖动，试听输入频率区域。松开、失焦或超时恢复 EQ。","Hold and drag horizontally to sweep the incoming frequency region. Cuts audition the removed side. Release, focus loss, close or lease timeout restores EQ.");
        tooltipText[2]=word("仅修改所选频段。单声道输入时 Side 不工作。","Selected band only. Side is inactive on mono input.");
        tooltipText[3]=compact?word("展开频段控制","Expand band controls"):word("收起频段控制","Collapse band controls");
        tooltipText[4]=word("6、12、18、24、36、48、72、96 dB/oct。保留已有工程的自动化 ID。","6, 12, 18, 24, 36, 48, 72, 96 dB/oct. Existing project slopes retain their original automation IDs.");
        tooltipText[5]=covers?word("手动展开：可能暂时覆盖所选节点；可收起","Manually expanded: may temporarily cover the selected node; collapse to clear it"):model.isCut()?word("滚轮调整斜率，单位 dB/oct","Wheel adjusts slope in dB/oct"):word("滚轮调整 Q","Wheel adjusts Q");
        PanelRect areas[]={{graph.right()-132,0,124,18},relative(372,76,112,28),relative(372,4,94,28),compact?relative(226,4,24,24):relative(472,5,24,24),relative(140,59,96,30),compact?relative(0,0,220,26):relative(0,0,84,30)};
        for(UINT_PTR i=0;tooltips && i<tooltipText.size();++i){auto r=areas[i];if(i && (!acceptsPanel() || (compact && (i==1 || i==2 || i==4))))r={};TOOLINFOW t{sizeof(t)};t.hwnd=window;t.uId=i+1;double scale=win::scale(window);t.rect={LONG(r.x*scale),LONG((r.y-scroll)*scale),LONG(r.right()*scale),LONG((r.bottom()-scroll)*scale)};t.lpszText=tooltipText[i].data();SendMessageW(tooltips,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&t));SendMessageW(tooltips,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&t));}
        // There is intentionally no main-graph explanatory tooltip.
    }
    void text(Gdiplus::Graphics& g,const std::wstring& value,PanelRect r,Color c,float size=11,bool center=false,bool bold=false) const {
        Gdiplus::Font font(L"Segoe UI",size,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush brush(c);Gdiplus::StringFormat f;f.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);f.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);if(center)f.SetAlignment(Gdiplus::StringAlignmentCenter);auto rect=gdipRect(r);g.DrawString(value.c_str(),int(value.size()),&font,rect,&f,&brush);
    }
    void line(Gdiplus::Graphics& g,double x1,double y1,double x2,double y2,Color c,float weight=1) const {Gdiplus::Pen pen(c,weight);g.DrawLine(&pen,float(x1),float(y1),float(x2),float(y2));}
    void rounded(Gdiplus::Graphics& g,PanelRect r,double radius,Color fill,Color stroke=Color(0,0,0,0),float weight=1) const {
        if(r.w<=0 || r.h<=0)return;Gdiplus::GraphicsPath path;float d=float(std::min(radius*2,std::min(r.w,r.h)));float x=float(r.x),y=float(r.y),w=float(r.w),h=float(r.h);
        path.AddArc(x,y,d,d,180.f,90.f);path.AddArc(x+w-d,y,d,d,270.f,90.f);path.AddArc(x+w-d,y+h-d,d,d,0.f,90.f);path.AddArc(x,y+h-d,d,d,90.f,90.f);path.CloseFigure();Gdiplus::SolidBrush b(fill);g.FillPath(&b,&path);if(stroke.GetA()){Gdiplus::Pen p(stroke,weight);g.DrawPath(&p,&path);}
    }
    void drawButton(Gdiplus::Graphics& g,PanelRect r,const std::wstring& label,double fade=1,bool enabled=true,bool menu=false) const {
        const double a=fade*(enabled?1:.45);rounded(g,r,5,color(0x245566,a),color(0x438392,a));text(g,label,{r.x+4,r.y+5,r.w-(menu?18:8),r.h-5},color(0xeafcff,a),13,true);if(menu)text(g,L"⌄",{r.right()-16,r.y+4,14,r.h-4},color(0xb9dce4,a),12,true);
    }
    void drawKnob(Gdiplus::Graphics& g,int i,double fade) const {
        auto r=knobCell(i);auto geom=ControlGeometry::layout(r.w,r.h,eqPanelPolicy());double d=geom.diameter,cx=r.x+r.w/2,cy=r.y+geom.top+d/2;
        const double n=model.state.targets[knobIndex(i)],a=fade*(enabledKnob(i)?1:.45);const char* en[]={"Frequency","Gain","Q"},*zh[]={"频率","增益","Q"};text(g,word(zh[i],en[i]),{r.x,r.y,r.w,22},color(0xb9dce4,a),13,true,true);
        auto arc=[&](double amount,unsigned rgb){std::array<PointF,101> points{};for(int j=0;j<=100;++j){double rad=rotaryAngle(amount*j/100.)*3.141592653589793/180.;points[j]={float(cx+std::sin(rad)*d*.43),float(cy-std::cos(rad)*d*.43)};}Gdiplus::Pen pen(color(rgb,a),3);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);g.DrawLines(&pen,points.data(),int(points.size()));};arc(1,0x438392);arc(n,0x68e5ed);
        Gdiplus::SolidBrush disc(color(0x245566,a));Gdiplus::Pen edge(color(0x438392,a));RectF dr(float(cx-d*.345),float(cy-d*.345),float(d*.69),float(d*.69));g.FillEllipse(&disc,dr);g.DrawEllipse(&edge,dr);double angle=rotaryAngle(n)*3.141592653589793/180.;line(g,cx+std::sin(angle)*d*.16,cy-std::cos(angle)*d*.16,cx+std::sin(angle)*d*.27,cy-std::cos(angle)*d*.27,color(0xadf6fc,a),3);
        auto value=knobValue(i);rounded(g,{r.x,value.y,r.w,value.h},4,color(0x0f3c49,.94*fade));char buf[64];if(i==0)std::snprintf(buf,sizeof(buf),"%.0f",model.value(frequency));else std::snprintf(buf,sizeof(buf),i==1?"%.1f":"%.2f",model.value(i==1?gain:q));
        if(editKnob!=i)text(g,eqWide(buf),value,color(0xeafcff,a),20,true,true);text(g,i==0?L"Hz":i==1?L"dB":L"Q",{value.right()+3,value.bottom()-18,27,18},color(0x9ec2cd,a),10);
        if(focusKnob==i && GetFocus()==window)rounded(g,{r.x-1,r.y-1,r.w+2,r.h+2},5,Color(0,0,0,0),color(0x68e5ed,.7*a));
    }
    void paintPanel(Gdiplus::Graphics& g) {
        if(!acceptsPanel())return;const double fade=presentation.fade(now());
        rounded(g,panel,12,color(0x0f3c49,hoverAlpha*fade),color(0x367383,fade));
        std::wstring title=word("频段","BAND")+L" "+std::to_wstring(model.selected+1);
        if(compact){title+=L" · "+word(shapeZh[int(model.value(type))],shapeEn[int(model.value(type))])+L" · "+word(fieldZh[int(model.value(target))],targetLabels[int(model.value(target))]);text(g,title,relative(12,7,204,18),color(0x82e3eb,fade),12,false,true);char s[128];double hz=model.value(frequency);char f[32];std::snprintf(f,sizeof(f),hz>=1000?"%.2fk":"%.0f Hz",hz>=1000?hz/1000:hz);if(model.isCut())std::snprintf(s,sizeof(s),"%s · %s dB/oct · Q %.2f",f,slopeChoiceLabels[model.slopeChoice()],model.value(q));else std::snprintf(s,sizeof(s),"%s · %+.1f dB · Q %.2f",f,model.value(gain),model.value(q));rounded(g,relative(12,29,208,20),3,color(0x0f3c49,.94*fade));text(g,eqWide(s),relative(12,29,208,20),color(0xffffff,fade),13);}
        else {
            text(g,L"● "+title,relative(12,9,76,18),color(0x82e3eb,fade),13,false,true);
            drawButton(g,relative(88,4,105,28),word(shapeZh[int(model.value(type))],shapeEn[int(model.value(type))]),fade,true,true);
            drawButton(g,relative(372,4,94,28),word(fieldZh[int(model.value(target))],targetLabels[int(model.value(target))]),fade,true,true);
            for(int i=0;i<3;++i)if(i!=1 || !model.isCut())drawKnob(g,i,fade);
            if(model.isCut()){text(g,word("斜率","Slope"),relative(132,32,112,20),color(0xb9dce4,fade),13,true);drawButton(g,relative(140,59,96,30),eqWide(slopeChoiceLabels[model.slopeChoice()]),fade,true,true);text(g,L"dB/oct",relative(132,103,112,22),color(0x9ec2cd,fade),13,true);}
            drawButton(g,relative(372,76,112,28),model.soloToken?(model.soloStatus.phase==AuditionPhase::active?word("正在试听","Listening"):word("等待音频","Pending")):word("按住 SOLO","Hold SOLO"),fade,model.canSolo());
            text(g,word("删除频段","Delete band"),relative(372,108,112,22),color(0xb9dce4,fade),11,true);
        }
        text(g,compact?L"↗":L"⌄",compact?relative(226,4,24,24):relative(472,5,24,24),color(0xeafcff,fade),18,true);
        text(g,L"×",compact?relative(226,29,24,22):relative(472,44,24,24),color(0xeafcff,fade),18,true);
    }
    void paint(HDC printDC=nullptr) {
        win::Paint buffer(window,true,printDC);auto& g=buffer.graphics();
        {g.Clear(color(0xf5fbfc));g.TranslateTransform(0,float(-scroll));
            for(int tick=-3;tick<=3;++tick){double db=rangeDb*tick/3.,y=yFor(db);line(g,graph.x,y,graph.right(),y,color(tick?0xdedede:0xb3b3b3));char b[32];std::snprintf(b,sizeof(b),"%+.0f",db);text(g,eqWide(b),{6,y-6,32,16},color(0x74858a),10);}
            for(double hz:{20.,50.,100.,200.,500.,1000.,2000.,5000.,10000.,20000.}){double x=xFor(hz);line(g,x,graph.y,x,graph.bottom(),color(0xdedede));char b[32];std::snprintf(b,sizeof(b),hz>=1000?"%.0fk":"%.0f",hz>=1000?hz/1000:hz);text(g,eqWide(b),{std::clamp(x-8,graph.x,graph.right()-22),graph.bottom()+3,32,16},color(0x74858a),10);}
            const auto& settings=model.analyzer.settings;for(int tick=0;tick<=3;++tick){double db=-double(settings.rangeDb)*tick/3.;char b[32];std::snprintf(b,sizeof(b),"%.0f",db);text(g,eqWide(b),{graph.right()+5,graph.y+SpectrumDisplay::verticalPosition(db,settings.rangeDb)*graph.h-6,46,16},color(0x74858a),10);}
            text(g,L"EQ dB",{1,graph.y-16,40,16},color(0x74858a),9);text(g,settings.tiltDbPerOctave>0?L"dBFS*":L"dBFS",{graph.right()+2,graph.y-16,48,16},color(0x74858a),9);
            text(g,word("频谱分析 ⌄","Analyzer ⌄"),{graph.right()-132,0,124,18},color(0x345e68),11,true);
            auto saved=g.Save();g.SetClip(gdipRect(graph));
            if(model.analyzer.display.hasData())for(unsigned tap=0;tap<2;++tap){if((settings.source==AnalyzerSource::pre && tap==1)||(settings.source==AnalyzerSource::post && tap==0))continue;const std::size_t count=std::size_t(std::clamp(std::ceil(graph.w*win::scale(window))+1,2.,8192.));std::vector<PointF> points;points.reserve(count);for(std::size_t x=0;x<count;++x){auto m=model.analyzer.display.point(tap,x,count,settings);if(m.valid)points.push_back({float(graph.x+double(x)/(count-1)*graph.w),float(graph.y+SpectrumDisplay::verticalPosition(m.db,settings.rangeDb)*graph.h)});}if(points.size()>1){Gdiplus::GraphicsPath area;area.AddLines(points.data(),int(points.size()));area.AddLine(points.back(),PointF(points.back().X,float(graph.bottom())));area.AddLine(PointF(points.back().X,float(graph.bottom())),PointF(points.front().X,float(graph.bottom())));area.CloseFigure();Gdiplus::SolidBrush fill(tap?fieldColor(0,.055):color(0x808080,.10));g.FillPath(&fill,&area);Gdiplus::Pen pen(tap?fieldColor(0):color(0x808080),tap?1.35f:1.f);g.DrawLines(&pen,points.data(),int(points.size()));}}
            if(model.soloToken && model.soloStatus.phase==AuditionPhase::active){double hz=model.value(frequency),lo=hz/std::pow(2.,.5/std::max(.1,model.value(q))),hi=hz*std::pow(2.,.5/std::max(.1,model.value(q)));int shape=int(model.value(type));if(shape==3 || shape==1)lo=20;if(shape==4 || shape==2)hi=20000;Gdiplus::SolidBrush b(fieldColor(0,.1));g.FillRectangle(&b,float(xFor(std::max(20.,lo))),float(graph.y),float(xFor(std::min(20000.,hi))-xFor(std::max(20.,lo))),float(graph.h));}
            const bool separate=model.active(1)||model.active(2);for(int f=0;f<3;++f)if((f==0 && !separate && model.active(0))||(f>0 && separate && (model.active(0)||model.active(f)))){std::vector<PointF> points;for(int x=0;x<=int(graph.w);++x){double hz=hzFor(graph.x+x);if(hz>model.responseRate()*.499)break;auto db=model.finalResponse(hz,model.responseRate());points.push_back({float(graph.x+x),float(yFor(db[f==0?0:f-1]))});}Gdiplus::Pen p(fieldColor(f),f==0?2.8f:2.2f);if(f){const float dash[]={f==1?8.f:2.f,f==1?5.f:4.f};p.SetDashPattern(dash,2);}if(points.size()>1)g.DrawLines(&p,points.data(),int(points.size()));}g.Restore(saved);
            for(std::size_t b=0;b<bandCount;++b)if(physical(model.state,index(b,enabled))>.5){auto pt=node(b);int f=int(physical(model.state,index(b,target)));if(b==model.selected){Gdiplus::Pen guide(fieldColor(f),.5f);const float dash[]={3,4};guide.SetDashPattern(dash,2);g.DrawLine(&guide,pt.X,float(graph.y),pt.X,float(graph.bottom()));Gdiplus::SolidBrush white(color(0xffffff));g.FillEllipse(&white,pt.X-12,pt.Y-12,24.f,24.f);Gdiplus::Pen rim(fieldColor(f));g.DrawEllipse(&rim,pt.X-12,pt.Y-12,24.f,24.f);}Gdiplus::SolidBrush fill(fieldColor(f,model.soloToken && b!=model.selected?.3:1));g.FillEllipse(&fill,pt.X-6,pt.Y-6,12.f,12.f);Gdiplus::Pen border(color(0xffffff),b==model.selected?3.f:1.f);g.DrawEllipse(&border,pt.X-6,pt.Y-6,12.f,12.f);
                if(b==model.selected){char s[96],freq[32];double hz=model.value(frequency);std::snprintf(freq,sizeof(freq),hz>=1000?"%.2f kHz":"%.0f Hz",hz>=1000?hz/1000:hz);if(int(model.value(type))<=2)std::snprintf(s,sizeof(s),"%s · %+.1f dB",freq,model.value(gain));else std::snprintf(s,sizeof(s),"%s",freq);auto r=selectedBadge();rounded(g,r,6,color(0xffffff,.96),color(0xcfe0e6));text(g,eqWide(s),{r.x+7,r.y+6,r.w-10,r.h-6},color(0x336675),10);}
                if(f){auto r=modeBadge(pt,f);rounded(g,r,4,fieldColor(f,.22));text(g,f==1?L"M":L"S",{r.x+5,r.y+2,16,16},color(0x14313b),11);}}
            if(!model.active(0) && !model.active(1) && !model.active(2))text(g,word("所有频段关闭 · 双击添加频段","All bands off · double-click to add a band"),{graph.x+18,graph.y+graph.h/2-26,400,24},color(0x74858a),12);
            if(model.soloToken)text(g,word("SOLO · 左右拖动试听范围","SOLO · drag horizontally to sweep"),{20,33,360,18},fieldColor(0),10);
            if(model.analyzer.availability!=AnalysisAvailability::fresh)text(g,model.analyzer.availability==AnalysisAvailability::stale?word("频谱已暂停","Spectrum paused"):word("频谱不可用","Spectrum unavailable"),{20,graph.bottom()+24,210,18},color(0x74858a),9);
            if(model.advanced)paintAdvanced(g);paintPanel(g);
        }
    }
    void paintAdvanced(Gdiplus::Graphics& g) {
        drawButton(g,{180,double(mainHeight+10),116,28},L"±"+std::to_wstring(int(rangeDb))+L" dB",1,true,true);
        text(g,(model.value(enabled)>.5?L"☑ ":L"☐ ")+word("频段开启","Band on"),{316,double(mainHeight+14),130,28},color(0x19343d),13);
        int cols=width<600?3:4;double cw=(width-32.)/cols;const char* en[]={"Dynamic","Detector","Source"},*zh[]={"动态","检测器","来源"};const Field fields[]={dynamicEnabled,detector,source};
        for(int i=0;i<3;++i){text(g,word(zh[i],en[i]),{18+i*cw,double(mainHeight+53),cw-12,18},color(0x19343d),11);const char* const* labels=i==0?dynamic_enabledLabels:i==1?detectorLabels:sourceLabels;const char* translations[3][2]={{"关闭","开启"},{"峰值","均方根"},{"内部","外部侧链"}};drawButton(g,{18+i*cw,double(mainHeight+73),cw-12,30},word(translations[i][int(model.value(fields[i]))],labels[int(model.value(fields[i]))]),1,int(model.value(type))<=2,true);}
    }
    int hit(POINT p) const {
        auto near=[&](std::size_t b){auto n=node(b);return physical(model.state,index(b,enabled))>.5 && std::hypot(double(p.x)-n.X,double(p.y)-n.Y)<16;};
        if(near(model.selected))return int(model.selected);for(int b=11;b>=0;--b)if(near(b))return b;return -1;
    }
    void stopSolo(){soloHeld=false;model.endSolo();presentation.activity(PanelPresentation::Activity::solo,false,now());}
    void finishKnob(){if(knob>=0){model.endValueGesture(knobID);knob=-1;presentation.activity(PanelPresentation::Activity::knobDrag,false,now());}}
    void cancel(){dragging=false;presentation.activity(PanelPresentation::Activity::nodeDrag,false,now());finishKnob();stopSolo();model.cancelDrag();if(window && GetCapture()==window)ReleaseCapture();}
    void finishText(bool commit) {
        if(!valueEdit || finishingText)return;finishingText=true;HWND edit=valueEdit;valueEdit=nullptr;const int i=editKnob;editKnob=-1;
        wchar_t wide[256]{};char utf8[1024]{};GetWindowTextW(edit,wide,256);WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof(utf8),nullptr,nullptr);
        if(commit && i>=0){const auto idx=editIndex;double next=0;if(textSession.changed(parameters[idx],eqPanelPolicy(),utf8,next))model.writeID(parameters[idx].id,next);}
        RemoveWindowSubclass(edit,editProc,1);DestroyWindow(edit);presentation.activity(PanelPresentation::Activity::textFocus,false,now());finishingText=false;
    }
    void startText(int i) {
        if(!acceptsPanel() || dragging || !enabledKnob(i))return;cancel();finishText(true);focusKnob=i;editKnob=i;auto r=knobValue(i);auto idx=knobIndex(i);editIndex=idx;const auto initial=textSession.begin(parameters[idx],eqPanelPolicy(),model.state.targets[idx]);
        presentation.activity(PanelPresentation::Activity::textFocus,true,now());valueEdit=CreateWindowExW(0,L"EDIT",eqWide(initial.c_str()).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|ES_RIGHT,int(r.x),int(r.y)-scroll,int(r.w),int(r.h),window,nullptr,module,nullptr);
        if(valueEdit){win::place(valueEdit,r.x,r.y-scroll,r.w,r.h);SendMessageW(valueEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);SetWindowSubclass(valueEdit,editProc,1,reinterpret_cast<DWORD_PTR>(this));SetFocus(valueEdit);SendMessageW(valueEdit,EM_SETSEL,0,-1);}else{editKnob=-1;presentation.activity(PanelPresentation::Activity::textFocus,false,now());}
    }
    int popup(HMENU menu,POINT client,bool panelMenu=true) {
        if(panelMenu)presentation.activity(PanelPresentation::Activity::menu,true,now());client.x=LONG(client.x*win::scale(window));client.y=LONG(client.y*win::scale(window));ClientToScreen(window,&client);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,client.x,client.y,0,window,nullptr);DestroyMenu(menu);if(panelMenu)presentation.activity(PanelPresentation::Activity::menu,false,now());return chosen;
    }
    int choiceMenu(POINT p,const std::vector<std::wstring>& options,int selected,bool panelMenu=true) {auto m=CreatePopupMenu();for(std::size_t i=0;i<options.size();++i)AppendMenuW(m,MF_STRING|(int(i)==selected?MF_CHECKED:0),UINT_PTR(i+1),options[i].c_str());return popup(m,{p.x,p.y-scroll},panelMenu)-1;}
    void analyzerMenu(POINT p) {
        auto menu=CreatePopupMenu();const char* groupsEn[]={"Source","Range","Tilt","Resolution","Response"},*groupsZh[]={"来源","范围","倾斜","分辨率","响应"};const auto& s=model.analyzer.settings;const int selected[]={int(s.source),int((s.rangeDb-60u)/30u),s.tiltDbPerOctave>0?1:0,s.fftSize==8192?1:0,int(s.response)};
        const std::vector<std::wstring> choices[]={ {word("处理前","Pre"),word("处理后","Post"),word("前 + 后","Pre + Post")},{L"60 dB",L"90 dB",L"120 dB"},{word("0 dB/oct · 测量","0 dB/oct · measurement"),word("4.5 dB/oct · 1 kHz 基准","4.5 dB/oct · 1 kHz pivot")},{word("4096 · 默认","4096 · default"),word("8192 · 精细","8192 · fine")},{word("快","Fast"),word("中","Medium"),word("慢","Slow")} };
        for(int group=0;group<5;++group){auto sub=CreatePopupMenu();for(std::size_t i=0;i<choices[group].size();++i)AppendMenuW(sub,MF_STRING|(int(i)==selected[group]?MF_CHECKED:0),UINT_PTR((group+1)*100+int(i)),choices[group][i].c_str());AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(sub),word(groupsZh[group],groupsEn[group]).c_str());}
        int command=popup(menu,{p.x,p.y-scroll},false);if(!command)return;auto next=s;int choice=command%100;switch(command/100){case 1:next.source=AnalyzerSource(choice);break;case 2:next.rangeDb=60+30*choice;break;case 3:next.tiltDbPerOctave=choice?4.5:0;break;case 4:next.fftSize=choice?8192:4096;break;case 5:next.response=AnalyzerResponse(choice);break;default:return;}model.configureAnalyzer(next);sync();
    }
    void deleteBand(){cancel();finishText(true);model.write(enabled,0);model.hidePanel();focusKnob=-1;sync();}
    void panelClick(POINT p,bool doubleClick) {
        if(!acceptsPanel() || dragging)return;presentation.interact(now());
        if(inside(compact?relative(226,4,24,24):relative(472,5,24,24),p)){finishText(true);presentation.choose(compact?PanelPresentation::Form::full:PanelPresentation::Form::compact,now());sync();return;}
        if(inside(compact?relative(226,29,24,22):relative(472,44,24,24),p)){cancel();finishText(true);model.hidePanel();sync();return;}
        if(compact)return;
        std::vector<std::wstring> opts;
        if(inside(relative(88,4,105,28),p)){for(int i=0;i<6;++i)opts.push_back(word(shapeZh[i],shapeEn[i]));int c=choiceMenu(p,opts,int(model.value(type)));if(c>=0){cancel();model.write(type,c);}sync();return;}
        if(inside(relative(372,4,94,28),p)){for(int i=0;i<3;++i)opts.push_back(word(fieldZh[i],targetLabels[i]));int c=choiceMenu(p,opts,int(model.value(target)));if(c>=0){cancel();model.write(target,c);}sync();return;}
        if(model.isCut() && inside(relative(140,59,96,30),p)){for(auto* s:slopeChoiceLabels)opts.push_back(eqWide(s));int c=choiceMenu(p,opts,model.slopeChoice());if(c>=0){cancel();model.writeSlopeChoice(c);}sync();return;}
        if(inside(relative(372,106,112,22),p)){deleteBand();return;}
        if(inside(relative(372,76,112,28),p) && model.canSolo() && activeWindow()){cancel();soloHeld=model.beginSolo();if(soloHeld){presentation.activity(PanelPresentation::Activity::solo,true,now());model.beginSoloSweep();dragHz=model.value(frequency);lastDrag=p;soloRenewAt=eqMilliseconds();SetCapture(window);}sync();return;}
        for(int i=0;i<3;++i)if((i!=1 || !model.isCut()) && inside(knobCell(i),p) && enabledKnob(i)){focusKnob=i;if(inside(knobValue(i),p)){startText(i);return;}auto r=knobCell(i);if(p.y<r.y+22 || p.y>=knobValue(i).y)return;auto idx=knobIndex(i);if(doubleClick){model.writeID(parameters[idx].id,parameters[idx].toNormalized(parameters[idx].initial));sync();return;}finishKnob();knobID=parameters[idx].id;if(model.beginValueGesture(knobID)){knob=i;rotaryDrag.begin(model.state.targets[idx],p.y);presentation.activity(PanelPresentation::Activity::knobDrag,true,now());SetCapture(window);}sync();return;}
    }
    void leftDown(POINT p,bool doubleClick) {
        finishText(true);SetFocus(window);
        if(inside({graph.right()-132,0,124,18},p)){analyzerMenu(p);return;}
        if(acceptsPanel() && inside(panel,p)){panelClick(p,doubleClick);return;}
        if(model.advanced && p.y>=mainHeight){advancedClick(p);return;}
        if(!inside(graph,p))return;cancel();focusKnob=-1;int b=hit(p);
        if(b>=0){lastDrag=p;dragHz=physical(model.state,index(b,frequency));dragDb=physical(model.state,index(b,gain));model.beginDrag(b);showSelection();dragging=true;presentation.activity(PanelPresentation::Activity::nodeDrag,true,now());SetCapture(window);sync();}
        else if(doubleClick){model.create(hzFor(p.x));if(model.panelVisible)showSelection();sync();}else{model.hidePanel();sync();}
    }
    void advancedClick(POINT p) {
        if(inside({180,double(mainHeight+10),116,28},p)){const double ranges[]={24,18,12,6};int current=1;for(int i=0;i<4;++i)if(rangeDb==ranges[i])current=i;int c=choiceMenu(p,{L"±24 dB",L"±18 dB",L"±12 dB",L"±6 dB"},current,false);if(c>=0)rangeDb=ranges[c];sync();return;}
        if(inside({316,double(mainHeight+10),130,28},p)){cancel();model.write(enabled,model.value(enabled)>.5?0:1);sync();return;}
        if(int(model.value(type))>2)return;double cw=(width-32.)/(width<600?3:4);const Field fields[]={dynamicEnabled,detector,source};const std::vector<std::wstring> choices[]={{word("关闭","Off"),word("开启","On")},{word("峰值","Peak"),word("均方根","RMS")},{word("内部","Internal"),word("外部侧链","External")}};for(int i=0;i<3;++i)if(inside({18+i*cw,double(mainHeight+73),cw-12,30},p)){int c=choiceMenu(p,choices[i],int(model.value(fields[i])),false);if(c>=0)model.write(fields[i],c);sync();return;}
    }
    void rightDown(POINT p) {
        finishText(true);SetFocus(window);if(acceptsPanel() && !compact && inside(panel,p)){for(int i=0;i<3;++i)if((i!=1 || !model.isCut()) && enabledKnob(i) && inside(knobCell(i),p)){auto idx=knobIndex(i);model.writeID(parameters[idx].id,parameters[idx].toNormalized(parameters[idx].initial));presentation.interact(now());sync();return;}return;}
        if(!inside(graph,p))return;cancel();int b=hit(p);auto menu=CreatePopupMenu();
        if(b<0){bool free=false;for(std::size_t i=0;i<bandCount;++i)free|=physical(model.state,index(i,enabled))<.5;AppendMenuW(menu,MF_STRING|(free?0:MF_GRAYED),1,word("添加频段","Add band").c_str());if(popup(menu,{p.x,p.y-scroll},false)==1){model.create(hzFor(p.x));if(model.panelVisible)showSelection();}sync();return;}
        model.select(b);showSelection();sync();AppendMenuW(menu,MF_STRING,1,word("关闭频段","Disable band").c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(int i=0;i<6;++i)AppendMenuW(menu,MF_STRING|(int(model.value(type))==i?MF_CHECKED:0),10+i,(word("形状 · ","Shape · ")+word(shapeZh[i],shapeEn[i])).c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(int i=0;i<3;++i)AppendMenuW(menu,MF_STRING|(int(model.value(target))==i?MF_CHECKED:0),20+i,(word("声场 · ","Target · ")+word(fieldZh[i],targetLabels[i])).c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,30,word("复制到空闲频段","Duplicate to unused band").c_str());AppendMenuW(menu,MF_STRING,31,word("删除频段","Delete band").c_str());
        int c=popup(menu,{p.x,p.y-scroll});if(c==1)model.write(enabled,0);else if(c>=10 && c<16)model.write(type,c-10);else if(c>=20 && c<23)model.write(target,c-20);else if(c==30){if(model.duplicate())showSelection();else MessageBeep(MB_OK);}else if(c==31){deleteBand();return;}sync();
    }
    void move(POINT p) {
        pointer=p;if(!trackingMouse){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,window,0};TrackMouseEvent(&t);trackingMouse=true;}
        const bool fine=(GetKeyState(VK_SHIFT)&0x8000)!=0;const double amount=fine?.1:1;
        if(soloHeld){dragHz=std::clamp(dragHz*std::pow(2.,(p.x-lastDrag.x)*amount/120),20.,20000.);lastDrag=p;model.sweepSolo(dragHz);sync();}
        else if(dragging){dragHz=std::clamp(dragHz*std::pow(1000.,(p.x-lastDrag.x)*amount/graph.w),20.,20000.);dragDb=std::clamp(dragDb-(p.y-lastDrag.y)*amount*2*rangeDb/graph.h,-24.,24.);lastDrag=p;model.drag(dragHz,dragDb);sync();}
        else if(knob>=0){double n=rotaryDrag.move(p.y,fine);model.updateValueGesture(knobID,n);double actual=model.state.targets[knobIndex(knob)];if(std::abs(actual-n)>1e-12)rotaryDrag.accumulator=actual;InvalidateRect(window,nullptr,FALSE);}
    }
    void wheel(POINT p,int delta) {
        if(dragging || soloHeld || knob>=0 || !delta)return;const int b=hit(p);const bool overPanel=acceptsPanel() && inside(panel,p);
        if(overPanel && !compact){for(int i=0;i<3;++i)if(inside(knobValue(i),p))return;if(inside(relative(88,4,105,28),p)||inside(relative(372,4,94,28),p)||(model.isCut() && inside(relative(140,59,96,30),p)))return;}
        if(b>=0 || overPanel){if(b>=0 && !overPanel){finishText(true);SetFocus(window);cancel();model.select(b);showSelection();}else presentation.interact(now());model.wheel(delta*.05,(GetKeyState(VK_SHIFT)&0x8000)!=0);sync();}
        else if(model.advanced){scroll-=delta/3;layout();InvalidateRect(window,nullptr,FALSE);}
    }
    void tick() {
        const bool foreground=activeWindow();if(foregroundWasActive && !foreground){finishText(true);cancel();presentation.activity(PanelPresentation::Activity::textFocus,false,now());presentation.activity(PanelPresentation::Activity::menu,false,now());}foregroundWasActive=foreground;
        // The lease always uses wall/monotonic time; bypass pauses only visuals.
        if(soloHeld && (!foreground || !(GetAsyncKeyState(VK_LBUTTON)&0x8000) || GetCapture()!=window))cancel();
        if(soloHeld && eqMilliseconds()-soloRenewAt>=75){soloRenewAt=eqMilliseconds();if(!model.renewSolo())cancel();}
        if(!paused){const double at=now(),targetAlpha=(foreground && inside(panel,pointer))||presentation.layoutLocked()||dragging?1:.72;if(targetAlpha!=hoverTarget){hoverFrom=hoverAlpha;hoverTarget=targetAlpha;hoverAt=at;}const double t=std::clamp((at-hoverAt)/160.,0.,1.),ease=t*t*(3-2*t);hoverAlpha=hoverFrom+(hoverTarget-hoverFrom)*ease;}
        layout();InvalidateRect(window,nullptr,FALSE);
    }
    void key(WPARAM k) {
        if(k==VK_ESCAPE){cancel();finishText(false);model.hidePanel();sync();return;}
        if(k==VK_DELETE || k==VK_BACK){deleteBand();return;}
        if(k==VK_TAB){cancel();finishText(true);model.select((model.selected+1)%bandCount);showSelection();sync();return;}
        if(focusKnob>=0 && acceptsPanel() && enabledKnob(focusKnob) && (k==VK_UP || k==VK_DOWN || k==VK_LEFT || k==VK_RIGHT)){auto idx=knobIndex(focusKnob);double step=(GetKeyState(VK_SHIFT)&0x8000)?.001:.01;model.writeID(parameters[idx].id,std::clamp(model.state.targets[idx]+((k==VK_UP || k==VK_RIGHT)?step:-step),0.,1.));presentation.interact(now());sync();}
        else if(k==VK_RETURN && focusKnob>=0)startText(focusKnob);
    }
    static LRESULT CALLBACK editProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto* v=reinterpret_cast<EqWindowsEditor*>(data);
        if(m==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
        if(m==WM_KEYDOWN && (w==VK_RETURN || w==VK_ESCAPE)){v->finishText(w==VK_RETURN);SetFocus(v->window);v->sync();return 0;}
        if(m==WM_KILLFOCUS){v->finishText(true);v->sync();return 0;}
        if(m==WM_MOUSEWHEEL)return DefSubclassProc(h,m,w,l);
        return DefSubclassProc(h,m,w,l);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* v=reinterpret_cast<EqWindowsEditor*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){v=static_cast<EqWindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}
        if(!v)return DefWindowProcW(h,m,w,l);auto logical=win::point(h,l);POINT p{LONG(logical.X),LONG(logical.Y)+v->scroll};
        switch(m){
        case WM_PAINT:v->paint();return 0;
        case WM_PRINTCLIENT:v->paint(reinterpret_cast<HDC>(w));return 0;
        case WM_ERASEBKGND:return 1;
        case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTTAB|DLGC_WANTCHARS;
        case WM_LBUTTONDOWN:v->leftDown(p,false);return 0;
        case WM_LBUTTONDBLCLK:v->leftDown(p,true);return 0;
        case WM_MOUSEMOVE:v->move(p);return 0;
        case WM_MOUSELEAVE:v->pointer={-1000,-1000};v->trackingMouse=false;return 0;
        case WM_LBUTTONUP:v->cancel();v->sync();return 0;
        case WM_CAPTURECHANGED:if(v->dragging || v->soloHeld || v->knob>=0){v->cancel();v->sync();}return 0;
        case WM_CANCELMODE:v->finishText(true);v->cancel();v->sync();return 0;
        case WM_KILLFOCUS:if(reinterpret_cast<HWND>(w)!=v->valueEdit){v->cancel();v->sync();}return 0;
        case WM_RBUTTONDOWN:v->rightDown(p);return 0;
        case WM_MOUSEWHEEL:p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);p.x=LONG(p.x/win::scale(h));p.y=LONG(p.y/win::scale(h))+v->scroll;v->wheel(p,GET_WHEEL_DELTA_WPARAM(w));return 0;
        case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,SB_VERT,&si);switch(LOWORD(w)){case SB_THUMBTRACK:v->scroll=si.nTrackPos;break;case SB_LINEUP:v->scroll-=30;break;case SB_LINEDOWN:v->scroll+=30;break;case SB_PAGEUP:v->scroll-=v->height;break;case SB_PAGEDOWN:v->scroll+=v->height;break;case SB_TOP:v->scroll=0;break;case SB_BOTTOM:v->scroll=v->documentHeight;break;}v->layout();InvalidateRect(h,nullptr,FALSE);return 0;}
        case WM_KEYDOWN:v->key(w);return 0;
        case WM_TIMER:if(w==1)v->tick();return 0;
        case WM_CTLCOLOREDIT:if(reinterpret_cast<HWND>(l)==v->valueEdit){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,v->paused?RGB(248,248,248):RGB(234,252,255));SetBkColor(dc,v->paused?RGB(51,51,51):RGB(15,60,73));return reinterpret_cast<LRESULT>(v->editBackground);}break;
        case WM_DESTROY:KillTimer(h,1);return 0;
        case WM_NCDESTROY:{
            // The host may destroy its parent before deleting EditorContent.
            // Do not retain a stale HWND that could later be reused by Win32.
            SetWindowLongPtrW(h,GWLP_USERDATA,0);
            if(v->window==h){v->window=nullptr;v->valueEdit=nullptr;v->tooltips=nullptr;}
            return DefWindowProcW(h,m,w,l);
        }
        }
        return DefWindowProcW(h,m,w,l);
    }
};
}
EditorContent* createEqEditor(){return new EqWindowsEditor;}
}
