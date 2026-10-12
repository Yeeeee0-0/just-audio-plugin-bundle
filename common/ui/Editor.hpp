#pragma once
#include "../vst3/Controller.hpp"
#include "NativeEditor.hpp"
#if defined(_WIN32)
#include "pluginterfaces/base/keycodes.h"
#endif
namespace just {
class Editor final:public Steinberg::Vst::EditorView {
    Controller* owner;void* native=nullptr;
#if defined(_WIN32)
    bool handledKeys[256]{};
    static unsigned nativeKey(Steinberg::char16 key,Steinberg::int16 code){
        using namespace Steinberg;
        if(key==27 || code==KEY_ESCAPE)return 0x1b;
        if(key==13 || code==KEY_RETURN || code==KEY_ENTER)return 0x0d;
        switch(code){case KEY_F2:return 0x71;case KEY_HOME:return 0x24;case KEY_END:return 0x23;
        case KEY_PAGEUP:return 0x21;case KEY_PAGEDOWN:case KEY_NEXT:return 0x22;
        case KEY_LEFT:return 0x25;case KEY_UP:return 0x26;case KEY_RIGHT:return 0x27;case KEY_DOWN:return 0x28;default:return 0;}
    }
#endif
public:
    explicit Editor(Controller*);
    bool requestScale(double);
    ~Editor() override;
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString) override;
    Steinberg::tresult PLUGIN_API attached(void*,Steinberg::FIDString) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect*) override;
    Steinberg::tresult PLUGIN_API canResize() override{return Steinberg::kResultTrue;}
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect*) override;
#if defined(_WIN32)
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key,Steinberg::int16 code,Steinberg::int16 modifiers) override {
        const unsigned vk=nativeKey(key,code);
        if(vk && !(modifiers & ~Steinberg::kShiftKey)){
            // One press must not both enter and commit a numeric field, or
            // cancel the field and then close its host. Arrow repeats remain live.
            if(handledKeys[vk] && (vk==0x1b || vk==0x0d || vk==0x71))return Steinberg::kResultTrue;
            if((vk==0x1b && handleNativeModalEscape(native)) || handleNativeControlKey(native,vk)){
                handledKeys[vk]=true;return Steinberg::kResultTrue;
            }
        }
        return EditorView::onKeyDown(key,code,modifiers);
    }
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key,Steinberg::int16 code,Steinberg::int16 modifiers) override {
        const unsigned vk=nativeKey(key,code);
        if(vk && handledKeys[vk]){handledKeys[vk]=false;return Steinberg::kResultTrue;}
        return EditorView::onKeyUp(key,code,modifiers);
    }
#endif
};
}
