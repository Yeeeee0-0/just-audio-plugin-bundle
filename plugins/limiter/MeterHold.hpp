#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace just::limiter {
// UI-owned accumulation only. Sources must be real processor observations.
// This helper is transport-independent; it neither reads audio queues nor writes
// processor commands. Repeated UI refreshes do not reintroduce a reset peak.
struct MeterHold {
 std::uint64_t lastSequence=0;
 double output=0,reduction=0,maximumOutput=0,maximumReduction=0;
 bool currentValid=false,outputHoldValid=false,reductionHoldValid=false;
 void observe(std::uint64_t sequence,double outputLinear,double attenuationDb) noexcept {
  if(sequence<=lastSequence)return;lastSequence=sequence;
  currentValid=std::isfinite(outputLinear)&&outputLinear>=0&&std::isfinite(attenuationDb)&&attenuationDb>=0;
  if(!currentValid)return;
  output=outputLinear;reduction=attenuationDb;
  maximumOutput=outputHoldValid?std::max(maximumOutput,output):output;
  maximumReduction=reductionHoldValid?std::max(maximumReduction,reduction):reduction;
  outputHoldValid=reductionHoldValid=true;
 }
 void resetOutput() noexcept {maximumOutput=0;outputHoldValid=false;}
 void resetReduction() noexcept {maximumReduction=0;reductionHoldValid=false;}
};
}
