#import <Cocoa/Cocoa.h>
#include "FieldModel.hpp"
static NSColor* JWColor(double r,double g,double b,double a=1){return [NSColor colorWithRed:r green:g blue:b alpha:a];}
static void JWText(NSString* text,NSPoint p,double size,NSColor* color) {
    [text drawAtPoint:p withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],NSForegroundColorAttributeName:color}];
}
@interface JWMeasuredField : NSView {
@public
    just::stereo::FieldMeasurement measured;
    just::AnalysisAvailability availability;
    BOOL meterOnly,midSide,zh;
}
@end
@implementation JWMeasuredField
- (BOOL)isFlipped{return YES;}
- (NSView*)hitTest:(NSPoint)p {(void)p;return nil;}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;const double w=self.bounds.size.width,h=self.bounds.size.height;
    NSColor* teal=JWColor(.06,.68,.75),*muted=JWColor(.44,.59,.64),*ink=JWColor(.18,.35,.40);
    const bool fresh=availability==just::AnalysisAvailability::fresh && measured.valid;
    if(meterOnly){
        JWText(zh?@"输出电平":@"Output level",NSMakePoint(MAX(0,(w-80)/2),0),12,ink);
        double top=53,bottom=h-46,rail=MAX(16,bottom-top);
        for(unsigned c=0;c<2;++c){double x=(c?.72:.28)*w;NSString* name=midSide?(c?@"S":@"M"):(c?@"R":@"L");JWText(name,NSMakePoint(x-4,30),10,muted);
            NSRect bar=NSMakeRect(x-8,top,16,rail);[JWColor(.85,.91,.93) setFill];[[NSBezierPath bezierPathWithRoundedRect:bar xRadius:4 yRadius:4] fill];
            const double peak=measured.peak(c,midSide),db=peak>0?20*std::log10(peak):-120;
            if(fresh && peak>0){double amount=std::clamp((db+60)/60,0.,1.);NSRect fill=NSMakeRect(x-8,bottom-rail*amount,16,rail*amount);[teal setFill];[[NSBezierPath bezierPathWithRoundedRect:fill xRadius:4 yRadius:4] fill];if(db>0){[JWColor(.91,.49,.36) setFill];NSRectFill(NSMakeRect(x-8,top-4,16,3));}}
            NSString* label=!fresh?@"—":peak<=0?@"−∞":[NSString stringWithFormat:@"%.1f",db];JWText(label,NSMakePoint(x-17,bottom+12),11,ink);
        }
        JWText(@"dBFS · sample peak",NSMakePoint(MAX(0,(w-102)/2),h-12),9,muted);return;
    }
    NSBezierPath* card=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,.5,.5) xRadius:12 yRadius:12];
    [JWColor(.945,.977,.985,.86) setFill];[card fill];[JWColor(.80,.88,.91) setStroke];card.lineWidth=1;[card stroke];
    JWText(zh?@"立体声声场":@"STEREO FIELD",NSMakePoint(18,16),10,muted);
    JWText(zh?@"实测输出":@"MEASURED OUTPUT",NSMakePoint(MAX(160,w-135),16),9,muted);
    const auto g=just::stereo::FieldGeometry::fit(w,h);
    NSBezierPath* grid=[NSBezierPath bezierPath];grid.lineWidth=.75;
    for(int ring=1;ring<=5;++ring){double r=g.radius*ring/5;
        for(int step=0;step<=80;++step){double a=M_PI*step/80;NSPoint p=NSMakePoint(g.centerX-r*std::cos(a),g.baseline-r*std::sin(a));if(!step)[grid moveToPoint:p];else[grid lineToPoint:p];}}
    [grid moveToPoint:NSMakePoint(g.centerX-g.radius,g.baseline)];[grid lineToPoint:NSMakePoint(g.centerX+g.radius,g.baseline)];
    [grid moveToPoint:NSMakePoint(g.centerX,g.baseline)];[grid lineToPoint:NSMakePoint(g.centerX,g.baseline-g.radius)];[JWColor(.73,.85,.90) setStroke];[grid stroke];
    if(fresh){[JWColor(.06,.66,.73,.4) setFill];for(std::size_t i=0;i<measured.count;++i){const auto p=g.pixel(measured.points[i],measured.scale);NSRect dot=NSMakeRect(p.side-.9,p.mid-.9,1.8,1.8);[[NSBezierPath bezierPathWithOvalInRect:dot] fill];}}
    else JWText(availability==just::AnalysisAvailability::stale?(zh?@"测量已过期":@"Measurement stale"):(zh?@"等待音频":@"Waiting for audio"),NSMakePoint(g.centerX-55,g.baseline-g.radius*.45),12,muted);
    JWText(@"L",NSMakePoint(g.centerX-g.radius,g.baseline+13),10,muted);JWText(@"0",NSMakePoint(g.centerX-3,g.baseline+13),10,muted);JWText(@"R",NSMakePoint(g.centerX+g.radius-6,g.baseline+13),10,muted);
    NSString* scale=[NSString stringWithFormat:zh?@"M/S 等比例 · ±%.1f":@"Equal M/S scale · ±%.1f",measured.scale];JWText(scale,NSMakePoint(18,h-15),9,muted);
}
@end
