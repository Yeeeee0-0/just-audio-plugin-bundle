#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace just::limiter {
// Measures the maximum applied control attenuation among the contributions to
// ONE emitted sample. It is not output/input level difference. For the Mix FIR
// this is a conservative contribution bound, not a single signal gain estimate.
class AppliedAttenuation {
 std::array<double,16> live{};
 std::array<double,65> down{};
 std::array<double,64> candidate{};
 unsigned liveAt=0,downAt=0,candidateAt=0;
public:
 AppliedAttenuation(){reset();}
 void reset() noexcept {live.fill(1);down.fill(1);candidate.fill(1);liveAt=downAt=candidateAt=0;}
 double advance(double liveGain,double mixGain,double mix,const std::array<double,65>& kernel) noexcept {
  const double delayedLive=live[liveAt];live[liveAt]=liveGain;liveAt=(liveAt+1)%16;
  double minimumMix=1;
  for(unsigned p=0;p<4;++p){down[downAt]=mixGain;if(p==0)for(unsigned k=0;k<65;++k)if(kernel[k]!=0)minimumMix=std::min(minimumMix,down[(downAt+65-k)%65]);downAt=(downAt+1)%65;}
  double applied=1;
  if(mix<1)applied=std::min(applied,delayedLive);
  if(mix>0)applied=std::min(applied,minimumMix);
  candidate[candidateAt]=applied;const auto emitted=candidate[(candidateAt+64-32)%64];candidateAt=(candidateAt+1)%64;
  return emitted;
 }
 static double reductionDb(double emitted,double safetyGain,double bypassMix,double transitionGain) noexcept {
  // Dry bypass has no limiting gain. The re-entry protection guard is a real
  // multiplier on this same final sample and is included exactly once.
  const double gain=(bypassMix+(1-bypassMix)*emitted*safetyGain)*transitionGain;
  return -20*std::log10(std::clamp(gain,1e-30,1.0));
 }
};
}
