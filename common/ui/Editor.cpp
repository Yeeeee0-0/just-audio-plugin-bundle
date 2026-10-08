#include "Editor.hpp"
namespace just {
using namespace Steinberg;
using namespace Steinberg::Vst;
Editor::Editor(Controller* c):EditorView(c),owner(c) {
    auto limits=moduleDefinition().editorSizeLimits();
    limits.minimumWidth=std::max(EditorSizeLimits::defaultMinimumWidth,int(std::ceil(limits.minimumWidth*c->viewState.renderScale)));
    limits.minimumHeight=std::max(EditorSizeLimits::defaultMinimumHeight,int(std::ceil(limits.minimumHeight*c->viewState.renderScale)));
    limits.constrain(c->viewState.width,c->viewState.height);
    rect=ViewRect(0,0,c->viewState.width,c->viewState.height);
}
Editor::~Editor(){if(native){destroyNativeEditor(native);owner->editorRemoved();}}
tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type) {
    if(!type)return kResultFalse;
#if defined(_WIN32)
    return std::strcmp(type,kPlatformTypeHWND)==0?kResultTrue:kResultFalse;
#else
    return std::strcmp(type,kPlatformTypeNSView)==0?kResultTrue:kResultFalse;
#endif
}
tresult PLUGIN_API Editor::attached(void* parent,FIDString type) {
    if(!parent || native || isPlatformTypeSupported(type)!=kResultTrue)return kInvalidArgument;
    EditorCallbacks callbacks;callbacks.owner=owner;callbacks.view=&owner->viewState;
    callbacks.resizeOwner=this;callbacks.requestScale=[](void* p,double scale){return static_cast<Editor*>(p)->requestScale(scale);};
    callbacks.getBypass=[](void* p){return static_cast<Controller*>(p)->getParamNormalized(bypassParamID)>=0.5;};
    callbacks.setBypass=[](void* p,bool on){VstEditSink sink(*static_cast<Controller*>(p));EditGesture edit(sink,bypassParamID);edit.update(on?1:0);};
    callbacks.setVisualsPaused=[](void* p,bool paused){static_cast<Controller*>(p)->setVisualsPaused(paused);};
    callbacks.services.owner=owner;callbacks.services.view=&owner->viewState;
    callbacks.services.readTarget=[](void* p,ParamID id){return static_cast<Controller*>(p)->getParamNormalized(id);};
    callbacks.services.beginEdit=[](void* p,ParamID id){return static_cast<Controller*>(p)->beginEdit(id)==kResultOk;};
    callbacks.services.performEdit=[](void* p,ParamID id,double value){VstEditSink sink(*static_cast<Controller*>(p));return sink.performEdit(id,value);};
    callbacks.services.endEdit=[](void* p,ParamID id){static_cast<Controller*>(p)->endEdit(id);};
    callbacks.services.readBusLayout=[](void* p,BusLayoutSnapshot& out){return static_cast<Controller*>(p)->readBusLayout(out);};
    callbacks.services.readRuntimeTelemetry=[](void* p,RuntimeTelemetrySnapshot& out){return static_cast<Controller*>(p)->readRuntimeTelemetry(out);};
    callbacks.services.readAnalysis=[](void* p,AnalysisCursor& cursor,AnalysisBatch& out){return static_cast<Controller*>(p)->readAnalysis(cursor,out);};
    callbacks.services.readSpectrum=[](void* p,SpectrumSnapshot& out){return static_cast<Controller*>(p)->readSpectrum(out);};
    callbacks.services.readSamples=[](void* p,SampleFrame& out){return static_cast<Controller*>(p)->readSamples(out);};
    if(owner->supportsExtendedSpectrum()){
        callbacks.services.readExtendedSpectrum=[](void* p,ExtendedSpectrumSnapshot& out){return static_cast<Controller*>(p)->readExtendedSpectrum(out);};
        callbacks.services.readAnalyzerPreferences=[](void* p,AnalyzerPreferences& out){return static_cast<Controller*>(p)->readAnalyzerPreferences(out);};
        callbacks.services.writeAnalyzerPreferences=[](void* p,const AnalyzerPreferences& value){return static_cast<Controller*>(p)->writeAnalyzerPreferences(value);};
    }
    callbacks.services.readCompleteSoundState=[](void* p,SoundState& out){return static_cast<Controller*>(p)->readCompleteSoundState(out);};
    callbacks.services.capturePresetState=[](void* p,SoundState& out){return static_cast<Controller*>(p)->capturePresetState(out);};
    callbacks.services.requestApplySoundState=[](void* p,const SoundState& s){return static_cast<Controller*>(p)->requestApplySoundState(s);};
    callbacks.services.readPresetTransaction=[](void* p){return static_cast<Controller*>(p)->readPresetTransaction();};
    callbacks.services.canUndoLastPreset=[](void* p){return static_cast<Controller*>(p)->canUndoLastPreset();};
    callbacks.services.undoLastPreset=[](void* p){return static_cast<Controller*>(p)->undoLastPreset();};
    callbacks.services.beginAudition=[](void* p,ParamID target){return static_cast<Controller*>(p)->beginAudition(target);};
    callbacks.services.renewAudition=[](void* p,std::uint64_t token){return static_cast<Controller*>(p)->renewAudition(token);};
    callbacks.services.endAudition=[](void* p,std::uint64_t token){static_cast<Controller*>(p)->endAudition(token);};
    callbacks.services.readAuditionStatus=[](void* p){return static_cast<Controller*>(p)->readAuditionStatus();};
    callbacks.readStatus=[](void* p) {
        auto& module=moduleDefinition();SoundState targets;
        for(std::size_t i=0;i<module.parameters.count;++i)targets.targets[i]=static_cast<Controller*>(p)->getParamNormalized(module.parameters.specs[i].id);
        return moduleStatus(module,targets);
    };
    native=createNativeEditor(parent,pluginIdentities[JUST_PLUGIN_INDEX],callbacks);
    if(!native)return kResultFalse;
    owner->editorAttached();
    resizeNativeEditor(native,rect.getWidth(),rect.getHeight());
    return EditorView::attached(parent,type);
}
bool Editor::requestScale(double scale) {
    if(!plugFrame || !std::isfinite(scale) || scale<.75 || scale>1.5)return false;
    const auto previous=owner->viewState;const auto oldRect=rect;
    owner->viewState.renderScale=scale;
    ViewRect proposed(0,0,int(std::lround(rect.getWidth()*scale/previous.renderScale)),int(std::lround(rect.getHeight()*scale/previous.renderScale)));
    // A hard native minimum may clamp the proportional size. Offer the legal
    // rectangle to the host and retain the staged scale only after acceptance.
    if(checkSizeConstraint(&proposed)!=kResultTrue || plugFrame->resizeView(this,&proposed)!=kResultTrue ||
       onSize(&proposed)!=kResultOk){owner->viewState=previous;rect=oldRect;if(native)resizeNativeEditor(native,rect.getWidth(),rect.getHeight());return false;}
    owner->viewState.scale=scale;return true;
}
tresult PLUGIN_API Editor::removed() {
    if(native){destroyNativeEditor(native);native=nullptr;owner->editorRemoved();}
    return EditorView::removed();
}
tresult PLUGIN_API Editor::checkSizeConstraint(ViewRect* r) {
    if(!r)return kInvalidArgument;
    // Widen before subtraction/addition: host rectangles can contain any int32.
    const auto width=int64(r->right)-r->left,height=int64(r->bottom)-r->top;
    if(width<0 || height<0)return kInvalidArgument;
    auto limits=moduleDefinition().editorSizeLimits();
    limits.minimumWidth=std::max(EditorSizeLimits::defaultMinimumWidth,int(std::ceil(limits.minimumWidth*owner->viewState.renderScale)));
    limits.minimumHeight=std::max(EditorSizeLimits::defaultMinimumHeight,int(std::ceil(limits.minimumHeight*owner->viewState.renderScale)));
    const auto right=int64(r->left)+std::clamp(width,int64(limits.minimumWidth),int64(EditorSizeLimits::maximumWidth));
    const auto bottom=int64(r->top)+std::clamp(height,int64(limits.minimumHeight),int64(EditorSizeLimits::maximumHeight));
    if(right>std::numeric_limits<int32>::max() || bottom>std::numeric_limits<int32>::max())return kInvalidArgument;
    r->right=static_cast<int32>(right);r->bottom=static_cast<int32>(bottom);
    return kResultTrue;
}
tresult PLUGIN_API Editor::onSize(ViewRect* r) {
    if(checkSizeConstraint(r)!=kResultTrue)return kInvalidArgument;
    const auto result=EditorView::onSize(r);if(result!=kResultOk)return result;
    owner->viewState.width=rect.getWidth();owner->viewState.height=rect.getHeight();
    if(native)resizeNativeEditor(native,rect.getWidth(),rect.getHeight());return kResultOk;
}
}
