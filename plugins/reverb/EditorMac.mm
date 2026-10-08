#import <Cocoa/Cocoa.h>
#include "Editor.hpp"
#include "Presets.hpp"
#include "FeedbackHistory.hpp"
#include "SimpleLayout.hpp"
#include "UiText.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/ObjCNames.hpp"
#define JustReverbDocument JUST_OBJC_CLASS(ReverbDocument)
#define JustReverbPlot JUST_OBJC_CLASS(ReverbPlot)
#include <memory>

@interface JustReverbDocument : NSView
@end
@implementation JustReverbDocument
- (BOOL)isFlipped{return YES;}
@end
@interface JustReverbPlot : NSView {
@public
    just::reverb::FeedbackHistory* history;
    just::EditorViewState* ui;
}
@end
@implementation JustReverbPlot
- (BOOL)isFlipped{return YES;}
- (void)drawRect:(NSRect)dirty {
    const BOOL chinese=ui && ui->language==just::UiLanguage::chinese;
    auto* newest=history?history->latest():nullptr;
    NSString* title=chinese?@"干声输入 → 混响尾音":@"DRY INPUT → REVERB TAIL";
    NSString* hint=chinese?@"等待音频数据":@"Waiting for audio";
    const bool fresh=history && history->availability()==just::AnalysisAvailability::fresh && newest;
    bool valid=fresh && (newest->header.flags&just::analysisInputAligned) && (newest->effectFields&just::analysisWet);
    if(history && history->availability()==just::AnalysisAvailability::stale)hint=chinese?@"暂无新音频":@"No new audio";
    else if(fresh) {
        if(newest->header.flags&just::analysisGap){hint=chinese?@"音频数据间断":@"Audio data gap";valid=false;}
        else if(!(newest->effectFields&just::analysisWet))hint=chinese?@"湿声测量不可用":@"Wet measurement unavailable";
        else if(!(newest->header.flags&just::analysisInputAligned))hint=chinese?@"等待对齐输入":@"Waiting for aligned input";
        else if(newest->header.flags&just::analysisBypassed)hint=chinese?@"旁路":@"Bypass";
        else if((newest->header.flags&just::analysisTransportKnown) && !(newest->header.flags&just::analysisPlaying))hint=chinese?@"宿主已停止":@"Host stopped";
        else if(std::max(newest->channels[0].peak,newest->channels[1].peak)==0) {
            hint=std::max(newest->channels[4].peak,newest->channels[5].peak)>0?(chinese?@"干声静音 · 尾音持续":@"Dry silent · wet tail active"):(chinese?@"静音":@"Silence");
        } else hint=chinese?@"实时干声 / 湿声贡献":@"Measured dry / wet contribution";
    }
    auto text=@{NSFontAttributeName:[NSFont systemFontOfSize:10],NSForegroundColorAttributeName:[NSColor colorWithRed:.43 green:.56 blue:.60 alpha:1]};
    [title drawAtPoint:NSMakePoint(0,0) withAttributes:text];
    NSRect plot=NSMakeRect(0,30,self.bounds.size.width,MAX(10,self.bounds.size.height-58));
    for(unsigned row=0;row<4;++row) {
        auto* line=[NSBezierPath bezierPath];line.lineWidth=1;
        CGFloat y=NSMinY(plot)+row*NSHeight(plot)/3;[line moveToPoint:NSMakePoint(NSMinX(plot),y)];[line lineToPoint:NSMakePoint(NSMaxX(plot),y)];
        [[NSColor colorWithRed:.84 green:.90 blue:.92 alpha:.75] setStroke];[line stroke];
    }
    [hint drawAtPoint:NSMakePoint(0,NSMaxY(plot)+10) withAttributes:text];
    NSString* scale=chinese?@"过去 6 s · 峰值 −96…0 dBFS":@"Past 6 s · peak −96…0 dBFS";
    NSSize extent=[scale sizeWithAttributes:text];[scale drawAtPoint:NSMakePoint(MAX(0,self.bounds.size.width-extent.width),NSMaxY(plot)+10) withAttributes:text];
    if(!valid)return;
    const double end=double(newest->header.endSample),span=newest->header.sampleRate*6.;
    const CGFloat center=NSMidY(plot),amplitude=NSHeight(plot)*.48;
    auto level=[](double peak){return std::clamp((20*std::log10(std::max(1e-8,peak))+96)/96.,0.,1.);};
    NSGraphicsContext* context=NSGraphicsContext.currentContext;[context saveGraphicsState];NSRectClip(plot);
    history->each([&](const just::AnalysisWindow& item){
        if(!(item.header.flags&just::analysisInputAligned))return;
        double x=NSMaxX(plot)-(end-double(item.header.endSample))/span*NSWidth(plot);
        if(x<NSMinX(plot) || x>NSMaxX(plot))return;
        const double dry=std::max(item.channels[0].peak,item.channels[1].peak);
        if(dry>0) {
            auto* bar=[NSBezierPath bezierPath];bar.lineWidth=2.0;
            const double a=level(dry)*amplitude;[bar moveToPoint:NSMakePoint(x,center-a)];[bar lineToPoint:NSMakePoint(x,center+a)];
            [[NSColor colorWithRed:.34 green:.54 blue:.60 alpha:.85] setStroke];[bar stroke];
        }
        if(!(item.effectFields&just::analysisWet) || (item.header.flags&just::analysisGap))return;
        for(unsigned ch=0;ch<2;++ch) {
            const double peak=item.channels[4+ch].peak;if(peak<=0)continue;
            const double a=level(peak)*amplitude;
            auto* bar=[NSBezierPath bezierPath];bar.lineWidth=2.2;bar.lineCapStyle=NSLineCapStyleRound;
            [bar moveToPoint:NSMakePoint(x+(ch?1:-1),center-a)];[bar lineToPoint:NSMakePoint(x+(ch?1:-1),center+a)];
            [[NSColor colorWithRed:ch?.44:.08 green:ch?.81:.67 blue:ch?.85:.75 alpha:ch?.55:.75] setStroke];[bar stroke];
        }
    });
    [context restoreGraphicsState];
}
@end
namespace just::reverb {
namespace {
inline SoundState readTargets(const EditorServices& s) {
    SoundState state;for(std::size_t i=0;i<registry.count;++i)state.targets[i]=s.readTarget(s.owner,parameters[i].id);return state;
}
struct MacroAdapter {
    EditorServices services{};SimpleMacro macro=SimpleMacro::mix;MacroGesture gesture;
    RotaryBinding binding() {
        RotaryBinding b;b.owner=this;b.view=services.view;
        b.read=[](void* p){auto& a=*static_cast<MacroAdapter*>(p);return macroNormalized(a.macro,readTargets(a.services));};
        b.begin=[](void* p){if(!macroWiringApproved)return false;auto& a=*static_cast<MacroAdapter*>(p);return a.gesture.begin(a.services,a.macro,readTargets(a.services));};
        b.write=[](void* p,double v){return static_cast<MacroAdapter*>(p)->gesture.update(v);};
        b.end=[](void* p){static_cast<MacroAdapter*>(p)->gesture.end();};return b;
    }
};
class MacContent final:public EditorContent {
    JustReverbDocument* document=nil;
    JustReverbPlot* plot=nil;
    NSScrollView* scroll=nil;
    NSTextField *advancedTitle=nil,*extraTitle=nil;
    EditorServices services{};
    bool advanced=false;int width=1120,height=460;
    FeedbackHistory feedback;
    std::uint64_t visualResumeGeneration=0;
    std::array<MacroAdapter,simpleMacroCount> adapters;
    std::array<std::unique_ptr<RotaryControl>,simpleMacroCount> simple;
    std::array<std::unique_ptr<RotaryControl>,registry.count-1> detail;
    std::array<ParamID,registry.count-1> detailIDs{};
    void layout() {
        if(!document)return;
        auto layout=SimpleLayout::fit(width);
        const auto& g=layout.graph;plot.frame=NSMakeRect(g.x,g.y,g.width,g.height);
        for(unsigned n=0;n<simple.size();++n){const auto& r=layout.controls[n];simple[n]->resize(r.x,r.y,r.width,r.height);}
        int rowHeight=156,cell=(width-72)/6;
        int advancedStart=layout.height+20;
        advancedTitle.frame=NSMakeRect(36,advancedStart,width-72,24);
        extraTitle.frame=NSMakeRect(36,advancedStart+190,width-72,24);
        advancedTitle.hidden=extraTitle.hidden=!advanced;
        for(unsigned n=0;n<detail.size();++n) {
            auto* handle=(__bridge NSView*)detail[n]->nativeHandle();handle.hidden=!advanced;
            const int x=n<4?36+(width-72-4*cell)/2+int(n)*cell:36+int((n-4)%6)*cell;
            const int y=n<4?advancedStart+30:advancedStart+220+int((n-4)/6)*rowHeight;
            detail[n]->resize(x+MAX(0,(cell-112)/2),y,MIN(112,cell),148);
        }
        document.frame=NSMakeRect(0,0,width,advanced?advancedStart+220+6*rowHeight:MAX(height,layout.height));
        scroll.frame=NSMakeRect(0,0,width,height);scroll.hasVerticalScroller=advanced || height<layout.height;
    }
public:
    ~MacContent() override {
        // Shared controls end active gestures before adapters and parent die.
        for(auto& control:detail)control.reset();for(auto& control:simple)control.reset();
        [scroll removeFromSuperview];plot=nil;document=nil;scroll=nil;
    }
    bool attach(void* parent,const EditorServices& s) override {
        if(!parent || !s.readTarget || !s.beginEdit || !s.performEdit || !s.endEdit)return false;services=s;
        scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect];scroll.drawsBackground=NO;scroll.borderType=NSNoBorder;
        document=[[JustReverbDocument alloc] initWithFrame:NSZeroRect];scroll.documentView=document;[(__bridge NSView*)parent addSubview:scroll];
        plot=[[JustReverbPlot alloc] initWithFrame:NSZeroRect];plot->history=&feedback;plot->ui=s.view;[document addSubview:plot];
        constexpr const char* zh[]={"明亮度","质感","距离","空间","衰减速率","立体声宽度","混合"};
        for(unsigned n=0;n<simple.size();++n) {
            auto macro=SimpleMacro(n);adapters[n].services=s;adapters[n].macro=macro;DisplayPolicy policy;policy.labelZh=zh[n];
            simple[n]=RotaryControl::create((__bridge void*)document,adapters[n].binding(),macroDisplaySpec(macro),policy);if(!simple[n])return false;
            auto* native=(__bridge NSView*)simple[n]->nativeHandle();native.toolTip=[NSString stringWithUTF8String:macroHelp(macro,s.view && s.view->language==UiLanguage::chinese)];
        }
        unsigned index=0;for(auto id:{Predelay,Diffusion,WetHP,HighCut})detailIDs[index++]=id;
        for(unsigned i=1;i<registry.count;++i){auto id=parameters[i].id;if(id!=Predelay && id!=Diffusion && id!=WetHP && id!=HighCut)detailIDs[index++]=id;}
        for(unsigned n=0;n<detail.size();++n) {
            const auto& parameter=parameters[registry.index(detailIDs[n])];DisplayPolicy policy;
            policy.labelZh=detailLabelZh(detailIDs[n]);
            detail[n]=RotaryControl::create((__bridge void*)document,s,parameter,policy);if(!detail[n])return false;
        }
        advancedTitle=[NSTextField labelWithString:@""];extraTitle=[NSTextField labelWithString:@""];
        for(auto* title:{advancedTitle,extraTitle}){title.font=[NSFont systemFontOfSize:10 weight:NSFontWeightMedium];title.textColor=[NSColor colorWithRed:.43 green:.56 blue:.60 alpha:1];[document addSubview:title];}
        layout();return true;
    }
    void resize(int w,int h) override {width=w;height=h;layout();}
    void refresh(const EditorViewState& view,const StatusSnapshot&) override {
        if(advanced!=view.advanced){advanced=view.advanced;layout();}
        for(unsigned n=0;n<simple.size();++n){simple[n]->refresh(macroWiringApproved);
            auto* native=(__bridge NSView*)simple[n]->nativeHandle();native.toolTip=[NSString stringWithUTF8String:macroHelp(SimpleMacro(n),view.language==UiLanguage::chinese)];}
        auto state=readTargets(services);
        const auto range=decayRateBounds(state);
        auto* rateView=(__bridge NSView*)simple[4]->nativeHandle();
        rateView.toolTip=[NSString stringWithFormat:view.language==UiLanguage::chinese?@"当前三频比例允许 %.1f–%.1f%%；中频时间 = 空间 × 实际速率 / 100，共同边界保持三段比例":@"Current spectral ratios allow %.1f–%.1f%%; mid time = Space × actual Rate / 100; joint limits preserve all ratios",range[0],range[1]];
        for(unsigned n=0;n<detail.size();++n)detail[n]->refresh(!(detailIDs[n]==Predelay && physical(state,Sync)>=.5));
        advancedTitle.stringValue=view.language==UiLanguage::chinese?@"进阶控制":@"ADDITIONAL CONTROLS";
        extraTitle.stringValue=view.language==UiLanguage::chinese?@"完整声音参数":@"COMPLETE SOUND PARAMETERS";
        if(visualResumeGeneration!=view.visualResumeGeneration){feedback={};visualResumeGeneration=view.visualResumeGeneration;}
        if(!view.visualsPaused)feedback.poll(services);
        plot.needsDisplay=YES;
    }
};
}
EditorContent* createEditor(){return new MacContent;}
}
