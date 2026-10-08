#include "Engine.hpp"
namespace just::compressor {
Settings settingsFromState(const SoundState& s) noexcept {
    using module::physical;
    Settings p;
    p.bypass=physical(s,0)>=0.5;
    p.thresholdDb=physical(s,100);p.ratio=physical(s,101);
    p.attackMs=physical(s,102);p.releaseMs=physical(s,103);
    p.style=physical(s,104)>=0.5?Style::punch:Style::clean;
    p.detector=static_cast<Detector>(static_cast<int>(physical(s,105)));
    p.rmsBlend=physical(s,106)/100;p.kneeDb=physical(s,107);p.rangeDb=physical(s,108);
    p.inputDb=physical(s,109);p.makeupDb=physical(s,110);p.mix=physical(s,111)/100;
    p.stereoLink=physical(s,112)/100;p.externalSidechain=physical(s,113)>=0.5;
    p.sidechainDb=physical(s,114);p.highPass=physical(s,115)>=0.5;p.highPassHz=physical(s,116);
    p.lowPass=physical(s,117)>=0.5;p.lowPassHz=physical(s,118);
    return sanitized(p);
}
bool CompressorEngine::prepare(const PrepareSpec& spec) {
    if(!core.prepare(spec))return false;
    firstTargets=true;configuration={};return true;
}
void CompressorEngine::reset(ResetReason) noexcept {core.reset();}
void CompressorEngine::applyTargets(const SoundState& state,std::int32_t) noexcept {
    core.setTargets(settingsFromState(state),firstTargets);firstTargets=false;
    configuration.requestedLookaheadMs=module::physical(state,119);
    configuration.requestedMaximumOrdinal=0;
    for(std::uint32_t i=0;i<std::min<std::uint32_t>(state.configurationCount,state.configurations.size());++i)
        if(state.configurations[i].id==module::maximumLookaheadConfigurationID)
            configuration.requestedMaximumOrdinal=static_cast<std::uint32_t>(std::round(bounded(state.configurations[i].value,0,2,0)));
    configuration.actualLatencySamples=0;
    configuration.pending=configuration.requestedLookaheadMs>0 || configuration.requestedMaximumOrdinal>0;
    // Preserve sound targets/configurations in the shared processor. This engine
    // applies zero lookahead until the prepared/PDC acknowledgement bridge exists.
}
void CompressorEngine::process(AudioBlock<float> b,const ProcessContext& c) noexcept {core.process(b,c);}
void CompressorEngine::process(AudioBlock<double> b,const ProcessContext& c) noexcept {core.process(b,c);}
bool CompressorEngine::readTelemetry(Telemetry& t) noexcept {
    MeterFrame m;if(!core.readMeter(m))return false;
    t={m.inputPeak,m.outputPeak,0,m.invalidInput};return true;
}
}
