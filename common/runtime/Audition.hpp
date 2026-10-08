#pragma once
#include "../parameters/Parameters.hpp"
namespace just {
enum class AuditionPhase:std::uint32_t {unavailable,pending,active,ended,rejected};
struct AuditionStatus {std::uint64_t token=0;ParamID bandAnchor=0;AuditionPhase phase=AuditionPhase::unavailable;};
struct AuditionRequest {std::uint64_t token=0,renewal=0,session=0;ParamID bandAnchor=0;std::uint32_t enabled=0;std::uint64_t deadlineNanoseconds=0;};
inline constexpr const char* auditionRequestID="JUST.Audition.Request.v2";
inline constexpr const char* auditionStatusID="JUST.Audition.Status.v2";
}
