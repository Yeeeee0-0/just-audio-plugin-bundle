#pragma once
#include "dsp/Core.hpp"
#include "Parameters.hpp"
#include "common/vst3/Module.hpp"
namespace just::compressor {
Settings settingsFromState(const SoundState&) noexcept;
struct PendingConfiguration {
    double requestedLookaheadMs=0;
    std::uint32_t requestedMaximumOrdinal=0;
    std::uint32_t actualLatencySamples=0;
    bool pending=false;
};
class CompressorEngine final:public Engine {
    Core core;
    PendingConfiguration configuration{};
    bool firstTargets=true;
public:
    bool prepare(const PrepareSpec&) override;
    void reset(ResetReason) noexcept override;
    void applyTargets(const SoundState&,std::int32_t) noexcept override;
    void process(AudioBlock<float>,const ProcessContext&) noexcept override;
    void process(AudioBlock<double>,const ProcessContext&) noexcept override;
    void endBlock() noexcept override {core.endBlock();}
    std::uint32_t latencySamples() const noexcept override {return 0;}
    Tail tailSamples() const noexcept override {return {};}
    bool readTelemetry(Telemetry&) noexcept override;
    // Test/shared-owner integration points; no pointer to this engine is exposed
    // to an editor. These consume the same queue and require one consumer only.
    bool readCompressorMeter(MeterFrame& frame) noexcept {return core.readMeter(frame);}
    PendingConfiguration pendingConfiguration() const noexcept {return configuration;}
};
EditorContent* createCompressorEditor();
}
