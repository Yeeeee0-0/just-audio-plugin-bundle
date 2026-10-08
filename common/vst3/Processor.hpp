#pragma once
#include "PluginIdentities.hpp"
#include "Module.hpp"
#include "TelemetryTransport.hpp"
#include "AnalysisTransport.hpp"
#include "../runtime/AnalysisCollector.hpp"
#include "../dsp/Engine.hpp"
#include "../parameters/Automation.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include <mutex>
namespace just {
inline const PluginIdentity& identity(){return pluginIdentities[JUST_PLUGIN_INDEX];}
class Processor final:public Steinberg::Vst::AudioEffect,public Steinberg::ITimerCallback {
    ParameterRegistry registry=moduleDefinition().parameters;
    SoundState current=initialState(identity().processor,registry);
    std::unique_ptr<Engine> engine;
    TelemetryTransport telemetryTransport{*this};
    AnalysisTransport analysisTransport{*this};
    AnalysisCollector analysis;
    std::atomic<bool> analysisRequested{false};
    AtomicSnapshot<PresetRequest> presetPending;
    AtomicSnapshot<PresetPublication> presetPublication;
    std::atomic<std::uint64_t> requestedPreset{0};
    PresetPublication presetAudio{};
    std::uint64_t seenPreset=0,lastPresetRequest=0;
    Steinberg::IPtr<Steinberg::Timer> stateTimer;
    AtomicSnapshot<AuditionRequest> auditionPending;
    AtomicSnapshot<AuditionStatus> auditionPublication;
    AuditionRequest auditionMain{},auditionAudio{};
    AuditionStatus auditionState{};
    std::uint64_t auditionDeadline=0,auditionRemaining=0;
    void updateAudition() noexcept;
    void endAudioAudition() noexcept;
    Steinberg::tresult handleAuditionMessage(Steinberg::Vst::IMessage*);
    void sendPresetPublication(const PresetPublication&);
    Steinberg::tresult handlePresetMessage(Steinberg::Vst::IMessage*);
    std::atomic<std::uint64_t> telemetrySession{0};
    std::uint64_t telemetrySequence=0;
    std::uint32_t telemetrySamples=0;
    void publishTelemetry(const ProcessContext&,std::uint32_t) noexcept;
    Automation automation{registry};
    PrepareSpec prepared{};
    AtomicSnapshot<SoundState> activePublication,pendingPublication;
    std::atomic<std::uint64_t> latestRestore{0};
    std::mutex stateCalls; // only getState/setState; process never acquires it
    std::uint64_t nextRevision=0;
    bool firstActivation=true,havePosition=false;
    bool layoutPrepared=false,layoutActive=false;
    std::uint64_t layoutSequence=0,layoutSession=0,lastLayoutSession=0;
    void publishLayout(bool unavailable=false); // non-RT only
    std::int64_t nextProjectSample=0;
    ProcessContext contextFor(const Steinberg::Vst::ProcessData&) noexcept;
    template<class Sample> void render(Steinberg::Vst::ProcessData&,const ProcessContext&) noexcept;
public:
    void onTimer(Steinberg::Timer*) override;
    Processor();
    static Steinberg::FUnknown* createInstance(void*) {return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);}
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*,Steinberg::int32,Steinberg::Vst::SpeakerArrangement*,Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType,Steinberg::Vst::BusDirection,Steinberg::int32,Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint*) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override{return engine?engine->latencySamples():0;}
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
};
}
