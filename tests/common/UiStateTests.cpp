#include "common/ui/Editor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using namespace Steinberg;using namespace Steinberg::Vst;
struct Frame final:IPlugFrame {
    bool accepts=false;int requests=0;
    tresult PLUGIN_API queryInterface(const TUID,void** object) override{*object=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}uint32 PLUGIN_API release() override{return 1;}
    tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* size) override{++requests;return accepts?view->onSize(size):kResultFalse;}
};
static unsigned checks=0;
static void require(bool ok,const char* text="contract assertion"){++checks;if(!ok){std::cerr<<"FAIL "<<text<<"\n";std::abort();}}
static std::vector<char> uiBytes(just::Controller& c){MemoryStream s;require(c.getState(&s)==kResultOk,"serialize UI preferences");return {s.getData(),s.getData()+s.getSize()};}
static void languagePreferences(HostApplication& host){
    static_assert(static_cast<int>(just::UiLanguage::chinese)==0 && static_cast<int>(just::UiLanguage::english)==1,"preserve saved language ordinals");
    const just::EditorViewState defaults{},aggregate{900,620,1,false};
    require(defaults.language==just::UiLanguage::english && aggregate.language==just::UiLanguage::english,"fresh and aggregate UI defaults are English");
    require(std::string(just::localized(defaults,"中文","English"))=="English","default localized text uses English");
    auto c=owned(new just::Controller);require(c->initialize(&host)==kResultOk,"initialize fresh language fixture");
    require(c->viewState.language==just::UiLanguage::english,"fresh controller without stored state starts in English");
    MemoryStream empty;
    require(c->setState(nullptr)==kInvalidArgument && c->setState(&empty)==kResultFalse && c->viewState.language==just::UiLanguage::english,"absent state retains fresh English fallback");
    c->setParamNormalized(1,.37123456789);
    std::vector<std::pair<ParamID,double>> targets;
    for(int32 i=0;i<c->getParameterCount();++i){ParameterInfo info{};require(c->getParameterInfo(i,info)==kResultOk);targets.emplace_back(info.id,c->getParamNormalized(info.id));}
    for(auto language:{just::UiLanguage::chinese,just::UiLanguage::english}){
        c->viewState.language=language;
        require(std::string(just::localized(c->viewState,"中文","English"))==(language==just::UiLanguage::chinese?"中文":"English"),"explicit language selection controls text");
        const auto saved=uiBytes(*c);MemoryStream state(const_cast<char*>(saved.data()),saved.size());
        auto restored=owned(new just::Controller);require(restored->initialize(&host)==kResultOk);
        require(restored->setState(&state)==kResultOk && restored->viewState.language==language && uiBytes(*restored)==saved,"saved Chinese and English survive exact UI state roundtrip");
        MemoryStream truncated(const_cast<char*>(saved.data()),saved.size()-1),missing;
        require(restored->setState(&truncated)==kResultFalse && restored->setState(&missing)==kResultFalse && uiBytes(*restored)==saved,"invalid or missing state never replaces an explicit preference");
        state.seek(0,IBStream::kIBSeekSet,nullptr);require(c->setState(&state)==kResultOk);
        for(const auto& target:targets)require(c->getParamNormalized(target.first)==target.second,"language restore leaves every sound target unchanged");
        restored->terminate();
    }
    c->terminate();
}
int main(){HostApplication host;languagePreferences(host);auto c=owned(new just::Controller);require(c->initialize(&host)==kResultOk);
    MemoryStream legacy;IBStreamer l(&legacy,kLittleEndian);l.writeInt32(1);l.writeInt32(900);l.writeInt32(620);l.writeDouble(2);l.writeBool(false);legacy.seek(0,IBStream::kIBSeekSet,nullptr);
    require(c->setState(&legacy)==kResultOk && c->viewState.scale==2 && c->viewState.renderScale==1 && !c->viewState.backgroundEnabled && c->viewState.language==just::UiLanguage::english,"legacy UI chunk without language falls back to English");
    c->viewState.language=just::UiLanguage::english;c->viewState.backgroundEnabled=true;c->viewState.reduceMotion=true;c->viewState.backgroundFps=15;
    MemoryStream v2;require(c->getState(&v2)==kResultOk);auto restored=owned(new just::Controller);restored->initialize(&host);v2.seek(0,IBStream::kIBSeekSet,nullptr);require(restored->setState(&v2)==kResultOk && restored->viewState.language==just::UiLanguage::english && restored->viewState.backgroundEnabled && restored->viewState.reduceMotion && restored->viewState.backgroundFps==15);
    c->viewState.visualsPaused=true;c->viewState.visualResumeGeneration=73;MemoryStream ephemeral;require(c->getState(&ephemeral)==kResultOk && ephemeral.getSize()==v2.getSize() && !std::memcmp(ephemeral.getData(),v2.getData(),v2.getSize()));
    require(!restored->viewState.visualsPaused && restored->viewState.visualResumeGeneration==0);
    v2.seek(0,IBStream::kIBSeekSet,nullptr);require(c->setState(&v2)==kResultOk && c->viewState.visualsPaused && c->viewState.visualResumeGeneration==73);
    const double target=c->getParamNormalized(1);auto view=owned(new just::Editor(c));Frame frame;view->setFrame(&frame);ViewRect before;view->getSize(&before);
    require(!view->requestScale(1.25) && frame.requests==1 && c->viewState.renderScale==1 && c->viewState.scale==2);ViewRect after;view->getSize(&after);require(after.getWidth()==before.getWidth() && after.getHeight()==before.getHeight());
    frame.accepts=true;require(view->requestScale(1.25) && c->viewState.renderScale==1.25 && c->viewState.scale==1.25);view->getSize(&after);require(after.getWidth()==1125 && after.getHeight()==775);
    require(view->requestScale(.75) && c->viewState.renderScale==.75);view->getSize(&after);require(after.getWidth()==675 && after.getHeight()==465);
    MemoryStream scaled;require(c->getState(&scaled)==kResultOk);scaled.seek(0,IBStream::kIBSeekSet,nullptr);require(restored->setState(&scaled)==kResultOk && restored->viewState.width==675 && restored->viewState.height==465 && restored->viewState.renderScale==.75);
    MemoryStream truncated(scaled.getData(),scaled.getSize()-1);require(restored->setState(&truncated)==kResultFalse && restored->viewState.renderScale==.75);
    require(!view->requestScale(2) && !view->requestScale(std::numeric_limits<double>::quiet_NaN()));require(c->getParamNormalized(1)==target);
    view->setFrame(nullptr);view=nullptr;restored->terminate();c->terminate();std::cout<<"PASS "<<checks<<" checks: fresh/legacy English, preserved Chinese/English preferences, v2 preferences/scale roundtrip, host refusal rollback, minimum scaled logical size, malformed transactional rejection, unchanged parameter targets; no native attachment\n";
}
