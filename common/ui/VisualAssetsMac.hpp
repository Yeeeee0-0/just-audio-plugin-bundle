#pragma once
#import <Cocoa/Cocoa.h>
#include "VisualAssets.hpp"
#include "ObjCNames.hpp"
#define JustBackgroundView JUST_OBJC_CLASS(BackgroundView)
static NSColor* justColor(unsigned value,CGFloat alpha=1){return [NSColor colorWithRed:((value>>16)&255)/255. green:((value>>8)&255)/255. blue:(value&255)/255. alpha:alpha];}
struct JustAssetStore {
    NSDictionary* manifest=nil;NSString* root=nil;
    void load(Class anchor,const char* slug){
        root=[[[NSBundle bundleForClass:anchor] resourcePath] stringByAppendingPathComponent:@"JustUI"];
#ifdef JUST_UI_TEST_RESOURCE_DIR
        if(![[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:@"manifest.json"]])root=@JUST_UI_TEST_RESOURCE_DIR;
#endif
        NSData* data=[NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"manifest.json"]];
        if(data.length<1024*1024){id json=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;if([json isKindOfClass:NSDictionary.class] && [json[@"schema"] integerValue]==1)manifest=json;}
    }
    NSDictionary* asset(NSString* key){id table=manifest[@"assets"];if(![table isKindOfClass:NSDictionary.class])return nil;id item=table[key];return [item isKindOfClass:NSDictionary.class]?item:nil;}
    NSString* key(const char* slug,NSString* kind){id products=manifest[@"products"];if(![products isKindOfClass:NSDictionary.class])return @"";id product=products[[NSString stringWithUTF8String:slug]];if(![product isKindOfClass:NSDictionary.class])return @"";id value=product[kind];return [value isKindOfClass:NSString.class]?value:@"";}
    NSImage* image(NSString* path){
        if(![path isKindOfClass:NSString.class] || !just::safeAssetRelativePath(path.UTF8String))return nil;
        NSString* resolved=[[root stringByAppendingPathComponent:path] stringByResolvingSymlinksInPath];NSString* base=[[root stringByResolvingSymlinksInPath] stringByAppendingString:@"/"];
        if(![resolved hasPrefix:base])return nil;
        NSData* data=[NSData dataWithContentsOfFile:resolved];if(data.length>8*1024*1024)return nil;
        NSBitmapImageRep* bitmap=data?[NSBitmapImageRep imageRepWithData:data]:nil;
        if(!bitmap || bitmap.pixelsWide>4096 || bitmap.pixelsHigh>4096)return nil;
        NSImage* result=[[NSImage alloc] initWithSize:NSMakeSize(bitmap.pixelsWide,bitmap.pixelsHigh)];[result addRepresentation:bitmap];return result;
    }
    NSColor* color(NSString* key,unsigned fallback){
        id theme=manifest[@"theme"];id text=[theme isKindOfClass:NSDictionary.class]?theme[key]:nil;
        if([text isKindOfClass:NSString.class] && [text length]==7 && [text hasPrefix:@"#"]){unsigned v=0;NSScanner* scan=[NSScanner scannerWithString:[text substringFromIndex:1]];if([scan scanHexInt:&v] && scan.isAtEnd)return justColor(v);}
        return justColor(fallback);
    }
    NSImage* icon(const char* slug){auto a=asset(key(slug,@"icon"));return image(a[@"path"]);}
};
@interface JustBackgroundView:NSView {
@public
    BOOL enabled;double phase;int frameRate;NSString* kind;NSArray<NSImage*>* frames;
}
@end
@implementation JustBackgroundView
- (BOOL)isFlipped{return YES;}
- (NSView*)hitTest:(NSPoint)p{return nil;} // artwork never owns input
- (void)drawRect:(NSRect)dirty {
    [justColor(0xf5fafb) setFill];NSRectFill(self.bounds);if(!enabled)return;
    if(frames.count){NSImage* frame=frames[std::size_t(phase*frameRate)%frames.count];[frame drawInRect:self.bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:.35 respectFlipped:YES hints:nil];return;}
    if(![kind isEqualToString:@"ambient"])return;
    CGFloat w=self.bounds.size.width,h=self.bounds.size.height;
    [justColor(0x5bdbe5,.10) setFill];[[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(w*.05+std::sin(phase/18.*M_PI*2)*25,h*.25,w*.6,h*.8)] fill];
    [justColor(0xb0dbe6,.12) setFill];[[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(w*.48,h*.03,w*.45,h*.75)] fill];
    [justColor(0x58bdc6,.14) setStroke];auto ring=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(w*.15+std::sin(phase/18.*M_PI*2)*25,h*.1,w*.7,h*.8)];ring.lineWidth=1;[ring stroke];
}
@end
