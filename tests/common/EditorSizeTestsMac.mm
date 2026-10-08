#import <Cocoa/Cocoa.h>
#include "common/vst3/Processor.hpp"
#include "common/vst3/Controller.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "common/ui/ObjCNames.hpp"
#include "common/ui/Controls.hpp"
#include <iostream>
using namespace Steinberg;using namespace Steinberg::Vst;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
static std::vector<std::uint8_t> componentState(just::Processor& p){MemoryStream m;check(p.getState(&m)==kResultOk,"save complete sound state");auto* b=reinterpret_cast<std::uint8_t*>(m.getData());return {b,b+m.getSize()};}
static std::vector<std::uint8_t> editorState(just::Controller& c){MemoryStream m;check(c.getState(&m)==kResultOk,"save UI-only state");auto* b=reinterpret_cast<std::uint8_t*>(m.getData());return {b,b+m.getSize()};}
static tresult restoreUi(just::Controller& c,int width,int height,double scale=1,bool advanced=false,int version=1){
    MemoryStream m;IBStreamer s(&m,kLittleEndian);s.writeInt32(version);s.writeInt32(width);s.writeInt32(height);s.writeDouble(scale);s.writeBool(advanced);m.seek(0,IBStream::kIBSeekSet,nullptr);return c.setState(&m);
}
static bool sameRect(const ViewRect& a,const ViewRect& b){return a.left==b.left && a.right==b.right && a.top==b.top && a.bottom==b.bottom;}
static NSView* findIdentifier(NSView* parent,const char* name){for(NSView* v in parent.subviews){if([v.identifier isEqualToString:[NSString stringWithUTF8String:name]])return v;if(auto* nested=findIdentifier(v,name))return nested;}return nil;}
static void visibleContent(NSView* parent){
    auto* plot=findIdentifier(parent,just::analysisViewIdentifier);auto* rotary=findIdentifier(parent,just::rotaryViewIdentifier);
    check(plot && rotary && !plot.isHidden && !rotary.isHidden,"real plot and primary rotary exist at minimum");
    std::cout<<"LAYOUT content="<<rotary.superview.bounds.size.width<<"x"<<rotary.superview.bounds.size.height
             <<" plotHeight="<<plot.frame.size.height<<" rotaryY="<<rotary.frame.origin.y
             <<" rotary="<<rotary.frame.size.width<<"x"<<rotary.frame.size.height
             <<" rotaryBottom="<<NSMaxY(rotary.frame)<<"\n";
    check(rotary.frame.size.width==just::ControlGeometry::cellWidth && rotary.frame.size.height==just::ControlGeometry::cellHeight,"fixture uses the full shared default control geometry");
    check(plot.frame.size.height>=60 && NSContainsRect(plot.superview.bounds,plot.frame) && NSContainsRect(rotary.superview.bounds,rotary.frame),"plot and full default control fit content bounds");
    check(NSMaxY(plot.frame)==NSMinY(rotary.frame),"plot and control allocation meet without overlap");
}
static void declarationTests(){
    auto m=just::moduleDefinition();m.minimumEditorWidth=m.minimumEditorHeight=0;auto legacy=m.editorSizeLimits();check(m.valid() && legacy.minimumWidth==480 && legacy.minimumHeight==260,"omitted fields preserve legacy floor");
    m.minimumEditorWidth=520;m.minimumEditorHeight=340;check(m.valid() && m.editorSizeLimits().minimumWidth==520 && m.editorSizeLimits().minimumHeight==340,"module 520x340 declaration accepted exactly");
    int width=480,height=260;m.editorSizeLimits().constrain(width,height);check(width==520 && height==340,"Reverb/Delay old floor raises to declared minimum");
    m.minimumEditorWidth=720;m.minimumEditorHeight=420;width=640;height=300;m.editorSizeLimits().constrain(width,height);check(m.valid() && width==720 && height==420,"Tremolo old 640x300 preference raises to declared 720x420 floor");m.minimumEditorWidth=520;
    m.minimumEditorHeight=0;check(m.valid() && m.editorSizeLimits().minimumHeight==260,"each optional dimension defaults independently");
    for(int bad:{-1,1,479,1921,std::numeric_limits<int>::max()}){m.minimumEditorWidth=bad;check(!m.valid(),"illegal width declaration rejected");}
    m.minimumEditorWidth=520;for(int bad:{-1,1,259,1201,std::numeric_limits<int>::max()}){m.minimumEditorHeight=bad;check(!m.valid(),"illegal height declaration rejected");}
    m.minimumEditorWidth=1920;m.minimumEditorHeight=1200;check(m.valid(),"maximum bounded declaration accepted");
}
int main(){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];declarationTests();
    HostApplication host;auto p=owned(new just::Processor);auto twin=owned(new just::Processor);auto c=owned(new just::Controller);
    check(c->viewState.width==900 && c->viewState.height==620,"constructor raises initial identity size to module minimum");
    check(p->initialize(&host)==kResultOk && twin->initialize(&host)==kResultOk && c->initialize(&host)==kResultOk,"actual sized fixture initialize");p->connect(c);c->connect(p);
    auto desired=just::initialState(just::pluginIdentities[0].processor,just::moduleDefinition().parameters);desired.seed=0x123456789abcdefull;desired.configurationCount=1;desired.configurations[0]={1,.75};
    auto sound=just::encodeState(desired,just::moduleDefinition().parameters);
    auto restore=[&](auto& processor){MemoryStream m;int32 written=0;m.write(sound.data(),sound.size(),&written);m.seek(0,IBStream::kIBSeekSet,nullptr);check(processor.setState(&m)==kResultOk,"same complete hidden config and seed restore");};restore(*p);restore(*twin);
    ProcessSetup setup{kRealtime,kSample64,64,48000};check(p->setupProcessing(setup)==kResultOk && twin->setupProcessing(setup)==kResultOk && p->setActive(true)==kResultOk && twin->setActive(true)==kResultOk,"prepare identical effect twins");
    std::array<double,64> left{},right{},outL{},outR{},refL{},refR{};double* ins[]={left.data(),right.data()},*outs[]={outL.data(),outR.data()},*refs[]={refL.data(),refR.data()};AudioBusBuffers input{},output{},refOutput{};input.numChannels=output.numChannels=refOutput.numChannels=2;input.channelBuffers64=ins;output.channelBuffers64=outs;refOutput.channelBuffers64=refs;
    ProcessData d{};d.numSamples=64;d.symbolicSampleSize=kSample64;d.numInputs=d.numOutputs=1;d.inputs=&input;d.outputs=&output;unsigned block=0;
    auto render=[&](){for(unsigned i=0;i<64;++i){left[i]=.5*std::sin((block*64+i)*.071);right[i]=-.4*std::cos((block*64+i)*.113);}auto reference=d;reference.outputs=&refOutput;check(p->process(d)==kResultOk && twin->process(reference)==kResultOk && outL==refL && outR==refR,"resize/open/restore keeps reference effect audio bit-exact");++block;};render();const auto before=componentState(*p);
    check(restoreUi(*c,480,260,2,true)==kResultOk && c->viewState.width==900 && c->viewState.height==620 && c->viewState.scale==2 && c->viewState.advanced,"old legal UI dimensions raised; metadata scale never doubles native size");
    const auto uiBefore=editorState(*c);
    for(auto size:{std::pair<int,int>{0,260},{479,260},{480,259},{-1,340},{1921,340},{520,1201},{std::numeric_limits<int>::max(),340}}){check(restoreUi(*c,size.first,size.second)==kResultFalse && editorState(*c)==uiBefore,"illegal or excessive UI restore rejected transactionally");}
    for(double scale:{0.,.74,2.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})check(restoreUi(*c,520,340,scale)==kResultFalse && editorState(*c)==uiBefore,"nonfinite/out-of-range scale rejects without mutation");
    check(restoreUi(*c,520,340,1,false,2)==kResultFalse && editorState(*c)==uiBefore,"UI schema unchanged and wrong version rejected");MemoryStream truncated;check(c->setState(&truncated)==kResultFalse && editorState(*c)==uiBefore,"truncated UI chunk rejected");
    auto view=owned(c->createView(ViewType::kEditor));ViewRect initial;check(view && view->getSize(&initial)==kResultOk && initial.getWidth()==900 && initial.getHeight()==620,"first host size uses declared floor after old UI restore");
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,2000,1300)];const auto parentFrame=parent.frame;
    check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"actual native sized editor attaches");visibleContent(parent);
    for(auto proposed:{ViewRect(0,0,480,260),ViewRect(8,12,528,352),ViewRect(0,0,1921,1201),ViewRect(0,0,900,620),ViewRect(0,0,0,0),ViewRect(std::numeric_limits<int32>::min(),std::numeric_limits<int32>::min(),std::numeric_limits<int32>::max(),std::numeric_limits<int32>::max())}){
        auto constrained=proposed;check(view->checkSizeConstraint(&constrained)==kResultTrue && constrained.left==proposed.left && constrained.top==proposed.top && constrained.getWidth()>=900 && constrained.getWidth()<=1920 && constrained.getHeight()>=620 && constrained.getHeight()<=1200,"native constraint bounded without signed subtraction overflow");
        auto repeat=constrained;view->checkSizeConstraint(&repeat);check(sameRect(repeat,constrained),"constraint idempotent in native units");check(view->onSize(&proposed)==kResultOk && sameRect(proposed,constrained),"onSize enforces same floor even if host skipped precheck");
        ViewRect actual;view->getSize(&actual);check(sameRect(actual,constrained) && c->viewState.width==actual.getWidth() && c->viewState.height==actual.getHeight(),"native and saved UI size agree");visibleContent(parent);render();check(componentState(*p)==before && componentState(*twin)==before && NSEqualRects(parent.frame,parentFrame),"resize leaves complete sound state and host parent unchanged");
    }
    for(auto invalid:{ViewRect(1,0,0,10),ViewRect(0,1,10,0),ViewRect(std::numeric_limits<int32>::max()-10,0,std::numeric_limits<int32>::max(),10)}){
        const auto original=invalid;ViewRect prior;view->getSize(&prior);const auto ui=editorState(*c);check(view->checkSizeConstraint(&invalid)==kInvalidArgument && sameRect(invalid,original) && view->onSize(&invalid)==kInvalidArgument,"inverted/overflowing rectangle rejected without mutation");ViewRect after;view->getSize(&after);check(sameRect(prior,after) && editorState(*c)==ui,"bad resize cannot change live or saved dimensions");render();
    }
    check(view->checkSizeConstraint(nullptr)==kInvalidArgument && view->onSize(nullptr)==kInvalidArgument,"null host rectangles rejected");
    check(view->removed()==kResultOk,"native view closes");view=nullptr;render();
    for(double scale:{.75,1.,2.}){check(restoreUi(*c,520,340,scale)==kResultOk,"legal legacy scale retained without size multiplication");view=owned(c->createView(ViewType::kEditor));ViewRect size;view->getSize(&size);check(size.getWidth()==900 && size.getHeight()==620,"reopen at effective floor for every legacy scale");check(view->attached((__bridge void*)parent,kPlatformTypeNSView)==kResultOk,"native reopen");visibleContent(parent);render();check(view->removed()==kResultOk,"reopened view removed");view=nullptr;}
    check(componentState(*p)==before && componentState(*twin)==before && c->getParamNormalized(0)==0 && c->getParamNormalized(1)==.6,"all UI lifecycle operations preserve parameters, seed/config and sound bytes");
    p->setActive(false);twin->setActive(false);p->disconnect(c);c->disconnect(p);c->terminate();p->terminate();twin->terminate();std::cout<<"PASS "<<checks<<" module minimum/native resize/legacy UI restore/reference audio checks\n";
}}
