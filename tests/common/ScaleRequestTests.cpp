// No native view is attached, no application/window is created, no OS input.
#include "common/ui/Editor.hpp"
#include "common/vst3/Processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"
#include <iostream>
#define moduleDefinition scaleFixtureOriginalDefinition
#include "UiEffectTestModule.cpp"
#undef moduleDefinition
namespace just {
const ModuleDefinition& moduleDefinition(){
    static const auto sized=[](){auto m=scaleFixtureOriginalDefinition();m.minimumEditorWidth=480;m.minimumEditorHeight=404;return m;}();return sized;
}
}
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
static bool sameRect(const ViewRect& a,const ViewRect& b){return a.left==b.left && a.right==b.right && a.top==b.top && a.bottom==b.bottom;}
template<class T> static std::vector<std::uint8_t> bytes(T& object){MemoryStream s;check(object.getState(&s)==kResultOk,"serialize state");auto* b=reinterpret_cast<const std::uint8_t*>(s.getData());return {b,b+s.getSize()};}
struct Frame final:IPlugFrame {
    bool accepts=false,callbackBeforeReject=false;int requests=0;ViewRect last{},old{};
    tresult PLUGIN_API queryInterface(const TUID,void** object) override{*object=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}uint32 PLUGIN_API release() override{return 1;}
    tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* size) override{
        ++requests;last=*size;view->getSize(&old);auto checked=*size;
        check(size->getWidth()>=480 && size->getHeight()>=260,"host never receives a below-hard-minimum request");
        check(view->checkSizeConstraint(&checked)==kResultTrue && sameRect(checked,*size),"host receives an already legal, idempotent proposal");
        std::cout<<"HOST request="<<last.getWidth()<<"x"<<last.getHeight()<<" old="<<old.getWidth()<<"x"<<old.getHeight()<<" accepts="<<accepts<<"\n";
        if(accepts || callbackBeforeReject)check(view->onSize(size)==kResultOk,"synchronous host onSize accepts legal constrained proposal");
        return accepts?kResultTrue:kResultFalse;
    }
};
int main(){
    HostApplication host;auto c=owned(new just::Controller);auto p=owned(new just::Processor);
    check(c->initialize(&host)==kResultOk && p->initialize(&host)==kResultOk,"initialize actual common controller/processor");
    auto sound=just::initialState(just::pluginIdentities[0].processor,just::moduleDefinition().parameters);
    sound.targets[1]=.37123456789;sound.seed=0x123456789abcdefULL;sound.configurationCount=1;sound.configurations[0]={1,.75};
    auto encoded=just::encodeState(sound,just::moduleDefinition().parameters);MemoryStream state(encoded.data(),encoded.size());
    check(p->setState(&state)==kResultOk,"restore complete targets/seed/config sound state");state.seek(0,IBStream::kIBSeekSet,nullptr);
    check(c->setComponentState(&state)==kResultOk,"restore controller targets");const auto originalSound=bytes(*p);const double target=c->getParamNormalized(1);
    MemoryStream legacy;IBStreamer stream(&legacy,kLittleEndian);stream.writeInt32(1);stream.writeInt32(480);stream.writeInt32(404);stream.writeDouble(2);stream.writeBool(true);legacy.seek(0,IBStream::kIBSeekSet,nullptr);
    check(c->setState(&legacy)==kResultOk && c->viewState.scale==2 && c->viewState.renderScale==1,"legacy scale2 remains metadata at minimum size");
    c->viewState.language=just::UiLanguage::english;c->viewState.backgroundEnabled=true;c->viewState.reduceMotion=true;c->viewState.lowPerformance=true;c->viewState.backgroundFps=15;
    auto view=owned(new just::Editor(c));Frame frame;view->setFrame(&frame);ViewRect initial;view->getSize(&initial);
    check(initial.getWidth()==480 && initial.getHeight()==404,"test starts at exact 480x404 minimum");
    auto rejected=[&](double scale,int width,int height,bool callback){
        frame.accepts=false;frame.callbackBeforeReject=callback;auto before=bytes(*c);ViewRect prior;view->getSize(&prior);const int requests=frame.requests;
        check(!view->requestScale(scale),"host rejection returns failure");
        check(frame.requests==requests+1 && frame.last.getWidth()==width && frame.last.getHeight()==height,"constrained request reaches rejecting host exactly once");
        ViewRect after;view->getSize(&after);check(sameRect(prior,after) && bytes(*c)==before,"rejection fully restores rectangle and every serialized UI field");
        check(bytes(*p)==originalSound && c->getParamNormalized(1)==target,"rejection preserves full sound state and target");frame.callbackBeforeReject=false;
    };
    rejected(.75,480,303,false);rejected(.75,480,303,true);
    auto accepted=[&](double scale,int width,int height){
        frame.accepts=true;const int requests=frame.requests;ViewRect before;view->getSize(&before);
        check(view->requestScale(scale),"host accepts constrained scale request");ViewRect after;view->getSize(&after);
        check(frame.requests==requests+1 && frame.last.getWidth()==width && frame.last.getHeight()==height && sameRect(frame.old,before),"one host request retains the old size until onSize");
        check(after.getWidth()==width && after.getHeight()==height && c->viewState.width==width && c->viewState.height==height && c->viewState.renderScale==scale && c->viewState.scale==scale,"accepted dimensions and scales commit together");
        check(bytes(*p)==originalSound && c->getParamNormalized(1)==target,"accepted scaling preserves full sound state and target");
    };
    accepted(.75,480,303);rejected(1,640,404,false);rejected(1,640,404,true);
    // A width clamped to 480 at 75% represents 640 logical points at 100%.
    // Preserve that established proportional policy; repeated toggles must not drift.
    accepted(1,640,404);accepted(.75,480,303);accepted(1,640,404);accepted(.75,480,303);
    auto restored=owned(new just::Controller);check(restored->initialize(&host)==kResultOk,"initialize restore controller");MemoryStream saved;check(c->getState(&saved)==kResultOk,"save existing v2 UI schema");saved.seek(0,IBStream::kIBSeekSet,nullptr);
    check(restored->setState(&saved)==kResultOk && bytes(*restored)==bytes(*c),"75 percent constrained dimensions and all UI preferences round trip unchanged");
    const int requests=frame.requests;const auto prior=bytes(*c);
    for(double invalid:{.5,2.,std::numeric_limits<double>::quiet_NaN()})check(!view->requestScale(invalid),"invalid scale rejected before contacting host");
    check(frame.requests==requests && bytes(*c)==prior,"preflight failures make no request and preserve UI state");
    view->setFrame(nullptr);check(!view->requestScale(1) && bytes(*c)==prior,"missing host frame preserves state");
    view=nullptr;restored->terminate();c->terminate();p->terminate();
    std::cout<<"PASS "<<checks<<" windowless scale/minimum/host acceptance/rejection/legacy/full-state checks\n";
}
