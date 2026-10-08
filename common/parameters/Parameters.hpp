#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <optional>
namespace just {
using ParamID = std::uint32_t;
inline constexpr std::size_t maxParameters = 512;
inline constexpr ParamID bypassParamID = 0;
enum class Mapping { linear, logarithmic };
enum class Transition { continuous, discrete, preparedConfiguration };
struct ParameterSpec {
    const char* stableKey;
    ParamID id;
    const char* title;
    const char* unit;
    double minimum, maximum, initial;
    Mapping mapping;
    std::uint32_t stepCount;
    bool automatable;
    const char* applicableModes;
    Transition transition;
    double smoothingMs;
    const char* const* enumLabels = nullptr;
    // Physical units; decodeState only, when this ParamID is absent. New
    // instances and Init still use initial. Unset preserves the existing rule.
    std::optional<double> restoreMissingDefault=std::nullopt;
    double toNormalized(double physical) const noexcept {
        if (!std::isfinite(physical)) physical=initial;
        physical=std::clamp(physical,minimum,maximum);
        const double v=mapping==Mapping::logarithmic
            ? std::log(physical/minimum)/std::log(maximum/minimum)
            : (physical-minimum)/(maximum-minimum);
        return std::clamp(v,0.0,1.0);
    }
    double toPhysical(double normalized) const noexcept {
        if (!std::isfinite(normalized)) return initial;
        normalized=std::clamp(normalized,0.0,1.0);
        if(stepCount) normalized=std::round(normalized*stepCount)/stepCount;
        return mapping==Mapping::logarithmic
            ? minimum*std::pow(maximum/minimum,normalized)
            : minimum+(maximum-minimum)*normalized;
    }
    // Formatting and parsing belong to the controller/UI thread, never process().
    void format(double normalized,char* destination,std::size_t capacity) const noexcept {
        if(!destination || !capacity) return;
        if(!std::isfinite(normalized)) normalized=toNormalized(initial);
        const double physical=toPhysical(normalized);
        if(enumLabels && stepCount) {
            std::snprintf(destination,capacity,"%s",enumLabels[static_cast<std::size_t>(std::round(std::clamp(normalized,0.0,1.0)*stepCount))]);
        } else std::snprintf(destination,capacity,"%.6g",physical);
    }
    bool parse(const char* text,double& normalized) const noexcept {
        if(!text) return false;
        if(enumLabels && stepCount) for(std::uint32_t i=0;i<=stepCount;++i)
            if(std::strcmp(text,enumLabels[i])==0) {normalized=double(i)/stepCount;return true;}
        errno=0;char* end=nullptr;
        const double value=std::strtod(text,&end);
        if(end==text || errno==ERANGE || !std::isfinite(value) || value<minimum || value>maximum) return false;
        while(*end==' ' || *end=='\t') ++end;
        if(*end && (!*unit || std::strcmp(end,unit)!=0)) return false;
        normalized=toNormalized(value);return true;
    }
};
struct ParameterRegistry {
    const ParameterSpec* specs=nullptr; std::size_t count=0;
    std::size_t index(ParamID id) const noexcept {
        for(std::size_t i=0;i<count;++i) if(specs[i].id==id) return i;
        return count;
    }
    bool valid() const noexcept {
        if(!specs || count==0 || count>maxParameters) return false;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=specs[i];
            if(!p.stableKey || !p.title || !p.unit || !p.applicableModes || !*p.stableKey ||
               !std::isfinite(p.minimum) || !std::isfinite(p.maximum) || !std::isfinite(p.initial) ||
               p.minimum>=p.maximum || p.initial<p.minimum || p.initial>p.maximum ||
               (p.mapping==Mapping::logarithmic && p.minimum<=0) ||
               !std::isfinite(p.smoothingMs) || p.smoothingMs<0) return false;
            if(p.restoreMissingDefault && (!std::isfinite(*p.restoreMissingDefault) ||
               *p.restoreMissingDefault<p.minimum || *p.restoreMissingDefault>p.maximum))return false;
            for(std::size_t j=0;j<i;++j)
                if(p.id==specs[j].id || std::strcmp(p.stableKey,specs[j].stableKey)==0) return false;
        }
        return true;
    }
};
struct ParamEvent {ParamID id;double normalizedValue;std::int32_t sampleOffset;};
}
