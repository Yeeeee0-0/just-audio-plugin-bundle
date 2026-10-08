#import <Cocoa/Cocoa.h>
#include "AnalysisView.hpp"
#include "ObjCNames.hpp"
#define JustAnalysisView JUST_OBJC_CLASS(AnalysisView)
@interface JustAnalysisView : NSView {
@public
    just::EditorServices services;just::AnalysisViewMode mode;
    just::AnalysisCursor cursor;just::AnalysisAvailability availability;
    std::array<just::AnalysisWindow,600> history;std::size_t count,write;
    just::SpectrumSnapshot spectrum;just::SampleFrame samples;
    std::uint64_t resumeGeneration;
}
- (void)updateAnalysis;
@end
@implementation JustAnalysisView
- (BOOL)isFlipped{return YES;}
- (void)updateAnalysis {
    if(services.view && services.view->visualsPaused)return;
    if(services.view && resumeGeneration!=services.view->visualResumeGeneration){resumeGeneration=services.view->visualResumeGeneration;cursor={};count=write=0;spectrum={};samples={};}
    if(mode==just::AnalysisViewMode::spectrum){availability=services.readSpectrum?services.readSpectrum(services.owner,spectrum):just::AnalysisAvailability::unavailable;}
    else if(mode==just::AnalysisViewMode::waveform || mode==just::AnalysisViewMode::stereoField){availability=services.readSamples?services.readSamples(services.owner,samples):just::AnalysisAvailability::unavailable;}
    else {
        availability=just::AnalysisAvailability::unavailable;
        for(unsigned attempt=0;attempt<16 && services.readAnalysis;++attempt){just::AnalysisBatch batch;availability=services.readAnalysis(services.owner,cursor,batch);if(availability!=just::AnalysisAvailability::fresh || !batch.count)break;
            for(unsigned i=0;i<batch.count;++i){const auto& item=batch.windows[i];if(count){const auto& previous=history[(write+599)%600];if(previous.header.session!=item.header.session || previous.header.epoch!=item.header.epoch)count=write=0;}
                history[write]=item;write=(write+1)%600;count=std::min(count+1,std::size_t(600));}
        }
    }self.needsDisplay=YES;
}
- (void)drawRect:(NSRect)dirty {
    [[NSColor colorWithWhite:.98 alpha:1] setFill];NSRectFill(self.bounds);
    NSRect r=NSInsetRect(self.bounds,8,24);if(r.size.width<8 || r.size.height<8)return;
    NSDictionary* attrs=@{NSFontAttributeName:[NSFont systemFontOfSize:11],NSForegroundColorAttributeName:NSColor.secondaryLabelColor};
    if(availability!=just::AnalysisAvailability::fresh){[(availability==just::AnalysisAvailability::stale?@"Feedback stale — no new audio":@"Feedback unavailable") drawAtPoint:NSMakePoint(8,6) withAttributes:attrs];return;}
    NSColor* colors[]={NSColor.systemGrayColor,[NSColor colorWithRed:.09 green:.56 blue:.6 alpha:1],NSColor.systemOrangeColor};
    auto dbY=[&](double amplitude){return NSMaxY(r)-std::clamp((20*std::log10(std::max(1e-8,amplitude))+96)/102.,0.,1.)*r.size.height;};
    [[NSColor colorWithWhite:.88 alpha:1] setStroke];for(int i=0;i<=4;++i){NSBezierPath* p=[NSBezierPath bezierPath];[p moveToPoint:NSMakePoint(NSMinX(r),NSMinY(r)+r.size.height*i/4)];[p lineToPoint:NSMakePoint(NSMaxX(r),NSMinY(r)+r.size.height*i/4)];[p stroke];}
    NSString* title=@"Input (gray) / output (teal) · same time and dBFS scale";
    if(mode==just::AnalysisViewMode::spectrum){
        title=@"Input / output spectrum · dBFS · actual sample rate";
        for(unsigned tap=0;tap<2;++tap){NSBezierPath* p=[NSBezierPath bezierPath];BOOL started=NO;
            for(unsigned k=1;k<just::spectrumBins;++k){double hz=double(k)*spectrum.header.sampleRate/spectrum.fftSize;if(hz<20)continue;
                double x=NSMinX(r)+std::log(hz/20)/std::log((spectrum.header.sampleRate/2)/20)*r.size.width;NSPoint point=NSMakePoint(x,dbY(spectrum.amplitude[tap][k]));if(!started){[p moveToPoint:point];started=YES;}else[p lineToPoint:point];}
            [colors[tap] setStroke];p.lineWidth=1.5;[p stroke];}
    }else if(mode==just::AnalysisViewMode::waveform || mode==just::AnalysisViewMode::stereoField){
        title=mode==just::AnalysisViewMode::waveform?@"Input / output waveform · fixed ±1 scale":@"Input / output stereo field · fixed scale";
        if(!(samples.header.flags&just::analysisInputAligned)){[@"Waiting for PDC alignment" drawAtPoint:NSMakePoint(8,6) withAttributes:attrs];return;}
        for(unsigned tap=0;tap<2;++tap){NSBezierPath* p=[NSBezierPath bezierPath];
            for(unsigned i=0;i<samples.count;++i){double l=samples.samples[2*tap][i],rr=(tap?samples.header.outputChannels:samples.header.inputChannels)==2?samples.samples[2*tap+1][i]:l;
                double x,y;if(mode==just::AnalysisViewMode::waveform){x=NSMinX(r)+double(i)*r.size.width/std::max(1u,samples.count-1);y=NSMidY(r)-std::clamp(l,-1.,1.)*r.size.height*.5;}
                else{x=NSMidX(r)+std::clamp((l-rr)*.5,-1.,1.)*r.size.width*.5;y=NSMidY(r)-std::clamp((l+rr)*.5,-1.,1.)*r.size.height*.5;}
                if(!i)[p moveToPoint:NSMakePoint(x,y)];else[p lineToPoint:NSMakePoint(x,y)];}
            [colors[tap] setStroke];p.lineWidth=1;[p stroke];}
    }else if(count){
        const auto& newest=history[(write+599)%600];double end=newest.header.endSample,span=newest.header.sampleRate*6.;
        const bool wet=mode==just::AnalysisViewMode::wetStereo;title=wet?@"Actual wet contribution · L / R · dBFS":title;
        for(unsigned tap=0;tap<2;++tap){NSBezierPath* p=[NSBezierPath bezierPath];BOOL started=NO;std::uint64_t next=0;
            for(std::size_t i=0;i<count;++i){const auto& w=history[(write+600-count+i)%600];const auto& h=w.header;
                if(!(h.flags&just::analysisInputAligned) || (wet && !(w.effectFields&just::analysisWet))){started=NO;continue;}
                double amplitude=wet?w.channels[4+tap].peak:std::max(w.channels[tap*2].peak,w.channels[tap*2+1].peak);
                double x=NSMaxX(r)-(end-h.endSample)/span*r.size.width;double y=dbY(amplitude);
                if(wet)y=NSMinY(r)+tap*r.size.height*.5+(y-NSMinY(r))*.5;
                if(x<NSMinX(r)){started=NO;continue;}NSPoint point=NSMakePoint(x,y);
                if(!started || next!=h.startSample || h.flags&just::analysisGap)[p moveToPoint:point];else[p lineToPoint:point];started=YES;next=h.endSample;
            }[colors[tap] setStroke];p.lineWidth=1.5;[p stroke];
        }
        if(newest.header.flags&just::analysisBypassed)title=[title stringByAppendingString:@" · Bypass"];
        else if(!(newest.header.flags&just::analysisPlaying) && newest.header.flags&just::analysisTransportKnown)title=[title stringByAppendingString:@" · Host stopped"];
        if(std::max(newest.channels[0].peak,newest.channels[1].peak)==0)title=[title stringByAppendingString:std::max(newest.channels[2].peak,newest.channels[3].peak)>0?@" · Tail/output active":@" · Silence"];
        if(!wet && newest.effectFields&just::analysisReduction){
            NSBezierPath* gr=[NSBezierPath bezierPath];BOOL started=NO;std::uint64_t next=0;
            for(std::size_t i=0;i<count;++i){const auto& w=history[(write+600-count+i)%600];if(!(w.effectFields&just::analysisReduction)){started=NO;continue;}
                double x=NSMaxX(r)-(end-w.header.endSample)/span*r.size.width;if(x<NSMinX(r)){started=NO;continue;}
                NSPoint point=NSMakePoint(x,NSMinY(r)+std::clamp(w.reductionDb/24.,0.,1.)*r.size.height*.25);
                if(!started || next!=w.header.startSample || w.header.flags&just::analysisGap)[gr moveToPoint:point];else[gr lineToPoint:point];started=YES;next=w.header.endSample;
            }[colors[2] setStroke];gr.lineWidth=1.5;[gr stroke];[@"GR: 0–24 dB (orange, upper quarter)" drawAtPoint:NSMakePoint(8,NSMaxY(r)+3) withAttributes:attrs];
        }
    }
    [title drawAtPoint:NSMakePoint(8,6) withAttributes:attrs];
}
@end
namespace just {
class MacAnalysisView final:public AnalysisView {
    JustAnalysisView* view;
public:
    MacAnalysisView(void* parent,const EditorServices& services,AnalysisViewMode mode){view=[[JustAnalysisView alloc] initWithFrame:NSMakeRect(0,0,600,180)];view.identifier=[NSString stringWithUTF8String:analysisViewIdentifier];view.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];view->services=services;view->mode=mode;view->availability=AnalysisAvailability::unavailable;[(__bridge NSView*)parent addSubview:view];}
    ~MacAnalysisView() override{[view removeFromSuperview];view=nil;}
    void resize(int x,int y,int width,int height) override{view.frame=NSMakeRect(x,y,std::max(1,width),std::max(1,height));}
    void refresh() override{[view updateAnalysis];}
    void* nativeHandle() const noexcept override{return (__bridge void*)view;}
};
std::unique_ptr<AnalysisView> AnalysisView::create(void* parent,const EditorServices& services,AnalysisViewMode mode){return parent?std::make_unique<MacAnalysisView>(parent,services,mode):nullptr;}
}
