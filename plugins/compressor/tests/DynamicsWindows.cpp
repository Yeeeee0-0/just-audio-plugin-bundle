// Windows-only UI service-contract fixture. Compile once per module with
// JUST_DYNAMICS_MODULE=4 (Compressor), 5 (Limiter), or 6 (Gate), that module's
// EditorWindows.cpp, and common/ui/ControlsWindows.cpp. Link user32, gdi32,
// comctl32, gdiplus. These synthetic measurements are NOT host/audio evidence.
#include "common/ui/VisualAssetsWindows.hpp"
#include "common/vst3/Module.hpp"
#if JUST_DYNAMICS_MODULE == 4
#include "plugins/compressor/Engine.hpp"
#elif JUST_DYNAMICS_MODULE == 5
#include "plugins/limiter/Editor.hpp"
#include "plugins/limiter/Parameters.hpp"
#elif JUST_DYNAMICS_MODULE == 6
#include "plugins/gate/UIModel.hpp"
#else
#error Select exactly one dynamics module
#endif
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
#if JUST_DYNAMICS_MODULE == 4
const auto registry=just::module::registry;
just::EditorContent* makeEditor(){return just::compressor::createCompressorEditor();}
#elif JUST_DYNAMICS_MODULE == 5
const auto registry=just::limiter::registry;
just::EditorContent* makeEditor(){return just::limiter::createEditor();}
#else
const auto registry=just::gate::registry;
just::EditorContent* makeEditor(){return just::gate::createEditorContent();}
#endif
struct Fixture {
    just::EditorViewState view;
    std::map<just::ParamID,double> targets;
    unsigned reads=0,cleanCursorReads=0,begins=0,writes=0,ends=0;
    just::ParamID edited=0;bool gesture=false;std::uint64_t sequence=1;
    Fixture(){for(std::size_t i=0;i<registry.count;++i){auto& p=registry.specs[i];targets[p.id]=p.toNormalized(p.initial);}view.language=just::UiLanguage::english;}
    just::EditorServices services(){
        just::EditorServices s;s.owner=this;s.view=&view;
        s.readTarget=[](void* p,just::ParamID id){return static_cast<Fixture*>(p)->targets.at(id);};
        s.beginEdit=[](void* p,just::ParamID id){auto& f=*static_cast<Fixture*>(p);if(f.gesture || !f.targets.count(id))return false;f.gesture=true;f.edited=id;++f.begins;return true;};
        s.performEdit=[](void* p,just::ParamID id,double n){auto& f=*static_cast<Fixture*>(p);if(!f.gesture || id!=f.edited || n<0 || n>1 || !std::isfinite(n))return false;f.targets[id]=n;++f.writes;return true;};
        s.endEdit=[](void* p,just::ParamID id){auto& f=*static_cast<Fixture*>(p);require(f.gesture && id==f.edited,"Unbalanced endEdit");f.gesture=false;++f.ends;};
        s.readBusLayout=[](void*,just::BusLayoutSnapshot& bus){bus={};bus.validFields=just::layoutSampleRate;bus.sampleRate=48000;return true;};
        s.readAnalysis=[](void* p,just::AnalysisCursor& cursor,just::AnalysisBatch& batch){auto& f=*static_cast<Fixture*>(p);++f.reads;if(!cursor.session)++f.cleanCursorReads;batch={};if(cursor.sequence==f.sequence)return just::AnalysisAvailability::fresh;
            auto& w=batch.windows[0];batch.count=1;w.header.session=w.header.epoch=1;w.header.sequence=f.sequence;w.header.startSample=(f.sequence-1)*480;w.header.endSample=f.sequence*480;w.header.sampleRate=48000;w.header.inputChannels=w.header.outputChannels=2;w.header.flags=just::analysisInputAligned|just::analysisPlaying|just::analysisTransportKnown;w.header.sourceNanoseconds=just::analysisNow();
            w.channels[0].peak=w.channels[1].peak=.75;w.channels[2].peak=w.channels[3].peak=.5;w.effectFields=just::analysisReduction|just::analysisSidechain|just::analysisGate;w.reductionDb=6.;w.gateState=2;cursor={1,1,f.sequence};return just::AnalysisAvailability::fresh;
        };return s;
    }
};
struct Search {const wchar_t* title=nullptr;int id=0;HWND found=nullptr;};
BOOL CALLBACK searchChild(HWND h,LPARAM parameter){auto& s=*reinterpret_cast<Search*>(parameter);if((s.title && just::win::windowText(h)==s.title) || (!s.title && GetDlgCtrlID(h)==s.id)){s.found=h;return FALSE;}return TRUE;}
HWND child(HWND root,const wchar_t* title,int id=0){Search s{title,id,nullptr};EnumChildWindows(root,searchChild,reinterpret_cast<LPARAM>(&s));return s.found;}
void refresh(just::EditorContent& editor,Fixture& fixture){editor.refresh(fixture.view,{});}
void checkPassiveContract(HWND root,just::EditorContent& editor,Fixture& f){
    const auto sound=f.targets;const unsigned writes=f.writes;
    for(double scale:{.75,1.,1.25,1.5}){f.view.renderScale=scale;for(bool advanced:{false,true}){f.view.advanced=advanced;editor.resize(1120,460);refresh(editor,f);}}
    for(auto language:{just::UiLanguage::chinese,just::UiLanguage::english}){f.view.language=language;refresh(editor,f);}
    require(f.targets==sound && f.writes==writes,"View/scale/language refresh changed sound targets");
    f.view.visualsPaused=true;refresh(editor,f);const auto reads=f.reads;++f.sequence;for(int n=0;n<4;++n)refresh(editor,f);require(f.reads==reads,"Paused display consumed analysis");
    const auto clears=f.cleanCursorReads;f.view.visualsPaused=false;++f.view.visualResumeGeneration;refresh(editor,f);require(f.reads>reads && f.cleanCursorReads>clears,"Resume did not start with a clean analysis cursor");
    // Rendering each custom child into a caller DC exercises WM_PRINTCLIENT
    // without showing windows, moving the pointer, or interacting with a host.
    auto dc=GetDC(root);HDC memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,1800,1000);auto previous=SelectObject(memory,bitmap);
    EnumChildWindows(root,[](HWND h,LPARAM data)->BOOL{wchar_t name[100]{};GetClassNameW(h,name,100);if(std::wstring(name).rfind(L"JUST.",0)==0)SendMessageW(h,WM_PRINTCLIENT,WPARAM(data),PRF_CLIENT);return TRUE;},reinterpret_cast<LPARAM>(memory));
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(root,dc);
}
void checkSpecificContract(HWND root,just::EditorContent& editor,Fixture& f){
#if JUST_DYNAMICS_MODULE == 4
    auto chart=child(root,L"Input/output envelope; drag threshold line");require(chart!=nullptr,"Compressor envelope missing");f.targets[100]=.5;refresh(editor,f);auto size=just::win::size(chart);const double scale=just::win::scale(chart);
    const int x=int(50*scale),y=int((36+(size.Height-66)*.5)*scale);auto before=f.targets;const auto begins=f.begins,ends=f.ends;
    SendMessageW(chart,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(chart,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,y+int(20*scale)));SendMessageW(chart,WM_CANCELMODE,0,0);
    require(f.begins==begins+1 && f.ends==ends+1 && !f.gesture,"Threshold drag did not balance its gesture on cancel");require(f.edited==100 && f.targets[100]<before[100],"Threshold line did not edit the existing threshold ID");before[100]=f.targets[100];require(before==f.targets,"Threshold line changed another parameter");
#elif JUST_DYNAMICS_MODULE == 5
    auto peak=child(root,nullptr,102),gr=child(root,nullptr,103);require(peak && gr,"Limiter hold reset controls missing");++f.sequence;refresh(editor,f);const auto sound=f.targets;const auto writes=f.writes;
    require(just::win::windowText(peak).find(L"dBFS")!=std::wstring::npos,"Measured output hold missing");require(just::win::windowText(gr).find(L"dB")!=std::wstring::npos,"Measured reduction hold missing");
    SendMessageW(GetParent(peak),WM_COMMAND,MAKEWPARAM(102,BN_CLICKED),reinterpret_cast<LPARAM>(peak));SendMessageW(GetParent(gr),WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),reinterpret_cast<LPARAM>(gr));
    require(just::win::windowText(peak).find(L"—")!=std::wstring::npos && just::win::windowText(gr).find(L"—")!=std::wstring::npos,"Hold reset did not clear display");refresh(editor,f);
    require(just::win::windowText(peak).find(L"—")!=std::wstring::npos,"Old sample repopulated output hold after reset");require(f.targets==sound && f.writes==writes,"Meter hold reset edited audio parameters");
#elif JUST_DYNAMICS_MODULE == 6
    const int id=500+int(just::gate::registry.index(just::gate::mode));auto menu=child(root,nullptr,id);require(menu!=nullptr,"Gate mode selector missing");f.view.advanced=true;refresh(editor,f);auto sound=f.targets;SendMessageW(menu,CB_SETCURSEL,2,0);SendMessageW(GetParent(menu),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(menu));
    require(f.edited==just::gate::mode && f.targets[just::gate::mode]==1. && !f.gesture,"Gate mode selector did not edit existing mode parameter");sound[just::gate::mode]=1.;require(f.targets==sound,"Gate mode selection changed other targets");
#endif
}
}
int main(){
    try {just::win::startup();for(unsigned pass=0;pass<2;++pass){Fixture fixture;auto root=CreateWindowExW(0,L"STATIC",L"JUST hidden dynamics service fixture",WS_OVERLAPPEDWINDOW,0,0,1800,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(root!=nullptr,"Hidden fixture window creation failed");SetPropW(root,just::win::viewProperty,reinterpret_cast<HANDLE>(&fixture.view));
        {std::unique_ptr<just::EditorContent> editor(makeEditor());require(editor && editor->attach(root,fixture.services()),"Editor attach failed");editor->resize(1120,460);checkPassiveContract(root,*editor,fixture);checkSpecificContract(root,*editor,fixture);require(!fixture.gesture && fixture.begins==fixture.ends,"Editor leaked gesture");}
        RemovePropW(root,just::win::viewProperty);DestroyWindow(root);
    }std::puts("Windows dynamics synthetic UI service contracts passed; not REAPER or audio acceptance.");return 0;}catch(const std::exception& e){std::fprintf(stderr,"Dynamics UI contract failed: %s\n",e.what());return 1;}
}
