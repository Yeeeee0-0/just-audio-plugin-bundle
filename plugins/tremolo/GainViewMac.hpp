#pragma once
#import <Cocoa/Cocoa.h>
#include "common/ui/ObjCNames.hpp"
#include "common/vst3/Module.hpp"

#define JTTremoloGainView JUST_OBJC_CLASS(TremoloGainView)
// The card contains only audio-clocked processor measurements. It never runs
// the mock's sine function or estimates gain by dividing a silent input.
@interface JTTremoloGainView : NSView {
@public
    just::EditorServices services;
    just::AnalysisCursor cursor;
    just::AnalysisAvailability availability;
    std::array<just::AnalysisWindow,600> history;
    std::size_t count,write;
    std::uint64_t visualResumeGeneration;
}
- (void)updateAnalysis;
- (const just::AnalysisWindow*)latest;
@end

@implementation JTTremoloGainView
- (BOOL)isFlipped { return YES; }
- (NSString*)textZh:(const char*)zh en:(const char*)en {
    return [NSString stringWithUTF8String:services.view?just::localized(*services.view,zh,en):en];
}
- (const just::AnalysisWindow*)latest { return count?&history[(write+599)%600]:nullptr; }
- (void)updateAnalysis {
    const auto* view=services.view;
    if(view && visualResumeGeneration!=view->visualResumeGeneration){count=write=0;cursor={};availability=just::AnalysisAvailability::unavailable;visualResumeGeneration=view->visualResumeGeneration;}
    if(view && view->visualsPaused)return;
    availability=just::AnalysisAvailability::unavailable;
    for(unsigned attempt=0;attempt<16 && services.readAnalysis;++attempt) {
        just::AnalysisBatch batch;
        availability=services.readAnalysis(services.owner,cursor,batch);
        if(availability!=just::AnalysisAvailability::fresh || !batch.count)break;
        for(unsigned i=0;i<batch.count;++i) {
            const auto& window=batch.windows[i];
            if(count) {
                const auto& previous=history[(write+599)%600];
                if(previous.header.session!=window.header.session || previous.header.epoch!=window.header.epoch)count=write=0;
            }
            history[write]=window;write=(write+1)%600;count=std::min(count+1,std::size_t(600));
        }
    }
    self.needsDisplay=YES;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSBezierPath* card=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,1,1) xRadius:12 yRadius:12];
    [[NSColor colorWithRed:.962 green:.985 blue:.989 alpha:.9] setFill];[card fill];
    [[NSColor colorWithRed:.83 green:.9 blue:.92 alpha:1] setStroke];card.lineWidth=1;[card stroke];
    NSDictionary* label=@{NSFontAttributeName:[NSFont systemFontOfSize:10 weight:NSFontWeightMedium],NSForegroundColorAttributeName:[NSColor colorWithRed:.43 green:.59 blue:.64 alpha:1]};
    [[self textZh:"立体声振幅包络" en:"STEREO AMPLITUDE ENVELOPE"] drawAtPoint:NSMakePoint(18,15) withAttributes:label];
    NSRect plot=NSMakeRect(24,48,MAX(1,self.bounds.size.width-48),MAX(1,self.bounds.size.height-78));
    [[NSColor colorWithRed:.80 green:.88 blue:.90 alpha:.55] setStroke];
    for(unsigned i=0;i<=4;++i) {
        NSBezierPath* line=[NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(NSMinX(plot),NSMinY(plot)+plot.size.height*i/4)];
        [line lineToPoint:NSMakePoint(NSMaxX(plot),NSMinY(plot)+plot.size.height*i/4)];line.lineWidth=1;[line stroke];
    }
    for(unsigned i=0;i<=12;++i) {
        NSBezierPath* line=[NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(NSMinX(plot)+plot.size.width*i/12,NSMinY(plot))];
        [line lineToPoint:NSMakePoint(NSMinX(plot)+plot.size.width*i/12,NSMaxY(plot))];line.lineWidth=1;[line stroke];
    }
    const auto* latest=[self latest];
    NSString* state=@"L / R · 0–1× · 6 s";
    if(availability!=just::AnalysisAvailability::fresh || !latest || !(latest->effectFields&just::analysisModulation)) {
        NSString* empty=availability==just::AnalysisAvailability::stale?[self textZh:"暂无新音频" en:"No new audio"]:[self textZh:"音频反馈不可用" en:"Audio feedback unavailable"];
        [empty drawAtPoint:NSMakePoint(NSMinX(plot)+8,NSMidY(plot)-6) withAttributes:label];
    } else {
        bool above=false;
        const double span=latest->header.sampleRate*6.;
        [NSGraphicsContext saveGraphicsState];[[NSBezierPath bezierPathWithRect:plot] addClip];
        for(unsigned channel=0;channel<latest->header.outputChannels;++channel) {
            NSBezierPath* path=[NSBezierPath bezierPath];bool started=false;std::uint64_t next=0;
            for(std::size_t i=0;i<count;++i) {
                const auto& window=history[(write+600-count+i)%600];
                if(!(window.effectFields&just::analysisModulation)){started=false;continue;}
                double x=NSMaxX(plot)-double(latest->header.endSample-window.header.endSample)/span*plot.size.width;
                if(x<NSMinX(plot)){started=false;continue;}
                above|=window.modulationMax[channel]>1.;
                auto y=[&](double gain){return NSMinY(plot)+(1-std::clamp(gain,0.,1.))*plot.size.height;};
                NSPoint high=NSMakePoint(x,y(window.modulationMax[channel]));
                if(!started || next!=window.header.startSample || (window.header.flags&just::analysisGap))[path moveToPoint:high];else[path lineToPoint:high];
                [path lineToPoint:NSMakePoint(x,y(window.modulationMin[channel]))];started=true;next=window.header.endSample;
            }
            [(channel?[NSColor colorWithRed:.51 green:.73 blue:.78 alpha:1]:[NSColor colorWithRed:.06 green:.71 blue:.77 alpha:1]) setStroke];
            if(channel){CGFloat dash[]={5,3};[path setLineDash:dash count:2 phase:0];}
            path.lineWidth=channel?1.3:2.;[path stroke];
        }
        [NSGraphicsContext restoreGraphicsState];
        if(latest->header.outputChannels==1)state=@"L · 0–1× · 6 s";
        if(above)state=[state stringByAppendingString:[self textZh:" · 超出刻度" en:" · above scale"]];
        if(latest->header.flags&just::analysisBypassed)state=[state stringByAppendingString:[self textZh:" · 旁路" en:" · Bypass"]];
        else if(!(latest->header.flags&just::analysisPlaying) && (latest->header.flags&just::analysisTransportKnown))state=[state stringByAppendingString:[self textZh:" · 停止" en:" · Stopped"]];
        if(latest->channels[0].peak==0 && latest->channels[1].peak==0)state=[state stringByAppendingString:[self textZh:" · 静音" en:" · Silence"]];
    }
    [state drawAtPoint:NSMakePoint(24,self.bounds.size.height-23) withAttributes:label];
}
@end
