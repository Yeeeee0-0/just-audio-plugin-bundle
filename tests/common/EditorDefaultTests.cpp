// Actual controller state lifecycle; no native view attachment or application.
#include "common/ui/Editor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"
#include <iostream>
#define moduleDefinition defaultFixtureOriginalDefinition
#include "UiEffectTestModule.cpp"
#undef moduleDefinition
namespace just {
ModuleDefinition& defaultFixture(){static auto m=defaultFixtureOriginalDefinition();return m;}
const ModuleDefinition& moduleDefinition(){return defaultFixture();}
}
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
static auto bytes(just::Controller& c){MemoryStream s;check(c.getState(&s)==kResultOk,"serialize UI state");return std::vector<char>(s.getData(),s.getData()+s.getSize());}
int main(){
    HostApplication host;auto& m=just::defaultFixture();
    check(m.valid() && m.defaultEditorWidth==0 && m.defaultEditorHeight==0,"zero defaults preserve existing module declaration");
    auto original=owned(new just::Controller);check(original->initialize(&host)==kResultOk,"initialize original defaults");
    check(original->viewState.width==just::pluginIdentities[0].width && original->viewState.height==just::pluginIdentities[0].height,"unopted controller retains exact identity size");original->terminate();
    m.minimumEditorWidth=760;m.minimumEditorHeight=560;
    auto minimum=owned(new just::Controller);check(minimum->viewState.width==760 && minimum->viewState.height==560,"unset defaults still use existing minimum clamp");
    m.defaultEditorWidth=1000;m.defaultEditorHeight=720;check(m.valid(),"independent defaults1000x720 and minimum760x560 accepted");
    for(int v:{-1,1,759,1921}){auto bad=m;bad.defaultEditorWidth=v;check(!bad.valid(),"invalid preferred width rejected");}
    for(int v:{-1,1,559,1201}){auto bad=m;bad.defaultEditorHeight=v;check(!bad.valid(),"invalid preferred height rejected");}
    m.defaultEditorHeight=0;auto widthOnly=owned(new just::Controller);check(widthOnly->viewState.width==1000 && widthOnly->viewState.height==560,"zero preferred dimension falls back independently");m.defaultEditorHeight=720;
    auto c=owned(new just::Controller);check(c->initialize(&host)==kResultOk,"initialize opted-in controller");
    check(c->viewState.width==1000 && c->viewState.height==720 && c->viewState.renderScale==1,"fresh controller starts at preferred logical size100 percent");
    c->setParamNormalized(1,.37123456789);const double target=c->getParamNormalized(1);
    MemoryStream legacy;IBStreamer s(&legacy,kLittleEndian);s.writeInt32(1);s.writeInt32(800);s.writeInt32(600);s.writeDouble(2);s.writeBool(true);legacy.seek(0,IBStream::kIBSeekSet,nullptr);
    check(c->setState(&legacy)==kResultOk && c->viewState.width==800 && c->viewState.height==600 && c->viewState.scale==2 && c->viewState.renderScale==1 && c->viewState.advanced,"legacy restored size wins over preferred default and retains metadata");
    auto view=owned(new just::Editor(c));ViewRect size;view->getSize(&size);check(size.getWidth()==800 && size.getHeight()==600,"editor construction does not reapply default");view=nullptr;
    MemoryStream small;IBStreamer t(&small,kLittleEndian);t.writeInt32(1);t.writeInt32(480);t.writeInt32(260);t.writeDouble(1);t.writeBool(false);small.seek(0,IBStream::kIBSeekSet,nullptr);
    check(c->setState(&small)==kResultOk && c->viewState.width==760 && c->viewState.height==560,"old small state clamps to minimum, not preferred size");
    c->viewState.width=600;c->viewState.height=450;c->viewState.renderScale=.75;c->viewState.scale=.75;c->viewState.language=just::UiLanguage::english;c->viewState.reduceMotion=true;
    MemoryStream v2;check(c->getState(&v2)==kResultOk,"save existing UI schema2");v2.seek(0,IBStream::kIBSeekSet,nullptr);
    auto restored=owned(new just::Controller);check(restored->initialize(&host)==kResultOk && restored->setState(&v2)==kResultOk,"restore schema2 into fresh preferred-size controller");
    check(bytes(*restored)==bytes(*c) && restored->viewState.width==600 && restored->viewState.height==450,"scaled saved size and all preferences win over preferred default");
    const auto before=bytes(*restored);MemoryStream truncated(v2.getData(),v2.getSize()-1);
    check(restored->setState(&truncated)==kResultFalse && bytes(*restored)==before,"malformed UI state remains transactional");
    check(c->getParamNormalized(1)==target,"default and UI restore never write sound target");
    restored->terminate();c->terminate();std::cout<<"PASS "<<checks<<" optional fresh/default/legacy/restored-size checks; no native attachment\n";
}
