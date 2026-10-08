#include "../FlangerEngine.hpp"
#include "common/vst3/Processor.hpp"
#include "common/vst3/StateStreams.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <cstdlib>
#include <iostream>
#include <chrono>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace f = just::flanger;
void check(bool good, const char* label) {
    if (!good) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
just::SoundState saved(just::Processor* processor) {
    MemoryStream bytes;
    check(processor->getState(&bytes) == kResultOk, "save component state");
    bytes.seek(0, IBStream::kIBSeekSet, nullptr);
    just::SoundState state;
    check(just::readSoundState(&bytes, just::identity().processor, f::registry, state), "decode complete state");
    return state;
}
struct Point { int offset; double value; };
struct Curve { just::ParamID id; std::vector<Point> points; };
// Independent host-curve expectation: continuous interpolation from offset -1,
// discrete selection only at the event. Does not call the common Automation class.
double at(const Curve& curve, int sample, double initial, bool discrete) {
    int previousOffset = -1;
    double previous = initial;
    for (const auto& point : curve.points) {
        if (point.offset > sample) {
            return discrete ? previous : previous + (point.value - previous) *
                (double(sample - previousOffset) / (point.offset - previousOffset));
        }
        previousOffset = point.offset; previous = point.value;
    }
    return previous;
}
template<class Sample> void bridgeMatrix() {
    auto* processor = new just::Processor;
    check(processor->initialize(nullptr) == kResultOk, "initialize bridge");
    for(double sampleRate : {44100.,48000.,96000.,192000.}) for (unsigned channels : {1u, 2u}) {
        SpeakerArrangement arrangement = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        check(processor->setBusArrangements(&arrangement, 1, &arrangement, 1) == kResultOk, "mono/stereo bridge");
        ProcessSetup setup{};
        setup.sampleRate = sampleRate; setup.maxSamplesPerBlock = 2048;
        setup.symbolicSampleSize = std::is_same<Sample, float>::value ? kSample32 : kSample64;
        check(processor->setupProcessing(setup) == kResultOk, "prepare bridge");
        check(processor->setActive(true) == kResultOk, "activate bridge");
        just::SoundState initial = saved(processor);
        f::FlangerEngine expected;
        check(expected.prepare({sampleRate, 2048, channels, channels, 0,
            std::is_same<Sample, float>::value ? just::SampleFormat::float32 : just::SampleFormat::float64, false}), "prepare reference");
        std::array<Sample, 2048> left{}, right{}, actualL{}, actualR{}, expectedL{}, expectedR{};
        Sample* inputPointers[] = {left.data(), right.data()};
        Sample* outputPointers[] = {actualL.data(), actualR.data()};
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = int(channels);
        if constexpr (std::is_same<Sample, float>::value) {
            input.channelBuffers32 = inputPointers; output.channelBuffers32 = outputPointers;
        } else { input.channelBuffers64 = inputPointers; output.channelBuffers64 = outputPointers; }
        ProcessData data{};
        data.symbolicSampleSize = setup.symbolicSampleSize;
        data.numInputs = data.numOutputs = 1; data.inputs = &input; data.outputs = &output;
        for (int n : {16, 32, 64, 128, 256, 1024, 2048, 17, 731}) {
            for (int i = 0; i < n; ++i) { left[i] = Sample(std::sin(i * .031)); right[i] = Sample(std::cos(i * .07)); }
            data.numSamples = n;
            check(processor->process(data) == kResultOk, "variable-block process");
            expected.applyTargets(initial, 0);
            just::AudioBlock<Sample> b{{left.data(), right.data()}, {expectedL.data(), expectedR.data()}, channels, channels, unsigned(n)};
            expected.process(b, {});
            for (int i = 0; i < n; ++i) check(actualL[i] == expectedL[i] && (channels == 1 || actualR[i] == expectedR[i]), "typed bridge exact effect audio");
        }
        // In-place buffers, followed by silent left input with a surviving tail.
        data.numSamples = 128;
        for(unsigned i=0;i<128;++i){left[i]=Sample(std::sin(i*.051));right[i]=Sample(std::cos(i*.071));}
        just::AudioBlock<Sample> inPlaceReference{{left.data(),right.data()},{expectedL.data(),expectedR.data()},channels,channels,128};
        expected.process(inPlaceReference,{});
        if constexpr(std::is_same<Sample,float>::value)output.channelBuffers32=inputPointers;else output.channelBuffers64=inputPointers;
        check(processor->process(data)==kResultOk,"in-place process");
        for(unsigned i=0;i<128;++i)check(left[i]==expectedL[i] && (channels==1 || right[i]==expectedR[i]),"in-place matches independent buffers");
        if constexpr(std::is_same<Sample,float>::value)output.channelBuffers32=outputPointers;else output.channelBuffers64=outputPointers;
        input.silenceFlags=1;inPlaceReference.inputSilenceFlags=1;expected.process(inPlaceReference,{});
        check(processor->process(data)==kResultOk && (output.silenceFlags&1)==0,"silent input does not suppress feedback tail");
        for(unsigned i=0;i<128;++i)check(actualL[i]==expectedL[i] && (channels==1 || actualR[i]==expectedR[i]),"input silence flag effect processing");
        processor->setActive(false);
    }
    check(processor->getLatencySamples() == 0 && processor->getTailSamples() == 960000, "actual PDC / tail");
    processor->terminate(); processor->release();
}
void curvesAndRestore() {
    auto* processor = new just::Processor;
    check(processor->initialize(nullptr) == kResultOk, "initialize automation host");
    ProcessSetup setup{kRealtime, kSample64, 2048, 48000};
    check(processor->setupProcessing(setup) == kResultOk, "setup automation host");
    processor->setActive(true);
    auto initial = saved(processor), expectedState = initial;
    f::FlangerEngine expected;
    check(expected.prepare({48000, 2048, 2, 2, 0, just::SampleFormat::float64, false}), "prepare automation reference");
    std::vector<Curve> curves = {
        {f::mixID, {{3, .1}, {63, .9}, {127, .2}, {511, 1}}},
        {f::inputGainID, {{127, .7}, {511, .3}}},
        {f::modeID, {{63, 1}, {255, 0}}},
        {f::baseMsID, {{63, 1}, {255, 0}, {511, .5}}},
        {f::phaseID, {{255, 359./360}, {511, 1./360}}},
        {f::feedbackID, {{63, 1}, {127, 0}, {511, .5}}},
        {f::wetPolarityID, {{127, 1}, {511, 0}}}
    };
    ParameterChanges changes;
    for (const auto& curve : curves) {
        int32 qi = 0, pi = 0; auto* queue = changes.addParameterData(curve.id, qi);
        for (const auto& p : curve.points) queue->addPoint(p.offset, p.value, pi);
    }
    std::array<double, 512> left{}, right{}, actualL{}, actualR{}, referenceL{}, referenceR{};
    for (unsigned i = 0; i < left.size(); ++i) { left[i] = std::sin(i * .13); right[i] = std::cos(i * .27); }
    double* inputs[] = {left.data(), right.data()}; double* outputs[] = {actualL.data(), actualR.data()};
    AudioBusBuffers input{}, output{}; input.numChannels = output.numChannels = 2;
    input.channelBuffers64 = inputs; output.channelBuffers64 = outputs;
    ProcessData data{}; data.symbolicSampleSize = kSample64; data.numSamples = 512;
    data.numInputs = data.numOutputs = 1; data.inputs = &input; data.outputs = &output; data.inputParameterChanges = &changes;
    check(processor->process(data) == kResultOk, "all automation queue points");
    for (int sample = 0; sample < 512; ++sample) {
        for (const auto& curve : curves) {
            auto i = f::registry.index(curve.id);
            expectedState.targets[i] = at(curve, sample, initial.targets[i], f::parameters[i].stepCount != 0);
        }
        expected.applyTargets(expectedState, sample);
        just::AudioBlock<double> b{{left.data()+sample, right.data()+sample}, {referenceL.data()+sample, referenceR.data()+sample}, 2, 2, 1};
        just::ProcessContext context; context.blockSampleOffset = sample;
        expected.process(b, context);
    }
    double maxError = 0;
    for(unsigned i=0;i<512;++i)maxError=std::max({maxError,std::abs(actualL[i]-referenceL[i]),std::abs(actualR[i]-referenceR[i])});
    std::cout << "host automation max error=" << maxError << '\n';
    check(maxError < 1e-12, "sample-offset automation / same-sample parameters match independent curves");
    check(saved(processor).targets == expectedState.targets, "final automation targets saved");
    ParameterChanges flush;
    auto desired = initial;
    for (unsigned i = 0; i < f::registry.count; ++i) {
        desired.targets[i] = f::parameters[i].toNormalized(f::parameters[i].toPhysical(i % 2 ? .8 : .2));
        int32 qi = 0, pi = 0; flush.addParameterData(f::parameters[i].id, qi)->addPoint(0, desired.targets[i], pi);
    }
    int32 qi=0, pi=0; flush.addParameterData(99999, qi)->addPoint(0, .5, pi);
    data.numSamples = 0; data.inputParameterChanges = &flush;
    check(processor->process(data) == kResultOk && saved(processor).targets == desired.targets, "zero-sample flush includes hidden parameters and ignores unknown ID");
    MemoryStream restore;
    check(just::writeSoundState(&restore, initial, f::registry), "encode preset state");
    restore.seek(0, IBStream::kIBSeekSet, nullptr);
    check(processor->setState(&restore) == kResultOk && saved(processor).targets == initial.targets, "restore visible immediately while stopped");
    data.inputParameterChanges = nullptr;
    check(processor->process(data) == kResultOk && saved(processor).targets == initial.targets, "restore applied at audio boundary");
    MemoryStream malformed; const char bad[] = "bad state"; malformed.write(const_cast<char*>(bad), sizeof(bad), nullptr);
    malformed.seek(0, IBStream::kIBSeekSet, nullptr);
    check(processor->setState(&malformed) != kResultOk && saved(processor).targets == initial.targets, "malformed restore preserves all sound targets");
    processor->setActive(false); processor->terminate(); processor->release();
}
int main() {
    bridgeMatrix<float>(); bridgeMatrix<double>(); curvesAndRestore();
    std::cout << "PASS actual VST3 bridge: float32/64 mono/stereo 44.1-192k variable blocks / in-place / silence tail, multi-point sample automation, simultaneous changes, hidden state and zero-block restore\n";
}
