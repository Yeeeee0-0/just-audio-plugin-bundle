#include <windows.h>
#include <commctrl.h>
#include <cwchar>
#include <vector>
#include <string>
#include "Editor.hpp"
#include "EditorModel.hpp"

namespace just::tremolo {
namespace {
constexpr ParamID amountControl=0xFFFFFFFEu;
struct Row {ParamID id;const wchar_t* title;bool advanced;HWND label=nullptr,control=nullptr,value=nullptr;};
class WindowsEditor final:public EditorContent {
    std::unique_ptr<EditorModel> model;
    HWND window=nullptr,note=nullptr;HFONT font=nullptr;
    bool advanced=false,refreshing=false;
    std::vector<Row> rows{
        {rateHz,L"Frequency (Hz)",false},{amountControl,L"Amount (dB)",false},{stereoPhase,L"Stereo Separation (°)",false},
        {shape,L"Waveform",true},{phase,L"Phase (°)",true},{stereoPhase,L"Stereo Phase (°)",true},{duty,L"Duty (%)",true},{edgeMs,L"Edge (ms)",true},
        {rateHz,L"Free Rate (Hz)",true},{sync,L"Sync",true},{division,L"Division",true},{timeMode,L"Time Mode",true},
        {depth,L"Depth (%)",true},{mix,L"Mix (%)",true},{inputGain,L"Input (dB)",true},{outputGain,L"Output (dB)",true}
    };
    bool editable(ParamID id) const {
        if(id==amountControl)return model->amountEditable();
        if(id==rateHz)return model->frequencyEditable();
        if(id==stereoPhase)return model->separationEditable();
        if(id==duty)return model->value(shape)==2;
        if(id==edgeMs)return model->value(shape)>=2;
        return true;
    }
    static bool choice(ParamID id){return id==shape || id==sync || id==division || id==timeMode;}
    static std::wstring wide(const char* source){std::wstring result;while(*source)result.push_back(*source++);return result;}
    HWND child(const wchar_t* type,const wchar_t* text,DWORD style,int id){
        HWND result=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return result;
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,w,l);
        if(msg==WM_HSCROLL && !self->refreshing){
            for(auto& row:self->rows)if(row.control==reinterpret_cast<HWND>(l) && !choice(row.id) && self->editable(row.id)){
                double n=double(SendMessageW(row.control,TBM_GETPOS,0,0))/10000;
                if(row.id==amountControl)self->model->writeAmount(amountDb(n,self->model->value(mix)*0.01));
                else self->model->writeNormalized(row.id,n);
                if(LOWORD(w)!=TB_THUMBTRACK)self->model->cancel();self->refreshValues();return 0;
            }
        }
        if(msg==WM_COMMAND && !self->refreshing){
            for(auto& row:self->rows){
                if(row.control==reinterpret_cast<HWND>(l) && choice(row.id) && HIWORD(w)==CBN_SELCHANGE){
                    self->model->writePhysical(row.id,double(SendMessageW(row.control,CB_GETCURSEL,0,0)));
                    self->model->cancel();self->refreshValues();return 0;
                }
                if(row.value==reinterpret_cast<HWND>(l) && HIWORD(w)==EN_KILLFOCUS && self->editable(row.id)){
                    wchar_t text[128];GetWindowTextW(row.value,text,128);wchar_t* end=nullptr;
                    double value=std::wcstod(text,&end);
                    if(row.id==amountControl && (std::wcscmp(text,L"-inf")==0 || std::wcscmp(text,L"−∞")==0))self->model->writeAmount(-INFINITY);
                    else if(end!=text && !*end && std::isfinite(value)){
                        if(row.id==amountControl)self->model->writeAmount(value);else self->model->writePhysical(row.id,value);
                    }
                    self->model->cancel();self->refreshValues();return 0;
                }
            }
        }
        if(msg==WM_PAINT){
            PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd,&paint);RECT rect;GetClientRect(hwnd,&rect);
            FillRect(dc,&rect,GetSysColorBrush(COLOR_WINDOW));
            if(!self->advanced && self->model->frequencyEditable()){
                HPEN pen=CreatePen(PS_SOLID,2,RGB(23,143,153));auto old=SelectObject(dc,pen);
                int width=rect.right,graph=std::min(340,int(width*0.6)),start=(width-graph)/2;
                for(int i=0;i<=240;++i){
                    double p=double(i)/240+self->model->value(phase)/360;
                    double d=self->model->value(duty)*0.01,e=edgePhase(self->model->value(edgeMs),self->model->value(rateHz),d,self->model->value(shape)==2);
                    double amplitude=1-self->model->value(mix)*0.01*self->model->value(depth)*0.01*(1-wave(unsigned(self->model->value(shape)),p,d,e));
                    int x=start+graph*i/240,y=20+int(60*(1-amplitude));
                    if(!i)MoveToEx(dc,x,y,nullptr);else LineTo(dc,x,y);
                }
                SelectObject(dc,old);DeleteObject(pen);
            }
            EndPaint(hwnd,&paint);return 0;
        }
        if(msg==WM_DESTROY){self->model->cancel();return 0;}
        return DefWindowProcW(hwnd,msg,w,l);
    }
    void refreshValues(){
        refreshing=true;
        for(auto& row:rows){
            bool visible=row.advanced==advanced;
            ShowWindow(row.label,visible?SW_SHOW:SW_HIDE);ShowWindow(row.control,visible?SW_SHOW:SW_HIDE);
            if(row.value)ShowWindow(row.value,visible?SW_SHOW:SW_HIDE);
            EnableWindow(row.control,editable(row.id));if(row.value)EnableWindow(row.value,editable(row.id));
            if(choice(row.id))SendMessageW(row.control,CB_SETCURSEL,int(model->value(row.id)),0);
            else {
                SendMessageW(row.control,TBM_SETPOS,TRUE,LPARAM(std::lround(model->target(row.id==amountControl?depth:row.id)*10000)));
                if(GetFocus()!=row.value){
                    wchar_t text[64];
                    if(row.id==amountControl && std::isinf(model->amount()))std::wcscpy(text,L"−∞");
                    else if(row.id==rateHz && !model->frequencyEditable())std::wcscpy(text,L"Sync");
                    else std::swprintf(text,64,L"%.2f",row.id==amountControl?model->amount():row.id==phase?fraction(model->value(phase)/360)*360:model->value(row.id));
                    SetWindowTextW(row.value,text);
                }
            }
        }
        SetWindowTextW(note,model->frequencyEditable()?L"Theoretical envelope · Channel information unavailable; Stereo is read-only":L"Theoretical envelope · Sync frequency/channel display unavailable");
        refreshing=false;InvalidateRect(window,nullptr,FALSE);
    }
public:
    ~WindowsEditor()override {if(model)model->cancel();if(window)DestroyWindow(window);if(font)DeleteObject(font);}
    bool attach(void* parent,const EditorServices& services)override {
        if(!parent || window)return false;model=std::make_unique<EditorModel>(services);
        INITCOMMONCONTROLSEX init{sizeof(init),ICC_BAR_CLASSES};InitCommonControlsEx(&init);
        WNDCLASSW klass{};klass.lpfnWndProc=procedure;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"JustTremoloModuleContent";
        klass.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&klass);
        window=CreateWindowExW(0,klass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,680,310,static_cast<HWND>(parent),nullptr,klass.hInstance,this);
        if(!window)return false;
        font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        int id=100;
        for(auto& row:rows){
            row.label=child(L"STATIC",row.title,0,id++);
            if(choice(row.id)){
                row.control=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,id++);
                const auto& p=spec(row.id);for(unsigned n=0;n<=p.stepCount;++n){auto label=wide(p.enumLabels[n]);SendMessageW(row.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
            }else{
                row.control=child(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,id++);SendMessageW(row.control,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));
                row.value=child(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_RIGHT|ES_AUTOHSCROLL,id++);
            }
        }
        note=child(L"STATIC",L"",0,id++);refreshValues();return true;
    }
    void resize(int width,int height)override {
        MoveWindow(window,0,0,width,height,TRUE);
        MoveWindow(note,8,std::max(0,height-26),std::max(1,width-16),26,TRUE);
        unsigned rowIndex=0,column=0;
        for(auto& row:rows){
            if(row.advanced!=advanced)continue;
            if(!advanced){
                int start=std::min(90,std::max(22,int(height*0.3)));
                int y=start+int(rowIndex)*std::max(26,std::min(65,(height-start-40)/3)),label=int(std::min(195.0,width*0.32));
                MoveWindow(row.label,8,y,label,24,TRUE);MoveWindow(row.control,label+18,y,std::max(40,width-label-115),24,TRUE);MoveWindow(row.value,width-82,y,74,24,TRUE);
            }else{
                int cell=width/3,x=column*cell+6,y=rowIndex*std::max(38,std::min(48,(height-34)/5));
                MoveWindow(row.label,x,y,cell-12,18,TRUE);
                if(choice(row.id))MoveWindow(row.control,x,y+18,cell-14,240,TRUE);
                else {MoveWindow(row.control,x,y+18,std::max(30,cell-82),22,TRUE);MoveWindow(row.value,x+cell-75,y+18,62,22,TRUE);}
            }
            ++rowIndex;if(advanced && ((column==0 && rowIndex==5) || (column==1 && rowIndex==4))){rowIndex=0;++column;}
        }
    }
    void refresh(const EditorViewState& state,const StatusSnapshot&)override {
        if(advanced!=state.advanced){model->cancel();advanced=state.advanced;RECT r;GetClientRect(window,&r);resize(r.right,r.bottom);}
        refreshValues();
    }
};
}
EditorContent* createEditorContent(){return new WindowsEditor;}
}
