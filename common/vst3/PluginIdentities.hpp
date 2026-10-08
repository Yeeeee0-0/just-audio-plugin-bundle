#pragma once
#include "../state/State.hpp"
#include <array>
namespace just {
struct PluginIdentity {
    const char* slug; const char* name; const char* bypassKey; std::uint32_t slot;
    Uid processor; Uid controller; int width; int height;
    std::array<const char*,4> simpleLabels; std::size_t simpleCount;
};
inline constexpr std::array<PluginIdentity,10> pluginIdentities {{
    {"eq", "JUST EQ", "eq.bypass", 1, {{0x4A555354u, 0x20261001u, 0x00000001u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000001u, 0x4354524Cu}}, 760, 460, {{"Frequency", "Gain", "Q", "Shape"}}, 4},
    {"reverb", "JUST Reverb", "reverb.bypass", 2, {{0x4A555354u, 0x20261001u, 0x00000002u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000002u, 0x4354524Cu}}, 720, 420, {{"Decay", "Pre-delay", "High Cut", "Mix"}}, 4},
    {"delay", "JUST Delay", "delay.bypass", 3, {{0x4A555354u, 0x20261001u, 0x00000003u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000003u, 0x4354524Cu}}, 720, 420, {{"Time", "Feedback", "High Cut", "Mix"}}, 4},
    {"tremolo", "JUST Tremolo", "trem.bypass", 4, {{0x4A555354u, 0x20261001u, 0x00000004u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000004u, 0x4354524Cu}}, 640, 300, {{"Frequency", "Amount", "Stereo Separation", ""}}, 3},
    {"compressor", "JUST Compressor", "compressor.bypass", 5, {{0x4A555354u, 0x20261001u, 0x00000005u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000005u, 0x4354524Cu}}, 720, 420, {{"Threshold", "Ratio", "Attack", "Release"}}, 4},
    {"limiter", "JUST Limiter", "limiter.bypass", 6, {{0x4A555354u, 0x20261001u, 0x00000006u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000006u, 0x4354524Cu}}, 720, 420, {{"Input", "Ceiling", "Release", ""}}, 3},
    {"gate", "JUST Gate", "gate.bypass", 7, {{0x4A555354u, 0x20261001u, 0x00000007u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000007u, 0x4354524Cu}}, 720, 420, {{"Threshold", "Range", "Release", ""}}, 3},
    {"flanger", "JUST Flanger", "flan.bypass", 8, {{0x4A555354u, 0x20261001u, 0x00000008u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000008u, 0x4354524Cu}}, 720, 420, {{"Rate", "Depth", "Feedback", "Mix"}}, 4},
    {"fake_stereo", "JUST Wider", "stereo.bypass", 9, {{0x4A555354u, 0x20261001u, 0x00000009u, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x00000009u, 0x4354524Cu}}, 720, 420, {{"Generated Width", "Low Protect", "Character", ""}}, 3},
    {"distortion", "JUST Distortion", "distortion.bypass", 10, {{0x4A555354u, 0x20261001u, 0x0000000Au, 0x50524F43u}}, {{0x4A555354u, 0x20261001u, 0x0000000Au, 0x4354524Cu}}, 720, 420, {{"Model", "Drive", "High Cut", "Mix"}}, 4}
}};
}
