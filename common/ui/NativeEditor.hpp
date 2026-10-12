#pragma once
#include "Model.hpp"
#include "../vst3/PluginIdentities.hpp"
#include "../vst3/Module.hpp"
namespace just {
struct EditorCallbacks {
    void* owner=nullptr;
    bool (*getBypass)(void*)=nullptr;
    void (*setBypass)(void*,bool)=nullptr;
    EditorViewState* view=nullptr;
    EditorServices services{};
    StatusSnapshot (*readStatus)(void*)=nullptr;
    void* resizeOwner=nullptr;
    bool (*requestScale)(void*,double)=nullptr;
    void (*setVisualsPaused)(void*,bool)=nullptr; // display reader only
    const char* presetDirectoryOverride=nullptr; // isolated embedding/test root; null uses Application Support
};
void* createNativeEditor(void* parent,const PluginIdentity&,EditorCallbacks);
void destroyNativeEditor(void*);
void resizeNativeEditor(void*,int width,int height);
#if defined(_WIN32)
// Handle only an Escape belonging to this view's live modal. The VST3 host
// may intercept keyboard input before the native child receives WM_KEYDOWN.
bool handleNativeModalEscape(void*);
// Route only supported keys for this editor's focused native rotary/value edit.
bool handleNativeControlKey(void*,unsigned virtualKey);
#endif
}
