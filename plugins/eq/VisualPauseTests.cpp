#include "EditorModel.hpp"
#include "PanelPresentation.hpp"
#include "common/ui/PresentationClock.hpp"
#include <cstdlib>
#include <iostream>
using namespace just;
using namespace just::eq;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static bool near(double a,double b){return std::abs(a-b)<1e-12;}

// Deterministic UI-service fixtures only: no processor, audio claim, window,
// native input, filesystem preset or user project is involved in these tests.
struct Fixture {
    EditorViewState view;
    SoundState sound=initialState({},registry);
    unsigned begins=0,ends=0,writes=0,spectra=0,windows=0,layouts=0,statusReads=0,renewals=0,auditionEnds=0;
    bool available=true;
    std::uint64_t sequence=1;
    float amplitude=.25f;
    double sampleRate=48000;
    AuditionPhase phase=AuditionPhase::active;
    Fixture(){sound.targets[index(0,enabled)]=1;sound.targets[index(0,frequency)]=parameters[index(0,frequency)].toNormalized(1034.56789);sound.targets[index(0,gain)]=parameters[index(0,gain)].toNormalized(-4.12345);}
    EditorServices services(){
        EditorServices s;s.owner=this;s.view=&view;
        s.readTarget=[](void* p,ParamID id){return static_cast<Fixture*>(p)->sound.targets[registry.index(id)];};
        s.beginEdit=[](void* p,ParamID){++static_cast<Fixture*>(p)->begins;return true;};
        s.performEdit=[](void* p,ParamID id,double value){auto& f=*static_cast<Fixture*>(p);++f.writes;f.sound.targets[registry.index(id)]=value;return true;};
        s.endEdit=[](void* p,ParamID){++static_cast<Fixture*>(p)->ends;};
        s.readBusLayout=[](void* p,BusLayoutSnapshot& out){auto& f=*static_cast<Fixture*>(p);++f.layouts;out={};out.validFields=layoutBuses|layoutSampleRate;out.active=true;out.sampleRate=f.sampleRate;return true;};
        s.readExtendedSpectrum=[](void* p,ExtendedSpectrumSnapshot& out){auto& f=*static_cast<Fixture*>(p);++f.spectra;if(!f.available)return AnalysisAvailability::unavailable;out={};out.fftSize=4096;out.binCount=2049;out.hopSize=1024;out.hasEnvelope=true;out.configurationGeneration=1;out.presentationGeneration=f.view.visualResumeGeneration;out.header={1,1,f.sequence,f.sequence*4096,(f.sequence+1)*4096,0,f.sampleRate,2,2,0,analysisInputAligned,analysisNow()};out.amplitude[0][32]=f.amplitude;for(auto& values:out.envelopeDb)values.fill(-180);out.envelopeDb[0][32]=20*std::log10(f.amplitude);return AnalysisAvailability::fresh;};
        s.readAnalysis=[](void* p,AnalysisCursor& cursor,AnalysisBatch& out){auto& f=*static_cast<Fixture*>(p);++f.windows;out={};if(!f.available)return AnalysisAvailability::unavailable;if(cursor.sequence==f.sequence)return AnalysisAvailability::stale;cursor={1,1,f.sequence};out.count=1;out.windows[0].header.sequence=f.sequence;out.windows[0].channels[0].peak=f.amplitude;return AnalysisAvailability::fresh;};
        s.beginAudition=[](void*,ParamID){return std::uint64_t(42);};
        s.renewAudition=[](void* p,std::uint64_t token){++static_cast<Fixture*>(p)->renewals;return token==42;};
        s.endAudition=[](void* p,std::uint64_t){++static_cast<Fixture*>(p)->auditionEnds;};
        s.readAuditionStatus=[](void* p){auto& f=*static_cast<Fixture*>(p);++f.statusReads;return AuditionStatus{42,id(0,frequency),f.phase};};
        return s;
    }
};
int main(){
    using Panel=PanelPresentation;
    {
        PresentationClock clock;Panel panel;panel.select(clock.now(1000));
        clock.setPaused(true,2500);clock.setPaused(true,3000);
        check(panel.phase(clock.now(100000))==Panel::Phase::visible,"bypass during idle freezes remaining deadline, repeated pause is harmless");
        clock.setPaused(false,100000);
        check(panel.phase(clock.now(101499))==Panel::Phase::visible && panel.phase(clock.now(101500))==Panel::Phase::fading,"resume preserves exactly the remaining 1500ms idle");
        check(panel.phase(clock.now(102150))==Panel::Phase::hidden,"unchanged 650ms fade follows remaining idle");
        clock.setPaused(true,102200);clock.setPaused(false,202200);
        check(!panel.acceptsInput(clock.now(202200)),"pause/resume cannot wake an already hidden card");
    }
    {
        PresentationClock clock;Panel panel;panel.select(clock.now(0));
        clock.setPaused(true,3325);const double alpha=panel.fade(clock.now(3325));
        check(near(alpha,.5) && near(panel.fade(clock.now(80000)),alpha),"mid-fade alpha remains fixed throughout bypass");
        clock.setPaused(false,80000);
        check(near(panel.fade(clock.now(80000)),.5) && panel.phase(clock.now(80324))==Panel::Phase::fading && !panel.acceptsInput(clock.now(80325)),"resume completes only the remaining 325ms, no elapsed-time catch-up");
    }
    for(auto activity:{Panel::Activity::nodeDrag,Panel::Activity::knobDrag,Panel::Activity::textFocus,Panel::Activity::solo,Panel::Activity::menu}){
        PresentationClock clock;Panel panel;panel.select(clock.now(0));panel.activity(activity,true,clock.now(100));
        clock.setPaused(true,200);panel.activity(activity,false,clock.now(5000));
        check(!panel.active(activity) && panel.acceptsInput(clock.now(8000)),"interaction can finish while paused without leaving a stuck hold");
        clock.setPaused(false,9000);
        check(panel.phase(clock.now(11999))==Panel::Phase::visible && panel.phase(clock.now(12000))==Panel::Phase::fading,"interaction ending during bypass restarts full idle when display time resumes");
    }
    {
        Fixture f;EditorModel model;model.connect(f.services());
        check(model.hasWindow && model.analyzer.spectrum.header.sequence==1 && model.latestWindow.header.sequence==1,"initial measurement fixture populates display");
        const auto initial=f.sound.targets;const auto spectrum=model.analyzer.spectrum;const auto history=model.latestWindow;const auto availability=model.historyAvailability;
        const unsigned spectra=f.spectra,windows=f.windows,layouts=f.layouts,statusReads=f.statusReads;
        f.view.visualsPaused=true;f.available=false;f.sampleRate=96000;
        model.refresh();model.refresh();
        check(f.spectra==spectra && f.windows==windows,"paused model does not consume spectrum or history reads");
        check(model.analyzer.spectrum.amplitude==spectrum.amplitude && model.analyzer.spectrum.header.sequence==spectrum.header.sequence && model.latestWindow.header.sequence==history.header.sequence && model.historyAvailability==availability && model.hasWindow,"unavailable shared reads cannot erase the frozen display");
        check(f.layouts==layouts+2 && f.statusReads==statusReads+2 && model.responseRate()==96000,"layout and audition safety keep refreshing while visuals pause");
        check(f.sound.targets==initial && !f.writes && !f.begins && !f.ends,"visual pause preserves all195 targets including existing103/104 values");
        f.sound.targets[index(0,frequency)]=parameters[index(0,frequency)].toNormalized(2222.125);
        f.sound.targets[index(0,gain)]=parameters[index(0,gain)].toNormalized(-6.75);
        f.view.advanced=true;model.refresh();
        check(std::abs(model.value(frequency)-2222.125)<1e-9 && near(model.value(gain),-6.75) && model.advanced,"host targets and view state continue syncing while bypassed");
        model.beginDrag(0);const auto began=f.begins;model.refresh();
        check(began==2 && !f.ends,"paused refresh never cancels a held frequency/gain gesture");
        model.drag(1777.125,-3.5);model.cancelDrag();
        check(f.begins==f.ends && f.writes==2 && std::abs(model.value(frequency)-1777.125)<1e-9,"parameter drag still writes and balances both existing gestures while paused");
        const auto finalSound=f.sound.targets;const auto edits=f.writes;
        f.view.visualsPaused=false;++f.view.visualResumeGeneration;model.refresh();
        check(!model.hasWindow && model.analysisCursor.sequence==0 && model.latestWindow.header.sequence==0 && model.analyzer.spectrum.header.sequence==0 && model.analyzer.spectrum.amplitude[0][32]==0,"resume generation clears old measured display and cursor before unavailable reads");
        check(model.analyzer.availability==AnalysisAvailability::unavailable && model.historyAvailability==AnalysisAvailability::unavailable,"resume waits for real fresh measurements instead of reviving pre-bypass data");
        f.available=true;f.sequence=20;f.amplitude=.75f;model.refresh();
        check(model.analyzer.spectrum.header.sequence==20 && model.latestWindow.header.sequence==20 && model.hasWindow && model.analyzer.spectrum.amplitude[0][32]==.75f,"new-generation available measurement replaces cleared cache");
        const auto cursor=model.analysisCursor.sequence;model.refresh();
        check(model.analysisCursor.sequence==cursor && model.hasWindow,"ordinary refresh in the same generation does not repeatedly clear history");
        check(f.sound.targets==finalSound && f.writes==edits && f.begins==f.ends,"resume and history reset never edit sound or create gestures");
    }
    {
        Fixture f;f.view.visualsPaused=true;f.view.visualResumeGeneration=8;EditorModel model;model.connect(f.services());
        check(!f.spectra && !f.windows && !model.hasWindow,"initially bypassed editor does not consume measurements during attach refresh");
        check(model.beginSolo(),"existing audition can start with measurement display paused");model.beginSoloSweep();model.refresh();
        check(model.soloToken==42 && model.renewSolo() && f.renewals==1 && !f.auditionEnds && !f.ends,"audition renewal and held sweep remain live while paused");
        model.sweepSolo(2500);check(f.writes==1 && model.soloToken==42,"live audition frequency gesture is not a presentation clock");
        f.phase=AuditionPhase::ended;model.refresh();
        check(!model.soloToken && f.auditionEnds==1 && f.begins==f.ends,"ended lease still releases audition and gesture during bypass");
        f.view.visualsPaused=false;++f.view.visualResumeGeneration;f.sequence=30;model.refresh();
        check(model.hasWindow && model.analyzer.spectrum.header.sequence==30,"initially paused editor reads current data on first resume");
    }
    std::cout<<"PASS "<<checks<<" visual pause clock/model checks; service fixtures, no GUI/input/audio acceptance\n";
}
