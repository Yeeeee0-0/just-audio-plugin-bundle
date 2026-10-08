#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <iostream>
#include <vector>
#include "Parameters.hpp"
#include "PanelPlacement.hpp"

// Targeted loaded-bundle geometry regression. All controls are invoked directly
// as fixture setup. No NSWindow, activation, OS event or physical input is used.
using namespace Steinberg;
using namespace Steinberg::Vst;
static unsigned checks=0;
static id forbidWindow(id cls,SEL,...){std::cerr<<"FAIL attempted NSWindow allocation "<<class_getName(cls)<<"\n";std::exit(2);}
static void require(bool ok,const char* label) {
    ++checks;if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}
}
static void noWindow() {
    require(NSApp.windows.count==0 && !NSApp.isActive,"no window or activation");
}
static NSView* identified(NSView* parent,NSString* identifier) {
    if([parent.identifier isEqualToString:identifier])return parent;
    for(NSView* child in parent.subviews)if(auto* match=identified(child,identifier))return match;
    return nil;
}
static NSButton* actionButton(NSView* parent,SEL action) {
    if([parent isKindOfClass:NSButton.class] && [(NSButton*)parent action]==action)return (NSButton*)parent;
    for(NSView* child in parent.subviews)if(auto* match=actionButton(child,action))return match;
    return nil;
}
static NSPopUpButton* scaleSelector(NSView* parent) {
    if([parent isKindOfClass:NSPopUpButton.class] && [(NSControl*)parent tag]==1 &&
       [(NSControl*)parent action]==@selector(settingChanged:))return (NSPopUpButton*)parent;
    for(NSView* child in parent.subviews)if(auto* match=scaleSelector(child))return match;
    return nil;
}
static std::vector<double> targets(IEditController* controller) {
    std::vector<double> values;
    for(int32 i=0;i<controller->getParameterCount();++i){ParameterInfo info{};
        require(controller->getParameterInfo(i,info)==kResultOk,"parameter metadata");
        values.push_back(controller->getParamNormalized(info.id));}
    return values;
}
class Handler final:public FObject,public IComponentHandler {
public:
    unsigned writes=0;
    tresult PLUGIN_API beginEdit(ParamID) override{return kResultOk;}
    tresult PLUGIN_API performEdit(ParamID,ParamValue) override{++writes;return kResultOk;}
    tresult PLUGIN_API endEdit(ParamID) override{return kResultOk;}
    tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
    OBJ_METHODS(Handler,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
class Frame final:public FObject,public IPlugFrame {
public:
    NSView* parent=nil;unsigned requests=0;bool reject=false;
    tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* rect) override {
        noWindow();++requests;
        if(!view || !rect || !parent)return kInvalidArgument;if(reject)return kResultFalse;
        parent.frame=NSMakeRect(0,0,rect->getWidth(),rect->getHeight());return view->onSize(rect);
    }
    OBJ_METHODS(Frame,FObject)
    DEFINE_INTERFACES DEF_INTERFACE(IPlugFrame) END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
static void legacyUiState(IEditController* controller,int version,double scale,bool advanced) {
    MemoryStream bytes;IBStreamer stream(&bytes,kLittleEndian);
    require(stream.writeInt32(version) && stream.writeInt32(480) && stream.writeInt32(260) &&
        stream.writeDouble(scale) && stream.writeBool(advanced),"old small UI header");
    if(version==2)require(stream.writeInt32(0) && stream.writeBool(true) && stream.writeBool(false) &&
        stream.writeBool(false) && stream.writeInt32(20) && stream.writeDouble(scale),"old v2 UI tail");
    bytes.seek(0,IBStream::kIBSeekSet,nullptr);
    require(controller->setState(&bytes)==kResultOk,"old small UI state restored");
}
static void settle(NSView* root) {
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.04]];
    [root layoutSubtreeIfNeeded];[root displayIfNeeded];noWindow();
}
static void verifyPanel(NSView* root,double scale,const std::string& path) {
    settle(root);auto* panel=identified(root,@"JustEQ.SelectedPanel");
    require(panel && !panel.hidden,"selected panel visible");
    require((panel.bounds.size.height==132 && panel.bounds.size.width==500) || (panel.bounds.size.height==56 && panel.bounds.size.width==260),"approved full or compact logical size");
    auto* canvas=identified(root,@"JustEQ.Canvas");
    auto* scroll=canvas.enclosingScrollView;
    require(scroll && NSContainsRect(scroll.contentView.documentVisibleRect,panel.frame),"entire panel in visible document rect");
    for(NSView* ancestor=panel.superview;ancestor;ancestor=ancestor.superview)
        require(NSContainsRect(NSInsetRect(ancestor.bounds,-.01,-.01),[panel convertRect:panel.bounds toView:ancestor]),"panel inside every ancestor clip");
    const auto physical=[panel convertRect:panel.bounds toView:root];
    require(std::abs(physical.size.height-panel.bounds.size.height*scale)<.02,"panel scales without changing readable logical geometry");
    for(NSView* child in panel.subviews)if(!child.hidden)
        require(NSContainsRect(panel.bounds,child.frame),"selected controls fully inside panel");
    if(panel.bounds.size.height==132){
        auto* rotary=identified(panel,@"JustEQ.Control.103");require(rotary,"real selected frequency control");
        bool number=false,label=false;
        for(NSView* child in rotary.subviews)if([child isKindOfClass:NSTextField.class]){auto* text=(NSTextField*)child;
            if(text.isEditable){number=true;require(text.font.pointSize==20 && text.frame.size.height==28,"real numeric20 readout28");require(text.cell.usesSingleLineMode && !text.cell.wraps && text.cell.scrollable,"single-line precise editor retained");}
            else if(text.font.pointSize==13)label=true;
        }require(number && label,"public rotary typography applied to real bundle");
    }
    auto* image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];
    [root cacheDisplayInRect:root.bounds toBitmapImageRep:image];
    require([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"capture actual bundle NSView");
    std::cout<<"CASE "<<path<<" outer="<<root.bounds.size.width<<"x"<<root.bounds.size.height
        <<" viewport="<<scroll.contentView.bounds.size.width<<"x"<<scroll.contentView.bounds.size.height
        <<" logicalPanel="<<NSStringFromRect(panel.frame).UTF8String<<" physicalPanel="<<NSStringFromRect(physical).UTF8String<<"\n";
    noWindow();
}
int main(int argc,char** argv) {@autoreleasepool {
    require(argc==3,"usage exact-EQ-bundle existing-evidence-directory");
    for(SEL selector:{@selector(alloc),@selector(allocWithZone:)}){auto method=class_getClassMethod(NSWindow.class,selector);class_replaceMethod(object_getClass(NSWindow.class),selector,(IMP)forbidWindow,method_getTypeEncoding(method));}
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];noWindow();
    std::string error;auto module=VST3::Hosting::Module::create(argv[1],error);require(bool(module),error.c_str());
    HostApplication host;auto classes=module->getFactory().classInfos();require(classes.size()==2,"actual EQ class pair");
    const std::string dest=argv[2];
    // The first case exercises the live common resize/75% route. Subsequent
    // cases exercise saved v1/v2 small dimensions before creating the editor.
    for(int scenario=0;scenario<4;++scenario) {
        auto controller=module->getFactory().createInstance<IEditController>(classes[1].ID());
        require(controller && controller->initialize(&host)==kResultOk,"actual EQ controller");
        auto handler=owned(new Handler);controller->setComponentHandler(handler);
        // Host target setup only. These are controlled fixtures, not audio or
        // native input acceptance and never the plugin's initial sound state.
        auto set=[&](just::eq::Field field,double physical){const auto& spec=just::eq::parameters[just::eq::index(0,field)];require(controller->setParamNormalized(spec.id,spec.toNormalized(physical))==kResultOk,"fixture target");};
        set(just::eq::enabled,1);set(just::eq::gain,3);
        const auto initial=targets(controller);
        if(scenario)legacyUiState(controller,scenario==1?1:2,scenario==2?1:.75,scenario==3);
        require(targets(controller)==initial && handler->writes==0,"UI restore preserves all sound parameters");
        auto view=owned(controller->createView(ViewType::kEditor));ViewRect size;
        require(view && view->getSize(&size)==kResultOk,"actual editor size");
        const int expectedWidth=scenario==3?570:scenario?760:1000,expectedHeight=scenario==3?420:scenario?560:720;
        require(size.getWidth()==expectedWidth && size.getHeight()==expectedHeight,"default or old small dimensions constrained correctly");
        NSView* root=[[NSView alloc] initWithFrame:NSMakeRect(0,0,size.getWidth(),size.getHeight())];
        auto frame=owned(new Frame);frame->parent=root;
        require(view->setFrame(frame)==kResultOk && view->attached((__bridge void*)root,kPlatformTypeNSView)==kResultOk,"attach only to standalone NSView");
        require(!identified(root,@"JustEQ.AddBand"),"top demo/add row removed");settle(root);
        const auto before=targets(controller);const auto writes=handler->writes;
        if(!scenario) {
            verifyPanel(root,1,dest+"/01-default.png");
            ViewRect minimum(0,0,1,1);require(view->checkSizeConstraint(&minimum)==kResultTrue && minimum.getWidth()==760 && minimum.getHeight()==560,"100 percent minimum is 760x560");
            require(frame->resizeView(view,&minimum)==kResultOk,"minimum resize");verifyPanel(root,1,dest+"/02-minimum-simple.png");
            auto* advanced=actionButton(root,@selector(changeView:));require(advanced,"real Advanced action");[advanced performClick:nil];
            verifyPanel(root,1,dest+"/03-minimum-advanced.png");
            auto* canvas=identified(root,@"JustEQ.Canvas");auto* scroll=canvas.enclosingScrollView;
            // The last existing advanced value remains reachable by scrolling.
            auto* output=identified(canvas,@"JustEQ.Control.2");require(output,"existing output control retained");
            const auto outputRect=[output convertRect:output.bounds toView:canvas];
            [scroll.contentView scrollToPoint:NSMakePoint(0,NSMaxY(canvas.bounds)-scroll.contentView.bounds.size.height)];
            require(NSContainsRect(scroll.contentView.documentVisibleRect,outputRect),"last advanced control fully reachable");
            [scroll.contentView scrollToPoint:NSZeroPoint];
            ViewRect beforeScale(0,0,760,560);require(frame->resizeView(view,&beforeScale)==kResultOk,"pre-scale size");
            auto* settings=actionButton(root,@selector(showSettings:));require(settings,"real settings action");[settings performClick:nil];
            auto* scale=scaleSelector(root);require(scale,"real scale selector");[scale selectItemWithTag:75];
            MemoryStream saved;require(controller->getState(&saved)==kResultOk,"save complete UI state before rejected scale");std::vector<char> previousUi(saved.getData(),saved.getData()+saved.getSize());
            frame->reject=true;require([NSApp sendAction:scale.action to:scale.target from:scale],"request rejected 75 percent via real common action");
            MemoryStream after;require(controller->getState(&after)==kResultOk && previousUi==std::vector<char>(after.getData(),after.getData()+after.getSize()),"host rejection rolls back complete serialized UI state");
            ViewRect rejected;require(view->getSize(&rejected)==kResultOk && rejected.getWidth()==760 && rejected.getHeight()==560,"host rejection keeps editor dimensions");
            frame->reject=false;scale=scaleSelector(root);require(scale,"rebuilt settings selector after rejection");[scale selectItemWithTag:75];unsigned previous=frame->requests;require([NSApp sendAction:scale.action to:scale.target from:scale],"request accepted 75 percent via real common action");
            require(frame->requests==previous+1,"host accepted 75 percent resize");
            [(NSView*)settings.target performSelector:@selector(closeModal:) withObject:nil];
            ViewRect zoomSize;require(view->getSize(&zoomSize)==kResultOk && zoomSize.getWidth()==570 && zoomSize.getHeight()==420,"75 percent physical dimensions");
            ViewRect zoomMinimum(0,0,1,1);require(view->checkSizeConstraint(&zoomMinimum)==kResultTrue && zoomMinimum.getWidth()==570 && zoomMinimum.getHeight()==420,"75 percent minimum");
            verifyPanel(root,.75,dest+"/04-minimum-75-advanced.png");[advanced performClick:nil];
            verifyPanel(root,.75,dest+"/05-minimum-75-simple.png");
        } else verifyPanel(root,scenario==3?.75:1,dest+"/0"+std::to_string(scenario+5)+"-restore-v"+std::to_string(scenario==1?1:2)+".png");
        require(targets(controller)==before && handler->writes==writes,"layout mode resize and scale make zero sound writes");
        if(!scenario){
            auto* canvas=identified(root,@"JustEQ.Canvas");auto* panel=identified(root,@"JustEQ.SelectedPanel");
            set(just::eq::frequency,1055);set(just::eq::gain,-8.6);set(just::eq::target,1);
            [(NSObject*)canvas performSelector:@selector(showSelection)];settle(root);
            require(panel.bounds.size.height==56,"1055Hz -8.6dB Mid safely collapses at minimum viewport");
            const auto* visible=canvas.enclosingScrollView;const auto viewport=visible.contentView.bounds;
            const double graphWidth=viewport.size.width-92,graphHeight=viewport.size.height-52;
            const double x=40+std::log(1055./20)/std::log(1000.)*graphWidth,y=20+graphHeight/2+8.6/36*graphHeight;
            NSRect reserved=NSUnionRect(NSInsetRect(NSMakeRect(x-34,y-12,46,24),-32,-32),NSMakeRect(x-64,y-44,128,26));
            require(!NSIntersectsRect(reserved,panel.frame),"actual low-node frequency/mode zone plus32 clear of card");
            verifyPanel(root,.75,dest+"/09-low-mid-compact.png");
            auto* form=actionButton(panel,@selector(togglePanelForm:));require(form,"real compact expand action");[form performClick:nil];settle(root);
            require(panel.bounds.size.height==132,"manual expand persists over automatic collision collapse");verifyPanel(root,.75,dest+"/10-manual-full.png");
            auto* frequencyControl=identified(panel,@"JustEQ.Control.103");bool exactHz=false;
            for(NSView* child in frequencyControl.subviews)if([child isKindOfClass:NSTextField.class] && [(NSTextField*)child isEditable])exactHz=[[(NSTextField*)child stringValue] isEqualToString:@"1055"];
            require(exactHz,"approved full Hz readout retains1055 instead of rounded1.1kHz");
            [form performClick:nil];settle(root);require(panel.bounds.size.height==56,"manual collapse persists");
            const auto sound=targets(controller);const auto performed=handler->writes;
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:3.8]];settle(root);
            require(panel.hidden && panel.alphaValue==0 && [panel hitTest:panel.frame.origin]==nil,"expired actual view leaves hit tree");
            require(targets(controller)==sound && handler->writes==performed,"fade/form transitions preserve sound state");
            [(NSObject*)canvas performSelector:@selector(showSelection)];settle(root);require(!panel.hidden && panel.alphaValue==1,"same selected node can show again without sound write");
        }
        require(view->removed()==kResultOk,"detach");view->setFrame(nullptr);view=nullptr;
        controller->setComponentHandler(nullptr);controller->terminate();noWindow();
    }
    std::cout<<"PASS "<<checks<<" loaded EQ layout checks; 10 captures; no NSWindow or native OS input\n";
}}
