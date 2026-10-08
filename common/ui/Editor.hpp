#pragma once
#include "../vst3/Controller.hpp"
#include "NativeEditor.hpp"
namespace just {
class Editor final:public Steinberg::Vst::EditorView {
    Controller* owner;void* native=nullptr;
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
};
}
