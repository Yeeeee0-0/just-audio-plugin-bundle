#pragma once
#include "Model.hpp"
#include <cstring>
namespace just {
inline constexpr const char* uiVersion="0.1.0";
inline constexpr const char* uiAuthor="Yee Huang";
inline constexpr const char* uiContact="yeehuang2002@163.com";
inline const char* displayProductName(const char* slug,const char* legacy) noexcept {return !std::strcmp(slug,"fake_stereo")?"JUST Wider":legacy;}
inline const char* productSubtitle(const char* slug) noexcept {
    const char* slugs[]={"eq","reverb","delay","tremolo","compressor","limiter","gate","flanger","fake_stereo","distortion"};
    const char* labels[]={"EQUALIZER","REVERBERATION","STEREO DELAY","AMPLITUDE MODULATION","DYNAMICS","PEAK LIMITER","NOISE GATE","COMB MODULATION","STEREO IMAGER","HARMONIC SHAPING"};
    for(unsigned i=0;i<10;++i)if(!std::strcmp(slug,slugs[i]))return labels[i];return "JUST AUDIO";
}
// Asset keys are independent of geometry and parameter/processor identity.
struct BackgroundSchedule {
    static int framesPerSecond(const EditorViewState&,bool) noexcept {
        return 0; // This release disables dynamic artwork; legacy UI fields still round-trip.
    }
};
inline bool safeAssetRelativePath(const char* path) noexcept {
    if(!path || !*path || *path=='/' || *path=='\\' || std::strstr(path,"..") || std::strchr(path,':') || std::strchr(path,'\\'))return false;
    return true;
}
struct ShellGeometry {static constexpr int header=84,footer=40;};
}
