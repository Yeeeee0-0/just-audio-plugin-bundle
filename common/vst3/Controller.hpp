#pragma once
#include "PluginIdentities.hpp"
#include "Module.hpp"
#include "../ui/Model.hpp"
#include "../runtime/AnalysisReader.hpp"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <mutex>
#include "public.sdk/source/vst/utility/dataexchange.h"
namespace just {
class Controller final:public Steinberg::Vst::EditController,public Steinberg::Vst::IDataExchangeReceiver {
    ParameterRegistry registry=moduleDefinition().parameters;
    AtomicSnapshot<BusLayoutSnapshot> layoutPublication;
    struct ReceivedTelemetry {RuntimeTelemetrySnapshot frame{};std::uint64_t receivedNanoseconds=0;};
    AtomicSnapshot<ReceivedTelemetry> telemetryPublication;
    Steinberg::Vst::DataExchangeReceiverHandler telemetryReceiver{this};
    std::uint32_t telemetryContext=0,lastTelemetryContext=0;
    std::uint64_t lastTelemetrySequence=0;
    std::mutex runtimeWrites; // non-audio notify/lifecycle writers only
    bool layoutConnected=false;
    const bool hasExtendedSpectrum=moduleDefinition().extendedSpectrum;
    AnalysisReader analysis{hasExtendedSpectrum};
    AnalyzerPreferences analyzerPreferences{};
    unsigned editorCount=0;
    SoundState completeState{},presetBefore{},presetDesired{},undoState{},undoAfter{};
    std::uint64_t completeEpoch=0,nextPreset=0,pendingPreset=0;
    std::array<std::uint64_t,maxParameters> targetRevisions{},presetTargetRevisions{};
    PresetTransactionStatus presetStatus=PresetTransactionStatus::unavailable;
    bool hasComplete=false,undoValid=false,pendingUndo=false,applyingPresetNotification=false;
    AuditionStatus audition{};
    bool sendAudition(std::uint64_t,ParamID,unsigned);
    void queryCompleteState();
    Steinberg::tresult receivePresetState(Steinberg::Vst::IMessage*);
    void sendAnalysisSubscription();
    std::uint64_t lastLayoutSequence=0,layoutSession=0;
public:
    Controller();
    std::uint64_t beginAudition(ParamID);
    bool renewAudition(std::uint64_t);
    void endAudition(std::uint64_t);
    AuditionStatus readAuditionStatus() const noexcept{return audition;}
    void cancelAudition(){if(audition.token)endAudition(audition.token);}
    bool readCompleteSoundState(SoundState&);
    bool capturePresetState(SoundState&);
    void setVisualsPaused(bool paused){analysis.setPresentationPaused(paused);}
    bool requestApplySoundState(const SoundState&);
    PresetTransactionStatus readPresetTransaction() const noexcept{return presetStatus;}
    bool canUndoLastPreset() noexcept;
    bool undoLastPreset();
    void editorAttached();
    void editorRemoved();
    AnalysisAvailability readAnalysis(AnalysisCursor&,AnalysisBatch&) const noexcept;
    AnalysisAvailability readSpectrum(SpectrumSnapshot&) const noexcept;
    AnalysisAvailability readSamples(SampleFrame&) const noexcept;
    bool supportsExtendedSpectrum() const noexcept{return hasExtendedSpectrum;}
    AnalysisAvailability readExtendedSpectrum(ExtendedSpectrumSnapshot&) const noexcept;
    bool readAnalyzerPreferences(AnalyzerPreferences& out) const noexcept;
    bool writeAnalyzerPreferences(const AnalyzerPreferences&) noexcept;
    OBJ_METHODS(Controller,Steinberg::Vst::EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditController)
    REFCOUNT_METHODS(Steinberg::Vst::EditController)
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID,Steinberg::uint32,Steinberg::TBool&) override;
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(Steinberg::Vst::DataExchangeUserContextID,Steinberg::uint32,Steinberg::Vst::DataExchangeBlock*,Steinberg::TBool) override;
    TelemetryAvailability readRuntimeTelemetry(RuntimeTelemetrySnapshot&) const noexcept;
    EditorViewState viewState{pluginIdentities[JUST_PLUGIN_INDEX].width,pluginIdentities[JUST_PLUGIN_INDEX].height,1,false};
    static Steinberg::FUnknown* createInstance(void*) {return static_cast<Steinberg::Vst::IEditController*>(new Controller);}
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID,Steinberg::Vst::ParamValue) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint*) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    bool readBusLayout(BusLayoutSnapshot&) const noexcept;
};
class VstEditSink final:public EditSink {
    Controller& controller;
public:
    explicit VstEditSink(Controller& c):controller(c){}
    bool beginEdit(ParamID id) override{return controller.beginEdit(id)==Steinberg::kResultOk;}
    bool performEdit(ParamID id,double v) override {
        if(controller.setParamNormalized(id,v)!=Steinberg::kResultOk)return false;
        return controller.performEdit(id,v)==Steinberg::kResultOk;
    }
    void endEdit(ParamID id) override{controller.endEdit(id);}
};
}
