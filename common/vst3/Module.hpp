#pragma once
#include "../dsp/Engine.hpp"
#include "../ui/Model.hpp"
#include "../runtime/Layout.hpp"
#include "../state/Preset.hpp"
#include "../ui/AnalyzerPreferences.hpp"
#include "../runtime/ExtendedSpectrum.hpp"
#include <memory>
namespace just {
// The native title bar owns an empty content parent. Module UI can use Cocoa/Win32
// inside that parent without owning sound state or changing the shared shell.
struct EditorServices {
    void* owner=nullptr;
    EditorViewState* view=nullptr;
    double (*readTarget)(void*,ParamID)=nullptr;
    bool (*beginEdit)(void*,ParamID)=nullptr;
    bool (*performEdit)(void*,ParamID,double)=nullptr;
    void (*endEdit)(void*,ParamID)=nullptr;
    // Optional UI-only copy. False means unknown/unconnected; never guess stereo.
    bool (*readBusLayout)(void*,BusLayoutSnapshot&)=nullptr;
    TelemetryAvailability (*readRuntimeTelemetry)(void*,RuntimeTelemetrySnapshot&)=nullptr;
    AnalysisAvailability (*readAnalysis)(void*,AnalysisCursor&,AnalysisBatch&)=nullptr;
    AnalysisAvailability (*readSpectrum)(void*,SpectrumSnapshot&)=nullptr;
    AnalysisAvailability (*readSamples)(void*,SampleFrame&)=nullptr;
    bool (*readCompleteSoundState)(void*,SoundState&)=nullptr;
    bool (*requestApplySoundState)(void*,const SoundState&)=nullptr;
    PresetTransactionStatus (*readPresetTransaction)(void*)=nullptr;
    bool (*canUndoLastPreset)(void*)=nullptr;
    bool (*undoLastPreset)(void*)=nullptr;
    std::uint64_t (*beginAudition)(void*,ParamID)=nullptr;
    bool (*renewAudition)(void*,std::uint64_t)=nullptr;
    void (*endAudition)(void*,std::uint64_t)=nullptr;
    AuditionStatus (*readAuditionStatus)(void*)=nullptr;
    // Coherent audio-published state, including configurations/seed. False while
    // a pending edit or preset has not reached the processor; caller may retry.
    bool (*capturePresetState)(void*,SoundState&)=nullptr;
    // Optional EQ analyzer services, UI thread only. Null on other modules.
    // Setting valid preferences persists in the controller UI chunk; changing
    // fftSize invalidates prior/partial FFT results without a DSP/parameter write.
    AnalysisAvailability (*readExtendedSpectrum)(void*,ExtendedSpectrumSnapshot&)=nullptr;
    bool (*readAnalyzerPreferences)(void*,AnalyzerPreferences&)=nullptr;
    bool (*writeAnalyzerPreferences)(void*,const AnalyzerPreferences&)=nullptr;
};
class EditorContent {
public:
    virtual ~EditorContent()=default; // UI thread only
    virtual bool attach(void* nativeContentParent,const EditorServices&)=0;
    virtual void resize(int width,int height)=0;
    virtual void refresh(const EditorViewState&,const StatusSnapshot&)=0;
    // UI thread, after attach and before first refresh, then on each transition.
    // Refresh and all editing services remain live. Pause only presentation
    // clocks/animations and telemetry-derived displays, never audio or leases.
    virtual void setVisualsPaused(bool) {}
};
struct BusPolicy {
    bool mono=true,stereo=true,monoToStereo=false,optionalSidechain=false;
};
struct ModuleDefinition {
    ParameterRegistry parameters;
    Engine* (*createEngine)()=nullptr; // setup/UI thread allocation; processor owns result
    const SimpleControlBinding* simpleControls=nullptr;
    std::size_t simpleControlCount=0;
    const AdvancedGroup* advancedGroups=nullptr;
    std::size_t advancedGroupCount=0;
    EditorContent* (*createEditorContent)()=nullptr;
    // UI thread status from true controller targets; audio telemetry requires a
    // separately reviewed bounded bridge, never a processor raw pointer.
    StatusSnapshot (*status)(const SoundState&)=nullptr;
    BusPolicy buses{};
    // Tail additions preserve old aggregate initializers when recompiling.
    const char* bypassTooltip=nullptr; // static UTF-8, UI lifetime
    bool (*simpleHasCustom)(const SoundState&)=nullptr; // pure UI target read
    const FactoryPreset* factoryPresets=nullptr;
    std::size_t factoryPresetCount=0;
    bool (*validatePresetState)(const SoundState&)=nullptr; // non-RT reject unsupported configurations
    bool (*validateAuditionTarget)(ParamID)=nullptr;
    // UI-only outer editor size, including the common header. Zero independently
    // retains the legacy 480/260 floor. Native ViewRect units, never sound state.
    int minimumEditorWidth=0,minimumEditorHeight=0;
    // Fresh controller only. Zero independently preserves the identity default;
    // restoring saved UI dimensions never re-applies these preferred sizes.
    int defaultEditorWidth=0,defaultEditorHeight=0;
    // EQ opts in explicitly. False preserves the existing 2048 FFT and UI v2.
    // One existing analysis worker per controller: legacy OR extended, never both.
    bool extendedSpectrum=false;
    EditorSizeLimits editorSizeLimits() const noexcept {
        return {minimumEditorWidth?std::clamp(minimumEditorWidth,EditorSizeLimits::defaultMinimumWidth,EditorSizeLimits::maximumWidth):EditorSizeLimits::defaultMinimumWidth,
                minimumEditorHeight?std::clamp(minimumEditorHeight,EditorSizeLimits::defaultMinimumHeight,EditorSizeLimits::maximumHeight):EditorSizeLimits::defaultMinimumHeight};
    }
    bool valid() const noexcept {
        if((minimumEditorWidth && (minimumEditorWidth<EditorSizeLimits::defaultMinimumWidth || minimumEditorWidth>EditorSizeLimits::maximumWidth)) ||
           (minimumEditorHeight && (minimumEditorHeight<EditorSizeLimits::defaultMinimumHeight || minimumEditorHeight>EditorSizeLimits::maximumHeight)))return false;
        const auto limits=editorSizeLimits();
        if((defaultEditorWidth && (defaultEditorWidth<limits.minimumWidth || defaultEditorWidth>EditorSizeLimits::maximumWidth)) ||
           (defaultEditorHeight && (defaultEditorHeight<limits.minimumHeight || defaultEditorHeight>EditorSizeLimits::maximumHeight)))return false;
        if(!parameters.valid() || !createEngine || simpleControlCount>32 ||
           (simpleControlCount && !simpleControls) || (advancedGroupCount && !advancedGroups))return false;
        const auto bypass=parameters.index(bypassParamID);
        if(factoryPresetCount>256 || (factoryPresetCount && !factoryPresets))return false;
        for(std::size_t i=0;i<factoryPresetCount;++i){const auto& p=factoryPresets[i];if(!p.key || !*p.key || !p.name || !p.category || !p.build)return false;for(std::size_t j=0;j<i;++j)if(!std::strcmp(p.key,factoryPresets[j].key))return false;}
        if(bypass==parameters.count || parameters.specs[bypass].stepCount!=1)return false;
        for(std::size_t i=0;i<simpleControlCount;++i) {
            const auto& binding=simpleControls[i];
            if(binding.parameterCount>binding.parameters.size())return false;
            for(std::size_t p=0;p<binding.parameterCount;++p)
                if(parameters.index(binding.parameters[p])==parameters.count)return false;
        }
        return true;
    }
};
inline StatusSnapshot moduleStatus(const ModuleDefinition& m,const SoundState& targets) {
    auto status=m.status?m.status(targets):StatusSnapshot{};
    status.advancedCustom=m.simpleHasCustom?m.simpleHasCustom(targets):
        hiddenParametersCustom(m.parameters,targets,m.simpleControls,m.simpleControlCount);
    return status;
}
inline const char* moduleBypassTooltip(const ModuleDefinition& m) noexcept {
    return m.bypassTooltip?m.bypassTooltip:"Bypass uses the effect-defined transition and latency; host parameter ID 0";
}
inline std::size_t effectiveFactoryPresetCount(const ModuleDefinition& m) noexcept {
    return m.factoryPresetCount?m.factoryPresetCount:1;
}
inline bool buildFactoryPreset(const ModuleDefinition& m,std::size_t index,Uid uid,SoundState& out) {
    if(index>=effectiveFactoryPresetCount(m))return false;
    if(m.factoryPresetCount)return m.factoryPresets[index].build(uid,out);
    out=initialState(uid,m.parameters);return true;
}
// Exactly one implementation per plugins/<slug>/Module.cpp; linked into its
// own VST3 and tests. This is the plug-in task's injection point.
const ModuleDefinition& moduleDefinition();
}
