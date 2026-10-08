#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#include "EditorModel.hpp"
#include "FeedbackHistory.hpp"
#include "PreviewLayout.hpp"
#include "common/ui/Controls.hpp"
#include <memory>
#include <vector>
#include <string>
namespace just::delay {
class WindowsContent final:public EditorContent {
    struct Row {HWND label=nullptr,control=nullptr,field=nullptr;ParamID id=0;bool simple=false,custom=false;int y=0,x=0;std::unique_ptr<ControlServicesAdapter> adapter;std::unique_ptr<RotaryControl> rotary;};
    HWND view=nullptr,note=nullptr,pingButton=nullptr;
    EditorServices services{};
    std::unique_ptr<Gesture> gesture;
    std::vector<Row> rows;
    bool advanced=false;
    PingPongRoute pingRoute;
    FeedbackHistory analysisHistory;
    int widthPixels=680,heightPixels=310,scroll=0,totalHeight=0;
    static std::wstring wide(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,out.data(),n);out.resize(n-1);return out;}
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<WindowsContent*>(GetWindowLongPtr(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<WindowsContent*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
                if(!self->advanced){
                    const auto geometry=PreviewLayout::fit(self->widthPixels,self->heightPixels);
                    const auto& card=geometry.graph;
                    RECT box{card.x,card.y,card.x+card.width,card.y+card.height};
                    HBRUSH panel=CreateSolidBrush(RGB(246,251,253));FillRect(dc,&box,panel);DeleteObject(panel);
                    int left=box.left+42,right=box.right-18;
                    int first=box.top+int(card.height*.37),second=box.top+int(card.height*.7);
                    auto grid=CreatePen(PS_SOLID,1,RGB(210,228,233));auto oldGrid=SelectObject(dc,grid);
                    for(int i=0;i<=10;++i){int x=left+(right-left)*i/10;MoveToEx(dc,x,box.top+30,nullptr);LineTo(dc,x,box.bottom-24);}
                    for(int y:{first,second}){MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);}
                    SelectObject(dc,oldGrid);DeleteObject(grid);
                    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(90,102,108));
                    const auto* latest=self->analysisHistory.latest();const wchar_t* title=L"L / R wet repeats: data unavailable";
                    if(self->analysisHistory.availability()==AnalysisAvailability::stale)title=L"L / R wet repeats: no new audio";
                    else if(self->analysisHistory.availability()==AnalysisAvailability::fresh && latest){
                        if(!(latest->effectFields&analysisWet))title=L"Wet measurement unavailable";
                        else if(!(latest->header.flags&analysisInputAligned))title=L"Waiting for PDC alignment";
                        else if(latest->header.flags&analysisBypassed)title=L"Actual L / R wet repeats: bypass";
                        else if(latest->header.flags&analysisTransportKnown && !(latest->header.flags&analysisPlaying))title=L"Actual L / R wet repeats: host stopped";
                        else if(std::max(latest->channels[0].peak,latest->channels[1].peak)==0)
                            title=std::max(latest->channels[4].peak,latest->channels[5].peak)>0?L"Input silent: repeats active":L"Actual L / R wet repeats: silence";
                        else title=L"Actual L / R wet repeats: input / wet peaks";
                    }
                    RECT caption{box.left+180,box.top+10,box.right-12,box.top+24};DrawTextW(dc,title,-1,&caption,DT_SINGLELINE|DT_END_ELLIPSIS);
                    TextOutW(dc,box.left+16,box.top+10,L"ECHO TRAIL · measured",21);
                    TextOutW(dc,box.left+16,first-6,L"L",1);TextOutW(dc,box.left+16,second-6,L"R",1);
                    TextOutW(dc,left,box.bottom-18,L"−2500 ms",8);TextOutW(dc,right-22,box.bottom-18,L"now",3);
                    if(latest && self->analysisHistory.availability()==AnalysisAvailability::fresh){
                        const double end=double(latest->header.endSample),span=latest->header.sampleRate*2.5;
                        const int amplitude=std::clamp(int(card.height*.13),4,33);
                        for(unsigned lane=0;lane<2;++lane)for(unsigned series=0;series<2;++series){
                            HPEN pen=CreatePen(PS_SOLID,series?3:2,series?RGB(18,184,199):RGB(102,148,161));auto old=SelectObject(dc,pen);
                            self->analysisHistory.each([&](const AnalysisWindow& item){const auto& h=item.header;
                                if(!(h.flags&analysisInputAligned) || (series && !(item.effectFields&analysisWet)) || (lane && h.outputChannels<2))return;
                                const double x=right-(end-double(h.endSample))/span*(right-left);if(x<left || x>right)return;
                                const unsigned channel=series?4+lane:(h.inputChannels==1?0:lane);
                                const double peak=std::clamp(item.channels[channel].peak,0.,1.);if(peak<=1e-8)return;
                                const int y=lane?second:first,a=int(peak*amplitude);
                                MoveToEx(dc,int(x),y-a,nullptr);LineTo(dc,int(x),y+a);
                            });SelectObject(dc,old);DeleteObject(pen);
                        }
                    }
                }EndPaint(window,&paint);return 0;}
            if(message==WM_DRAWITEM && wp==40000){
                auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);const bool on=value(editorTargets(self->services),route)==2;
                RECT rect=item->rcItem;HDC dc=item->hDC;SetBkMode(dc,TRANSPARENT);
                auto fill=CreateSolidBrush(on?RGB(217,245,245):RGB(255,255,255));auto border=CreatePen(PS_SOLID,1,on?RGB(132,212,217):RGB(213,226,231));
                auto oldFill=SelectObject(dc,fill),oldPen=SelectObject(dc,border);RoundRect(dc,rect.left+1,rect.top+1,rect.right-1,rect.bottom-1,32,32);
                SelectObject(dc,oldFill);SelectObject(dc,oldPen);DeleteObject(fill);DeleteObject(border);
                SetTextColor(dc,on?RGB(0,125,138):RGB(99,131,143));RECT label{14,0,90,rect.bottom};DrawTextW(dc,L"Ping Pong",-1,&label,DT_SINGLELINE|DT_VCENTER);
                auto track=CreateSolidBrush(on?RGB(27,185,197):RGB(214,226,229));oldFill=SelectObject(dc,track);oldPen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,92,7,123,24,17,17);SelectObject(dc,oldFill);DeleteObject(track);
                oldFill=SelectObject(dc,GetStockObject(WHITE_BRUSH));Ellipse(dc,on?109:95,10,on?120:106,21);SelectObject(dc,oldFill);SelectObject(dc,oldPen);
                RECT stateText{136,0,rect.right-12,rect.bottom};DrawTextW(dc,on?L"ON":L"OFF",-1,&stateText,DT_SINGLELINE|DT_VCENTER);
                if(item->itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);DrawFocusRect(dc,&rect);}return TRUE;
            }
            if(message==WM_COMMAND && LOWORD(wp)==40000 && HIWORD(wp)==BN_CLICKED){
                auto state=editorTargets(self->services);
                const int next=self->pingRoute.next(state);
                if(self->gesture->begin(route)){self->gesture->update(double(next)/spec(route).stepCount);self->gesture->end();}
                self->refresh(*self->services.view,{});return 0;
            }
            if(message==WM_COMMAND && HIWORD(wp)==CBN_SELCHANGE){
                HWND control=reinterpret_cast<HWND>(lp);auto id=ParamID(LOWORD(wp)%10000);
                if(self->gesture->begin(id)){self->gesture->update(double(SendMessage(control,CB_GETCURSEL,0,0))/spec(id).stepCount);self->gesture->end();}
                self->refresh(*self->services.view,{});return 0;
            }
            if(message==WM_MOUSEWHEEL && self->advanced){self->scroll=std::clamp(self->scroll-GET_WHEEL_DELTA_WPARAM(wp)/3,0,std::max(0,self->totalHeight-self->heightPixels+65));self->layout();return 0;}
            if(message==WM_VSCROLL && self->advanced){
                switch(LOWORD(wp)){case SB_LINEUP:self->scroll-=30;break;case SB_LINEDOWN:self->scroll+=30;break;case SB_PAGEUP:self->scroll-=180;break;case SB_PAGEDOWN:self->scroll+=180;break;case SB_THUMBTRACK:self->scroll=HIWORD(wp);break;}
                self->scroll=std::clamp(self->scroll,0,std::max(0,self->totalHeight-self->heightPixels+65));self->layout();return 0;
            }
            if(message==WM_CAPTURECHANGED && self->gesture)self->gesture->end();
        }
        return DefWindowProc(window,message,wp,lp);
    }
    HWND child(const wchar_t* kind,const wchar_t* title,DWORD style,int id){
        HWND h=CreateWindowEx(0,kind,title,WS_CHILD|style,0,0,1,1,view,reinterpret_cast<HMENU>(std::intptr_t(id)),GetModuleHandle(nullptr),nullptr);
        SendMessage(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;
    }
    void add(ParamID id,bool simple,int y,int x){
        const auto& p=spec(id);Row row;row.id=id;row.simple=simple;row.y=y;row.x=x;
        if(p.enumLabels){row.label=child(L"STATIC",wide(p.title).c_str(),SS_LEFT,0);row.control=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,int(id+10000));for(unsigned i=0;i<=p.stepCount;++i)SendMessage(row.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.enumLabels[i]).c_str()));}
        else {
            row.adapter=std::make_unique<ControlServicesAdapter>(services,id);auto displaySpec=p;
            if(simple && id==timeL)displaySpec.title="Time";
            row.rotary=RotaryControl::create(view,row.adapter->services(),displaySpec,controlPolicy(id,false,simple,services.view));
            if(row.rotary)row.control=static_cast<HWND>(row.rotary->nativeHandle());
        }
        rows.push_back(std::move(row));
    }
    void layout(){
        ShowScrollBar(view,SB_VERT,advanced);
        ShowWindow(pingButton,advanced?SW_HIDE:SW_SHOW);
        const auto geometry=PreviewLayout::fit(widthPixels,heightPixels);
        MoveWindow(pingButton,geometry.ping.x,geometry.ping.y,geometry.ping.width,geometry.ping.height,TRUE);
        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,totalHeight,UINT(std::max(1,heightPixels-65)),scroll,0};SetScrollInfo(view,SB_VERT,&si,TRUE);
        int columns=widthPixels<600?1:2;
        if(advanced){
            int y=12;
            for(auto& row:rows)if(!row.simple){row.x=(y%2);++y;}
            int ordinal=0;for(auto& row:rows)if(!row.simple){row.x=ordinal%columns;row.y=12+(ordinal/columns)*148;++ordinal;}totalHeight=12+((ordinal+columns-1)/columns)*148;
        }
        for(auto& r:rows){
            bool show=r.simple!=advanced;for(HWND h:{r.label,r.control,r.field})if(h)ShowWindow(h,show?SW_SHOW:SW_HIDE);
            if(!show)continue;
            int x,y,w;
            if(r.simple){const auto& cell=geometry.cells[r.x];w=cell.width;x=cell.x;y=cell.y;}
            else {w=(widthPixels-40)/columns;x=12+r.x*w;y=r.y-scroll;}
            if(r.label)MoveWindow(r.label,x,y,w-14,20,TRUE);
            if(spec(r.id).enumLabels)MoveWindow(r.control,x,y+23,w-18,260,TRUE);
            else if(r.rotary)r.rotary->resize(x,y,w-18,r.simple?geometry.cells[r.x].height:136);
        }
        ShowWindow(note,!advanced && geometry.note.height==0?SW_HIDE:SW_SHOW);
        if(advanced)MoveWindow(note,12,heightPixels-45,widthPixels-28,42,TRUE);
        else MoveWindow(note,geometry.note.x,geometry.note.y,geometry.note.width,geometry.note.height,TRUE);
    }
public:
    ~WindowsContent() override {if(gesture)gesture->end();rows.clear();if(view)DestroyWindow(view);}
    bool attach(void* parent,const EditorServices& s) override {
        services=s;gesture=std::make_unique<Gesture>(s);
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_BAR_CLASSES};InitCommonControlsEx(&init);
        WNDCLASS cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"JustDelayContent";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        if(!RegisterClass(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        view=CreateWindowEx(0,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,680,310,static_cast<HWND>(parent),nullptr,cls.hInstance,this);
        if(!view)return false;
        int i=0;for(auto id:{timeL,feedback,mix})add(id,true,0,i++);
        int y=12;for(std::size_t group=0;group<moduleDefinition().advancedGroupCount;++group){const auto& g=moduleDefinition().advancedGroups[group];for(std::size_t j=0;j<g.count;++j)add(g.parameters[j],false,y+(j/2)*65,int(j%2));y+=int((g.count+1)/2)*65+25;}
        totalHeight=y;note=child(L"STATIC",L"",SS_LEFT,0);ShowWindow(note,SW_SHOW);
        pingButton=child(L"BUTTON",L"Ping Pong",BS_OWNERDRAW|WS_TABSTOP,40000);
        refresh(*s.view,{});return true;
    }
    void resize(int w,int h) override {widthPixels=w;heightPixels=h;MoveWindow(view,0,0,w,h,TRUE);layout();}
    void refresh(const EditorViewState& state,const StatusSnapshot&) override {
        if(advanced!=state.advanced){gesture->end();scroll=0;}advanced=state.advanced;
        auto s=editorTargets(services);pingRoute.observe(s);
        analysisHistory.poll(services);
        SetWindowText(pingButton,value(s,route)==2?L"Ping Pong  ON":L"Ping Pong  OFF");
        InvalidateRect(pingButton,nullptr,TRUE);
        for(auto& row:rows){
            const bool visible=row.simple!=advanced,enabled=visible && editable(row.id,s,advanced);
            if(row.label)SetWindowText(row.label,wide(localized(*services.view,chineseLabel(row.id),spec(row.id).title)).c_str());
            if(row.rotary){
                const bool custom=row.simple && row.id==timeL && !simpleTimeEditable(s);
                if(custom!=row.custom){
                    row.rotary.reset();auto displaySpec=spec(row.id);displaySpec.title="Time";if(custom)displaySpec.unit="";
                    row.rotary=RotaryControl::create(view,row.adapter->services(),displaySpec,controlPolicy(row.id,custom,row.simple,services.view));
                    if(row.rotary)row.control=static_cast<HWND>(row.rotary->nativeHandle());row.custom=custom;
                }
                if(row.rotary)row.rotary->refresh(enabled);
            }else {EnableWindow(row.control,enabled);SendMessage(row.control,CB_SETCURSEL,std::size_t(value(s,row.id)),0);}
        }
        RuntimeTelemetrySnapshot telemetry;auto availability=services.readRuntimeTelemetry?services.readRuntimeTelemetry(services.owner,telemetry):TelemetryAvailability::unavailable;
        wchar_t tempo[96];if(availability==TelemetryAvailability::fresh && (telemetry.validFields&telemetryTempo))swprintf(tempo,96,L"Host tempo %.1f BPM",telemetry.bpm);
        else wcscpy_s(tempo,L"Host tempo unavailable");
        std::wstring message=advanced?L"Ping-Pong sums stereo input to its start side. ":L"L: ";
        if(!advanced){message+=wide(value(s,syncL)?notes[int(value(s,noteL))]:"Free ms");message+=L" · R: ";message+=wide(value(s,syncR)?notes[int(value(s,noteR))]:"Free ms");message+=simpleTimeEditable(s)?L" · ":L" · Time: Custom — Advanced · ";}
        message+=tempo;const auto* measured=analysisHistory.latest();
        if(analysisHistory.availability()==AnalysisAvailability::fresh && measured && (measured->effectFields&analysisDelay)){wchar_t actual[96];swprintf(actual,96,L" · Actual L/R %.1f / %.1f ms",measured->delayMs[0],measured->delayMs[1]);message+=actual;}
        SetWindowText(note,message.c_str());
        layout();
    }
};
EditorContent* createEditorContent(){return new WindowsContent;}
}
#endif
