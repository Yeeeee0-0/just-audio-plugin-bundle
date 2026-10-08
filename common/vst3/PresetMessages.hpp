#pragma once
#include "../state/Preset.hpp"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
namespace just {
inline constexpr const char* presetRequestID="JUST.Preset.Apply.v2";
inline constexpr const char* presetQueryID="JUST.Preset.Query.v2";
inline constexpr const char* presetStateID="JUST.Preset.State.v2";
inline bool messageSession(Steinberg::Vst::IMessage* m,std::uint64_t session) {
    Steinberg::int64 value=0;return m && m->getAttributes() && session &&
        m->getAttributes()->getInt("Session",value)==Steinberg::kResultOk && value>0 && std::uint64_t(value)==session;
}
}
