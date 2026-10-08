#pragma once
#include "Parameters.hpp"
#include "common/runtime/Telemetry.hpp"
#include <array>

namespace just::flanger::ui {
inline constexpr std::array<ParamID,16> order={rateHzID,depthID,feedbackID,mixID,
    baseMsID,modeID,shapeID,phaseID,stereoPhaseID,syncID,divisionID,timeModeID,
    fbLowpassHzID,wetPolarityID,inputGainID,outputGainID};
inline constexpr std::array<const char*,16> labelsZh={"速率","深度","反馈","混合",
    "基础延时","模式","波形","相位","立体声相位","同步","节拍","时序模式",
    "反馈低通","湿声极性","输入","输出"};

// The preview's demo clock is never a source for a production rate readout.
struct TimingReadout {
    bool rateAvailable=false,tempoAvailable=false,syncKnown=false,syncFallback=false;
    double rateHz=0,bpm=0;
};
inline TimingReadout timingReadout(TelemetryAvailability availability,
                                  const RuntimeTelemetrySnapshot& frame) noexcept {
    TimingReadout result;
    if(availability!=TelemetryAvailability::fresh)return result;
    result.rateAvailable=(frame.validFields&telemetryEffectiveRate) && std::isfinite(frame.effectiveRateHz) && frame.effectiveRateHz>0;
    result.tempoAvailable=(frame.validFields&telemetryTempo) && std::isfinite(frame.bpm) && frame.bpm>0;
    result.syncKnown=(frame.validFields&telemetrySync)!=0;
    result.syncFallback=result.syncKnown && (frame.flags&telemetryFlagSyncUnavailable);
    if(result.rateAvailable)result.rateHz=frame.effectiveRateHz;
    if(result.tempoAvailable)result.bpm=frame.bpm;
    return result;
}
inline bool enabled(ParamID id,bool manual,bool sync,bool stereo) noexcept {
    return !(manual && (id==rateHzID || id==depthID || id==divisionID || id==syncID)) &&
           !(sync && id==rateHzID) && (id!=stereoPhaseID || stereo);
}
struct Layout {
    int margin=24,graphTop=28,graphHeight=96,controlsTop=0,controlWidth=112,controlHeight=148,gap=40,columns=4,canvasHeight=0;
};
inline Layout layout(int width,int height,bool advanced) noexcept {
    Layout g;
    g.margin=std::clamp(width/31,12,36);
    const int row=height<316?136:148;
    g.controlHeight=row;
    g.graphHeight=advanced?96:std::max(96,height-row-72);
    g.controlsTop=g.graphTop+g.graphHeight+10;
    g.columns=advanced?std::max(4,(width-2*g.margin)/154):4;
    g.controlWidth=112;
    g.gap=std::max(0,(width-2*g.margin-g.columns*g.controlWidth)/std::max(1,g.columns-1));
    // Simple controls stay centered as a four-control row at large widths.
    if(!advanced)g.gap=std::min(65,g.gap);
    g.canvasHeight=((order.size()+g.columns-1)/g.columns)*160+12;
    return g;
}
}
