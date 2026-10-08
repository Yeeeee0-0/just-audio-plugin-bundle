#pragma once
#include "Parameters.hpp"
namespace just::limiter {
// Approved append-only ID, using the official common restore-default contract.
// No existing ID or normalized mapping is repurposed.

inline bool algorithmRegistered()noexcept{return registry.index(algorithmVersion)<registry.count;}
inline bool transparentVersion(double normalized)noexcept{return normalized>=.5;}
inline std::uint32_t fixedLatency(double rate)noexcept{return std::uint32_t(std::ceil(rate*.005))+48;}
inline unsigned reconstructionRadius(double rate)noexcept{return std::min(64u,fixedLatency(rate)/3);}
inline double maximumTpLookaheadMs(double rate)noexcept{return rate>0?1000.*(fixedLatency(rate)-3*reconstructionRadius(rate))/rate:0;}
inline double effectiveLookaheadMs(double requested,double rate,bool modern,bool tp)noexcept{
 if(rate<=0)return 0;
 if(!modern)requested=tp?std::clamp(requested,.1,5.):std::clamp(requested,0.,1.);
 else requested=std::clamp(requested,0.,5.);
 auto samples=std::uint32_t(std::ceil(requested*rate*.001));
 if(modern&&tp)samples=std::min(samples,fixedLatency(rate)-3*reconstructionRadius(rate));
 return 1000.*samples/rate;
}
}
