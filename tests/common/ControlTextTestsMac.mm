// Windowless AppKit cell/layout regression; not native keyboard or focus acceptance.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "common/ui/Controls.hpp"
#include <iostream>

static unsigned checks=0,failures=0;
static const char* stage="startup";
static id forbidWindow(id cls,SEL,...){std::cerr<<"FAIL attempted window allocation "<<class_getName(cls)<<" during "<<stage<<"\n";std::exit(2);}
static void check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<message<<"\n";}}
// An unattached editor has no OS input session. Suppress AppKit's otherwise
// lazy TUINSWindow for input methods; leave cell, layout and navigation intact.
@interface WindowlessFieldEditor:NSTextView
@end
@implementation WindowlessFieldEditor
- (NSTextInputContext*)inputContext{return nil;}
@end
struct Sink {
    double value=0;unsigned starts=0,writes=0,ends=0;
    just::EditorServices services(){just::EditorServices s;s.owner=this;
        s.readTarget=[](void* p,just::ParamID){return static_cast<Sink*>(p)->value;};
        s.beginEdit=[](void* p,just::ParamID){++static_cast<Sink*>(p)->starts;return true;};
        s.performEdit=[](void* p,just::ParamID,double n){auto& s=*static_cast<Sink*>(p);++s.writes;s.value=n;return true;};
        s.endEdit=[](void* p,just::ParamID){++static_cast<Sink*>(p)->ends;};return s;
    }
};
static void textCase(int width,int height,double physical,just::DisplayPolicy policy={},const just::ParameterSpec* selected=nullptr){
    const just::ParameterSpec spec=selected?*selected:just::ParameterSpec{"fixture.gain",1,"Gain","dB",-18,18,0,just::Mapping::linear,0,true,"all",just::Transition::continuous,0};
    Sink sink;sink.value=spec.toNormalized(physical);const double original=sink.value;
    stage="control creation";
    NSView* parent=[[NSView alloc] initWithFrame:NSMakeRect(0,0,width,height)];
    auto control=just::RotaryControl::create((__bridge void*)parent,sink.services(),spec,policy);
    check(bool(control),"create production common control without a window");if(!control)return;
    control->resize(0,0,width,height);NSView* view=(__bridge NSView*)control->nativeHandle();
    NSTextField *field=nil,*unit=nil,*label=nil;
    for(NSView* child in view.subviews)if([child isKindOfClass:NSTextField.class]){
        auto* text=(NSTextField*)child;if(text.isEditable)field=text;else{
            if(!label && [text.stringValue isEqualToString:[NSString stringWithUTF8String:spec.title]])label=text;
            if([text.stringValue isEqualToString:[NSString stringWithUTF8String:spec.unit]])unit=text;
        }
    }
    check(field && unit && label,"numeric field, label and independent unit exist");if(!field || !unit || !label)return;
    const NSRect valueFrame=field.frame,unitFrame=unit.frame;const CGFloat fontSize=field.font.pointSize;
    const bool custom=policy.valueFontSize>0;
    check(valueFrame.size.width==width-30 && valueFrame.size.height==(custom?28:22),"default or opted-in readout height matches contract");
    check(fontSize==(custom?20:width==78?12:15) && label.font.pointSize==(custom?13:11),"numeric and label fonts honor opt-in and preserve legacy defaults");
    const auto geometry=just::ControlGeometry::layout(width,height,policy);
    check(valueFrame.origin.y==geometry.valueTop && valueFrame.size.height==geometry.valueHeight && NSMaxY(valueFrame)<=height,"public layout matches native readout and fits control bounds");
    if(!custom)check(valueFrame.origin.y==24+std::min(double(width-14),double(height-46)) && unit.frame.origin.y==valueFrame.origin.y+4,"unset policy preserves exact7d default geometry");
    check(NSMaxX(valueFrame)<=NSMinX(unitFrame) && !NSIntersectsRect(valueFrame,unitFrame),"number and unit areas do not overlap");

    // Invoke only the production formatting/refresh lifecycle. OS focus, Enter,
    // Escape and Tab remain the separately scheduled native QA responsibility.
    id<NSTextFieldDelegate> delegate=field.delegate;
    [delegate controlTextDidBeginEditing:[NSNotification notificationWithName:NSControlTextDidBeginEditingNotification object:field]];
    char expected[128];std::snprintf(expected,sizeof(expected),"%.17g",spec.toPhysical(original));
    NSString* precise=[NSString stringWithUTF8String:expected];
    check([field.stringValue isEqualToString:precise] && precise.length>=18,"focus preparation preserves complete positive/negative 17-digit precision");
    check([precise sizeWithAttributes:@{NSFontAttributeName:field.font}].width>field.bounds.size.width,"long test value exceeds the visible field width");
    NSTextFieldCell* cell=(NSTextFieldCell*)field.cell;
    check(cell.usesSingleLineMode && !cell.wraps && cell.isScrollable,"production numeric cell is single line, unwrapped and scrollable");

    // Let the actual NSCell configure an NSTextView and clipping container.
    // No custom wrapping/scrolling setup is applied by the fixture.
    stage="text view creation";
    NSTextView* editor=[[WindowlessFieldEditor alloc] initWithFrame:field.bounds];editor.fieldEditor=YES;
    stage="cell selectWithFrame";
    [cell selectWithFrame:field.bounds inView:field editor:editor delegate:nil start:0 length:precise.length];
    check([editor.string isEqualToString:precise],"AppKit editor receives the entire precise string");
    check(NSEqualRanges(editor.selectedRange,NSMakeRange(0,precise.length)),"all digits and sign can be selected");
    check(editor.isHorizontallyResizable,"AppKit field editor supports horizontal expansion");
    [editor.layoutManager ensureLayoutForTextContainer:editor.textContainer];
    __block NSUInteger lines=0;
    [editor.layoutManager enumerateLineFragmentsForGlyphRange:NSMakeRange(0,editor.layoutManager.numberOfGlyphs)
        usingBlock:^(NSRect,NSRect,NSTextContainer*,NSRange,BOOL*){++lines;}];
    check(lines==1,"positive/negative long value occupies exactly one text line");
    if(custom)check(editor.font.pointSize==20 && [editor.layoutManager usedRectForTextContainer:editor.textContainer].size.height<=valueFrame.size.height,"actual20-point field-editor line fits enlarged readout");
    stage="text navigation";
    [editor moveToBeginningOfDocument:nil];[editor scrollRangeToVisible:editor.selectedRange];
    const NSRect startVisible=editor.visibleRect;
    check(NSEqualRanges(editor.selectedRange,NSMakeRange(0,0)),"caret can navigate to the first character");
    [editor moveToEndOfDocument:nil];[editor scrollRangeToVisible:editor.selectedRange];
    const NSRect endVisible=editor.visibleRect;
    const NSRect viewport=[editor convertRect:endVisible toView:view];
    check(NSEqualRanges(editor.selectedRange,NSMakeRange(precise.length,0)),"caret can navigate beyond the final character");
    check(endVisible.origin.x>startVisible.origin.x,"navigating to the end scrolls the long string horizontally");
    check(NSMaxX(viewport)<=NSMinX(unitFrame) && !NSIntersectsRect(viewport,unitFrame),"AppKit padded scroll viewport stays separate from the unit");
    [editor selectAll:nil];
    check(NSEqualRanges(editor.selectedRange,NSMakeRange(0,precise.length)) && [editor.string isEqualToString:precise],"complete string remains selectable after navigation");
    for(int i=0;i<10;++i)control->refresh();
    check([field.stringValue isEqualToString:precise] && [editor.string isEqualToString:precise],"refresh path cannot overwrite active precise text");
    check(NSEqualRects(field.frame,valueFrame) && NSEqualRects(unit.frame,unitFrame) && field.font.pointSize==fontSize,"editing and refresh preserve field, unit and font layout");
    [cell endEditing:editor];
    [delegate controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:field]];
    check(sink.value==original && sink.starts==0 && sink.writes==0 && sink.ends==0,"unchanged editing and refresh emit no host gestures or writes");
    check(!parent.window && !view.window && !field.window && !editor.window,"entire regression remains windowless");
    std::cout<<"CASE "<<width<<"x"<<height<<" precise="<<expected<<" field="<<valueFrame.size.width<<"x"<<valueFrame.size.height
             <<" font="<<fontSize<<" lines="<<lines<<" scrollable="<<cell.isScrollable
             <<" visibleX="<<startVisible.origin.x<<"->"<<endVisible.origin.x<<" viewportRight="<<NSMaxX(viewport)<<" unitLeft="<<NSMinX(unitFrame)<<"\n";
}
int main(){@autoreleasepool {
    // Fail before AppKit can allocate any window, including private helpers.
    for(SEL selector:{@selector(alloc),@selector(allocWithZone:)}){
        auto method=class_getClassMethod(NSWindow.class,selector);
        class_replaceMethod(object_getClass(NSWindow.class),selector,(IMP)forbidWindow,method_getTypeEncoding(method));
    }
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    for(auto size:{std::pair<int,int>{112,148},{78,108}})for(double value:{2.8228567857142863,-2.8228567857142863})textCase(size.first,size.second,value);
    just::DisplayPolicy typography;typography.valueFontSize=20;typography.labelFontSize=13;typography.valueFieldHeight=28;
    for(double value:{2.8228567857142863,-2.8228567857142863})textCase(112,108,value,typography);
    const just::ParameterSpec frequency{"fixture.frequency",2,"Frequency","Hz",20,20000,1000,just::Mapping::logarithmic,0,true,"all",just::Transition::continuous,0};
    const just::ParameterSpec q{"fixture.q",3,"Q","Q",.1,18,.7,just::Mapping::logarithmic,0,true,"all",just::Transition::continuous,0};
    textCase(112,108,454.31234567890123,typography,&frequency);textCase(112,108,.71234567890123456,typography,&q);
    check(NSApp.windows.count==0,"no NSWindow created");
    std::cout<<(failures?"FAIL ":"PASS ")<<checks<<" windowless numeric text/layout checks; failures="<<failures<<"\n";
    return failures?1:0;
}}
