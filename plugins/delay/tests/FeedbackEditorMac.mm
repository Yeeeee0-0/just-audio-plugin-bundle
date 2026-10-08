#import <Cocoa/Cocoa.h>
#include "../EditorModel.hpp"
#include "../DelayEngine.hpp"
#include <iostream>
using namespace just;
using namespace just::delay;
static void check(bool good,const char* label){if(!good){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct Mock {
    SoundState state=initialState({},registry);
    std::array<AnalysisWindow,600> windows{};
    unsigned begins=0,writes=0,ends=0;
    bool hostTempo=false;
};
struct WetPeak final:AnalysisTap {
    double peak[2]{};
    double delayMs[2]{};
    std::uint32_t fields=0;
    void pushSample(std::uint32_t,const EffectAnalysisSample& sample) noexcept override {
        fields|=sample.validFields&(analysisWet|analysisDelay);
        if(sample.validFields&analysisWet)for(int ch=0;ch<2;++ch)peak[ch]=std::max(peak[ch],std::abs(sample.wet[ch]));
        if(sample.validFields&analysisDelay)for(int ch=0;ch<2;++ch)delayMs[ch]=sample.delayMs[ch];
    }
};
static void fillMeasuredRepeats(Mock& mock){
    set(mock.state,route,2);set(mock.state,mix,100);set(mock.state,timeL,250);set(mock.state,timeR,250);set(mock.state,feedback,70);
    DelayEngine engine;check(engine.prepare({48000,480,2,2,0,SampleFormat::float64,false}),"engine prepares");engine.applyTargets(mock.state,0);
    check(engine.latencySamples()==15,"measured fixture retains PDC15");
    std::array<double,480> left{},right{},outL{},outR{};left[0]=right[0]=1;
    WetPeak tap;ProcessContext context;context.analysis=&tap;
    for(unsigned n=0;n<mock.windows.size();++n){
        left[0]=right[0]=n%100==0?1:0;
        tap.peak[0]=tap.peak[1]=0;tap.fields=0;
        AudioBlock<double> block{{left.data(),right.data()},{outL.data(),outR.data()},2,2,480,n?3ull:0ull};
        engine.process(block,context);engine.endBlock();
        auto& window=mock.windows[n];auto& header=window.header;
        header.session=header.epoch=1;header.sequence=n+1;header.startSample=n*480;header.endSample=(n+1)*480;
        header.sampleRate=48000;header.inputChannels=header.outputChannels=2;header.sourceNanoseconds=analysisNow();
        header.flags=analysisInputAligned|analysisPlaying|analysisTransportKnown;
        window.effectFields=tap.fields;
        window.delayMs[0]=tap.delayMs[0];window.delayMs[1]=tap.delayMs[1];
        window.channels[0].peak=window.channels[1].peak=n%100==0?1:0;
        window.channels[4].peak=tap.peak[0];window.channels[5].peak=tap.peak[1];
    }
    check(mock.windows[25].channels[4].peak>.9,"first measured left repeat");
    check(mock.windows[50].channels[5].peak>.05,"second measured right repeat");
}
static NSButton* ping(NSView* view){for(NSView* child in view.subviews){if([child isKindOfClass:NSButton.class] && [child.identifier isEqualToString:@"just.delay.ping-pong"])return (NSButton*)child;if(auto* result=ping(child))return result;}return nil;}
static NSTextField* timeField(NSView* view){
    if([view.identifier isEqualToString:@"just.common.rotary"] && [view.accessibilityLabel isEqualToString:@"Time"])
        for(NSView* child in view.subviews)if([child isKindOfClass:NSTextField.class] && ((NSTextField*)child).editable)return (NSTextField*)child;
    for(NSView* child in view.subviews)if(auto* result=timeField(child))return result;
    return nil;
}
static bool hasText(NSView* view,NSString* needle){for(NSView* child in view.subviews){if([child isKindOfClass:NSTextField.class] && [[(NSTextField*)child stringValue] containsString:needle])return true;if(hasText(child,needle))return true;}return false;}
static NSTextField* timingNote(NSView* view){
    for(NSView* child in view.subviews){
        if([child isKindOfClass:NSTextField.class] && [[(NSTextField*)child stringValue] hasPrefix:@"L: "] && !child.hiddenOrHasHiddenAncestor)return (NSTextField*)child;
        if(auto* result=timingNote(child))return result;
    }
    return nil;
}
static void checkTimingLabels(EditorContent& editor,NSView* parent,EditorViewState& view,Mock& mock){
    struct DivisionCase {int left,right;NSString* leftText;NSString* rightText;};
    const DivisionCase divisions[]={{4,3,@"1/4",@"1/8"},{11,10,@"1/4 D",@"1/8 D"},{18,17,@"1/4 T",@"1/8 T"}};
    const auto saved=mock.state;const auto savedLanguage=view.language;const bool savedTempo=mock.hostTempo;
    const unsigned begins=mock.begins,writes=mock.writes,ends=mock.ends;unsigned cases=0;
    mock.hostTempo=true;
    for(auto language:{UiLanguage::chinese,UiLanguage::english})for(const auto& division:divisions)for(int mode=0;mode<4;++mode){
        view.language=language;set(mock.state,syncL,mode&1);set(mock.state,syncR,(mode>>1)&1);
        set(mock.state,noteL,division.left);set(mock.state,noteR,division.right);
        const auto targets=mock.state.targets;editor.refresh(view,{});
        auto* note=timingNote(parent);check(note && NSMaxY(note.frame)<=note.superview.bounds.size.height,"timing note visible in minimum content size");
        NSString* freeText=language==UiLanguage::chinese?@"自由 ms":@"Free ms";
        NSString* expected=[NSString stringWithFormat:@"L: %@ · R: %@",mode&1?division.leftText:freeText,mode&2?division.rightText:freeText];
        if(![note.stringValue hasPrefix:expected]){std::cerr<<"Timing label: expected "<<expected.UTF8String<<"; got "<<note.stringValue.UTF8String<<'\n';check(false,"Chinese/English Free/Sync and straight/dotted/triplet labels");}
        check([note.stringValue containsString:language==UiLanguage::chinese?@"宿主速度 120.0 BPM":@"Host tempo 120.0 BPM"],"timing labels retain fresh tempo suffix");
        check([note.stringValue containsString:language==UiLanguage::chinese?@"实测 L/R 250.0 / 250.0 ms":@"Actual L/R 250.0 / 250.0 ms"],"timing labels retain actual engine delay suffix");
        if(mode)check([note.stringValue containsString:language==UiLanguage::chinese?@"时间：自定义 — Advanced":@"Time: Custom — Advanced"],"Sync keeps Simple Time Custom semantics");
        check(mock.state.targets==targets && mock.begins==begins && mock.writes==writes && mock.ends==ends,"label refresh changes no sound targets or gestures");++cases;
    }
    mock.state=saved;mock.hostTempo=savedTempo;view.language=savedLanguage;editor.refresh(view,{});
    check(mock.state.targets==saved.targets && mock.begins==begins && mock.writes==writes && mock.ends==ends,"label matrix restore leaves complete sound targets unchanged");
    std::cout<<"PASS timing labels: "<<cases<<" Chinese/English Free/Sync + straight/dotted/triplet cases, fresh tempo/actual delay/Custom preserved, no sound edits\n";
}
static void screenshot(NSView* view,const char* path){
    [view layoutSubtreeIfNeeded];auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    check([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path] atomically:YES],"screenshot written");
}
int main(int argc,char** argv){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    check(NSApp.windows.count==0,"fixture starts without NSWindow");
    Mock mock;fillMeasuredRepeats(mock);EditorViewState view;view.language=UiLanguage::english;EditorServices services;services.owner=&mock;services.view=&view;
    services.readTarget=[](void* owner,ParamID id){auto& m=*static_cast<Mock*>(owner);return m.state.targets[registry.index(id)];};
    services.beginEdit=[](void* owner,ParamID){++static_cast<Mock*>(owner)->begins;return true;};
    services.performEdit=[](void* owner,ParamID id,double value){auto& m=*static_cast<Mock*>(owner);++m.writes;m.state.targets[registry.index(id)]=value;return true;};
    services.endEdit=[](void* owner,ParamID){++static_cast<Mock*>(owner)->ends;};
    services.readAnalysis=[](void* owner,AnalysisCursor& cursor,AnalysisBatch& batch){
        auto& m=*static_cast<Mock*>(owner);batch.count=0;
        for(unsigned i=0;i<batch.windows.size() && cursor.sequence<m.windows.size();++i){batch.windows[i]=m.windows[cursor.sequence++];++batch.count;}
        return AnalysisAvailability::fresh;
    };
    services.readRuntimeTelemetry=[](void* owner,RuntimeTelemetrySnapshot& snapshot){
        if(!static_cast<Mock*>(owner)->hostTempo)return TelemetryAvailability::unavailable;
        snapshot.session=1;snapshot.sequence=1;snapshot.queueContext=1;snapshot.validFields=telemetryTempo;snapshot.bpm=120;
        return TelemetryAvailability::fresh;
    };
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,640,356)];
    std::unique_ptr<EditorContent> editor(createEditorContent());check(editor->attach((__bridge void*)parent,services),"editor attaches");editor->resize(640,356);for(int i=0;i<5;++i)editor->refresh(view,{});
    check(ping(parent) && ping(parent).state==NSControlStateValueOn,"Ping Pong remains directly visible");
    check(timeField(parent) && timeField(parent).enabled,"symmetric Ping Pong Time remains editable");
    if(argc>1)screenshot(parent,argv[1]);
    mock.hostTempo=true;editor->refresh(view,{});check(hasText(parent,@"Host tempo 120.0 BPM"),"fresh host tempo clearly displayed");
    mock.hostTempo=false;editor->refresh(view,{});check(hasText(parent,@"Host tempo unavailable"),"missing host tempo clearly displayed");
    checkTimingLabels(*editor,parent,view,mock);
    set(mock.state,timeL,250.123456789);set(mock.state,timeR,250.123456789);editor->refresh(view,{});
    auto* exactField=timeField(parent);auto* delegate=(id<NSTextFieldDelegate>)exactField.delegate;
    NSNotification* editNote=[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:exactField];
    [delegate controlTextDidBeginEditing:editNote];
    check(std::abs(exactField.doubleValue-250.123456789)<1e-9,"Time editing exposes full physical precision");
    editNote=[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:exactField];
    [delegate controlTextDidEndEditing:editNote];
    check(mock.writes==0 && value(mock.state,timeL)==value(mock.state,timeR),"unchanged precise Time edit makes no host write");
    auto before=mock.state.targets;for(int i=0;i<12;++i){view.advanced=!view.advanced;editor->refresh(view,{});}
    check(mock.state.targets==before && mock.writes==0,"Simple Advanced view switch leaves sound state unchanged");
    editor.reset();check(parent.subviews.count==0,"editor removes owned content");
    check(parent.window==nil && NSApp.windows.count==0,"fixture completed without NSWindow or activation");
    std::cout<<"PASS measured Ping Pong left/right feedback UI, direct route button, pure view switch\n";
}}
