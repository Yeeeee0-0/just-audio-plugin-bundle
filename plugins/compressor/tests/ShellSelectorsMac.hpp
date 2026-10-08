#pragma once
#import <Cocoa/Cocoa.h>

// Read-only test selectors for the frozen common shell. The view button changes
// its displayed action with the current mode and all titles follow UI language.
static NSButton* button(NSView* root,NSString* name) {
    NSArray<NSString*>* titles=@[name];
    if([name isEqualToString:@"Advanced"])titles=@[@"Advanced",@"Simple",@"高级",@"简易"];
    else if([name isEqualToString:@"Bypass"])titles=@[@"Bypass",@"● Bypass",@"● 旁路"];
    else if([name isEqualToString:@"Preset"])titles=@[@"Preset",@"Preset ▾",@"预设 ▾"];
    for(NSView* view in root.subviews) {
        if([view isKindOfClass:NSButton.class] && [titles containsObject:[(NSButton*)view title]])return (NSButton*)view;
        if(auto* nested=button(view,name))return nested;
    }
    return nil;
}
