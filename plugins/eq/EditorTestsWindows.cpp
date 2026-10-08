// Windows-only hidden-HWND contract tests. These do not constitute REAPER,
// listening, screenshot review, accessibility, or real mouse/keyboard acceptance.
// Build as a standalone executable with common/ui/ControlsWindows.cpp.
#include "EditorWindows.cpp"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace just;
using namespace just::eq;
namespace {
unsigned checks=0;
void check(bool yes,const char* message){++checks;if(!yes){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
struct Fixture {
    EditorViewState view;
    SoundState sound=initialState({},registry);
    AnalyzerPreferences preferences;
    std::set<ParamID> held;
    unsigned writes=0,begins=0,ends=0,reads=0,renewals=0,auditionEnds=0;
    std::uint64_t token=0,sequence=1;
    Fixture(){sound.targets[index(0,enabled)]=1;sound.targets[index(0,frequency)]=parameters[index(0,frequency)].toNormalized(1034.56789);sound.targets[index(0,gain)]=parameters[index(0,gain)].toNormalized(8.125);}
    EditorServices services(){
        EditorServices s;s.owner=this;s.view=&view;
        s.readTarget=[](void* p,ParamID id){return static_cast<Fixture*>(p)->sound.targets[registry.index(id)];};
        s.beginEdit=[](void* p,ParamID id){auto& f=*static_cast<Fixture*>(p);++f.begins;return f.held.insert(id).second;};
        s.performEdit=[](void* p,ParamID id,double n){auto& f=*static_cast<Fixture*>(p);check(f.held.count(id),"write requires open host gesture");++f.writes;f.sound.targets[registry.index(id)]=n;return true;};
        s.endEdit=[](void* p,ParamID id){auto& f=*static_cast<Fixture*>(p);check(f.held.erase(id)==1,"host gesture closes exactly once");++f.ends;};
        s.readBusLayout=[](void*,BusLayoutSnapshot& out){out={};out.validFields=layoutBuses|layoutSampleRate;out.inputChannels=out.outputChannels=2;out.sampleRate=96000;out.active=true;return true;};
        s.readAnalyzerPreferences=[](void* p,AnalyzerPreferences& out){out=static_cast<Fixture*>(p)->preferences;return true;};
        s.writeAnalyzerPreferences=[](void* p,const AnalyzerPreferences& in){static_cast<Fixture*>(p)->preferences=in;return true;};
        s.readExtendedSpectrum=[](void* p,ExtendedSpectrumSnapshot& out){auto& f=*static_cast<Fixture*>(p);++f.reads;out={};out.fftSize=f.preferences.fftSize;out.binCount=out.fftSize/2+1;out.hopSize=1024;out.hasEnvelope=true;out.configurationGeneration=1;out.presentationGeneration=f.view.visualResumeGeneration;out.header={1,1,f.sequence,f.sequence*4096,(f.sequence+1)*4096,0,96000,2,2,0,analysisInputAligned,analysisNow()};for(auto& values:out.envelopeDb)values.fill(-180);out.envelopeDb[0][44]=-12;out.envelopeDb[1][44]=-6;out.amplitude[0][44]=.25f;out.amplitude[1][44]=.5f;return AnalysisAvailability::fresh;};
        s.beginAudition=[](void* p,ParamID){return static_cast<Fixture*>(p)->token=42;};
        s.renewAudition=[](void* p,std::uint64_t t){auto& f=*static_cast<Fixture*>(p);++f.renewals;return t==f.token;};
        s.endAudition=[](void* p,std::uint64_t t){auto& f=*static_cast<Fixture*>(p);check(t==f.token,"end active solo token");f.token=0;++f.auditionEnds;};
        s.readAuditionStatus=[](void* p){auto& f=*static_cast<Fixture*>(p);return AuditionStatus{f.token,id(0,frequency),f.token?AuditionPhase::active:AuditionPhase::ended};};
        return s;
    }
};
void printPixels(EqWindowsEditor& editor,bool monochrome){
    RECT r{};GetClientRect(editor.window,&r);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
    SendMessageW(editor.window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    std::size_t chroma=0,dark=0;const auto* bytes=static_cast<const BYTE*>(pixels);for(std::size_t n=0;n<std::size_t(r.right)*r.bottom;++n,bytes+=4){if(bytes[0]!=bytes[1] || bytes[1]!=bytes[2])++chroma;if(bytes[0]<130 && bytes[1]<130 && bytes[2]<130)++dark;}
    check(monochrome?chroma==0:chroma>100,"WM_PRINTCLIENT canvas palette follows bypass");check(dark>100,"render contains real plotted/card content");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
}
int main(){
    auto f=std::make_unique<Fixture>();auto e=std::make_unique<EqWindowsEditor>();
    HWND parent=CreateWindowExW(0,L"STATIC",L"JUST EQ hidden contract fixture",WS_OVERLAPPED,0,0,1100,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(parent!=nullptr,"create hidden test parent");SetPropW(parent,win::viewProperty,reinterpret_cast<HANDLE>(&f->view));
    WNDCLASSW classInfo{};
    {
        auto failed=std::make_unique<EqWindowsEditor>();
        check(!failed->attach(reinterpret_cast<HWND>(static_cast<INT_PTR>(-1)),f->services()),"invalid child parent fails native HWND creation");
        check(!failed->attached && !failed->classReference && !failed->window && eqWindowClassRegistry.references==0,"failed creation balances class reference");
        check(!GetClassInfoW(failed->module,eqWindowClassName,&classInfo),"failed first creation unregisters its callback class");
    }
    check(e->attach(parent,f->services()),"attach Windows EQ canvas");e->resize(1000,596);
    check(e->attached && e->classReference && eqWindowClassRegistry.references==1,"successful attach owns exactly one class reference");
    check(!e->attach(parent,f->services()) && eqWindowClassRegistry.references==1,"duplicate attach cannot leak another class reference");
    {
        auto peer=std::make_unique<EqWindowsEditor>();
        check(peer->attach(parent,f->services()) && eqWindowClassRegistry.references==2,"multiple editors reuse the same DLL class");
        peer.reset();
        check(eqWindowClassRegistry.references==1 && GetClassInfoW(e->module,eqWindowClassName,&classInfo),"closing one editor preserves remaining editor callback");
        auto failed=std::make_unique<EqWindowsEditor>();
        check(!failed->attach(reinterpret_cast<HWND>(static_cast<INT_PTR>(-1)),f->services()),"failed additional editor does not create a native window");
        check(eqWindowClassRegistry.references==1 && GetClassInfoW(e->module,eqWindowClassName,&classInfo),"failed additional editor does not unregister a live class");
    }
    check(e->model.analyzer.settings.rangeDb==120,"analyzer defaults to 120 dB");check(e->model.responseRate()==96000,"curves use actual published sample rate");check(e->graph.x==40 && e->graph.y==20 && e->graph.w==908,"logical graph agrees with Mac geometry");
    check(e->acceptsPanel() && !e->compact,"full safe selected card visible");check(e->panel.w==500 && e->panel.h==132,"full card preserves exact layout");
    auto initial=f->sound.targets;unsigned writes=f->writes;e->sync();e->resize(760,436);e->resize(1000,596);check(f->writes==writes && f->sound.targets==initial,"refresh and layout never write audio parameters");
    printPixels(*e,false);
    // The exact original survives a focus/Return round trip; Escape cancels edits.
    e->startText(0);check(e->valueEdit!=nullptr,"native exact text field created");e->finishText(true);check(f->writes==writes && f->sound.targets==initial,"unchanged text preserves precise state");
    e->startText(0);SetWindowTextW(e->valueEdit,L"2000");e->finishText(false);check(f->writes==writes,"cancelled text is not written");
    e->startText(0);SetWindowTextW(e->valueEdit,L"2000 Hz");e->finishText(true);check(std::abs(e->model.value(frequency)-2000)<1e-7 && f->held.empty(),"committed text uses parsed balanced real parameter gesture");
    // A captured drag has balanced frequency and gain edits on cancellation.
    auto n=e->node(0);e->leftDown({LONG(n.X),LONG(n.Y)},false);check(e->dragging && f->held.size()==2,"node captures frequency and gain together");e->move({LONG(n.X+12),LONG(n.Y-5)});e->cancel();check(f->held.empty() && !e->dragging,"cancel releases all captured node edits");
    // Re-select on overlap prioritizes the selected node, matching Mac.
    f->sound.targets[index(1,enabled)]=1;f->sound.targets[index(1,frequency)]=f->sound.targets[index(0,frequency)];f->sound.targets[index(1,gain)]=f->sound.targets[index(0,gain)];e->sync();n=e->node(0);check(e->hit({LONG(n.X),LONG(n.Y)})==0,"selected overlapping node wins hit test");
    e->showSelection();e->presentation.choose(PanelPresentation::Form::compact,e->now());e->layout();check(e->compact && e->panel.w==260 && e->panel.h==56,"manual compact card preserves dimensions");e->presentation.choose(PanelPresentation::Form::full,e->now());e->layout();check(!e->compact,"manual expand persists");
    e->presentation.select(e->now()-4000);e->layout();check(!e->acceptsPanel(),"expired card leaves input tree");e->pointer={LONG(e->panel.x+20),LONG(e->panel.y+20)};e->tick();check(!e->acceptsPanel(),"hover cannot revive expired card");e->showSelection();e->layout();
    // Bypass freezes measurement and presentation while edit callbacks stay live.
    f->view.visualsPaused=true;e->setVisualsPaused(true);auto at=e->now();auto reads=f->reads;auto seq=e->model.analyzer.spectrum.header.sequence;++f->sequence;e->sync();check(e->now()==at && f->reads==reads && e->model.analyzer.spectrum.header.sequence==seq,"bypass freezes clocks and measurement reads");printPixels(*e,true);
    check(e->model.beginSolo(),"solo lease available with connected bus layout");e->model.beginSoloSweep();e->model.sweepSolo(3000);check(e->model.renewSolo(),"solo lease still renews while display is paused");e->cancel();check(f->token==0 && f->held.empty(),"solo cancellation restores engine lease and closes sweep edit");
    f->view.visualsPaused=false;++f->view.visualResumeGeneration;e->setVisualsPaused(false);e->sync();check(f->reads>reads && e->model.analyzer.spectrum.header.sequence==f->sequence,"resume consumes fresh generation");
    f->view.advanced=true;e->refresh(f->view,{});check(e->model.advanced && e->documentHeight>e->height && e->advancedControls.size()==7,"Advanced exposes existing seven exact parameter controls");
    f->view.renderScale=1.25;e->resize(1000,596);RECT physical{};GetWindowRect(e->window,&physical);check(physical.right-physical.left==1250 && physical.bottom-physical.top==745,"host renderScale scales HWND once");check(e->graph.w==908 && e->panel.w==500,"scaling does not shrink logical graph/card");
    f->view.language=UiLanguage::chinese;e->refresh(f->view,{});check(e->word("频率","Frequency")==L"频率","Chinese UI chooses translated labels");
    const HMODULE editorModule=e->module;
    e.reset();check(f->held.empty() && f->begins==f->ends,"destruction balances all real edit gestures");
    check(eqWindowClassRegistry.references==0 && !eqWindowClassRegistry.registered && !GetClassInfoW(editorModule,eqWindowClassName,&classInfo),"last editor unregisters callback after destroying its HWND");
    {
        auto reopened=std::make_unique<EqWindowsEditor>();
        check(reopened->attach(parent,f->services()),"class can register again for a later editor lifetime");
        reopened.reset();
        check(eqWindowClassRegistry.references==0 && !GetClassInfoW(editorModule,eqWindowClassName,&classInfo),"repeated attach and close leaves no registered EQ callback");
    }
    RemovePropW(parent,win::viewProperty);DestroyWindow(parent);
    std::cout<<"PASS "<<checks<<" Windows hidden-HWND adapter checks; no REAPER, listening, real-input or visual-review acceptance\n";
}
