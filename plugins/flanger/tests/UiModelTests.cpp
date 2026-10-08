#include "../UiModel.hpp"
#include <cstdlib>
#include <iostream>
using namespace just;
namespace u=just::flanger::ui;
static unsigned checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
int main(){
    RuntimeTelemetrySnapshot f;
    f.validFields=telemetryEffectiveRate|telemetryTempo|telemetrySync;
    f.effectiveRateHz=.73123456789;f.bpm=137;
    for(auto state:{TelemetryAvailability::unavailable,TelemetryAvailability::stale}){
        auto r=u::timingReadout(state,f);
        check(!r.rateAvailable && !r.tempoAvailable && !r.syncKnown,"stale/unavailable telemetry cannot advertise sync or BPM");
    }
    auto actual=u::timingReadout(TelemetryAvailability::fresh,f);
    check(actual.rateAvailable && actual.rateHz==f.effectiveRateHz && actual.tempoAvailable && actual.bpm==137 && actual.syncKnown && !actual.syncFallback,"fresh source retains full measured precision and host tempo");
    f.flags=telemetryFlagSyncUnavailable;
    actual=u::timingReadout(TelemetryAvailability::fresh,f);
    check(actual.syncFallback && actual.rateAvailable,"engine fallback stays explicit even with a valid effective rate");
    f.validFields=telemetryEffectiveRate;
    actual=u::timingReadout(TelemetryAvailability::fresh,f);
    check(actual.rateAvailable && !actual.tempoAvailable && !actual.syncKnown && !actual.syncFallback,"missing source mask never claims host sync from rate alone");
    f.effectiveRateHz=std::numeric_limits<double>::quiet_NaN();f.bpm=-1;f.validFields=telemetryEffectiveRate|telemetryTempo;
    actual=u::timingReadout(TelemetryAvailability::fresh,f);
    check(!actual.rateAvailable && !actual.tempoAvailable,"invalid numeric telemetry stays unavailable");
    check(!u::enabled(just::flanger::rateHzID,false,true,true) && u::enabled(just::flanger::depthID,false,true,true),"Sync disables free Rate and retains Depth");
    check(!u::enabled(just::flanger::rateHzID,true,false,true) && !u::enabled(just::flanger::depthID,true,false,true) && !u::enabled(just::flanger::syncID,true,false,true),"Manual does not expose an active LFO clock");
    check(!u::enabled(just::flanger::stereoPhaseID,false,false,false) && u::enabled(just::flanger::stereoPhaseID,false,false,true),"stereo phase requires the real stereo bus");
    for(int width:{680,720,880,1080})for(int height:{296,310,340,460}){
        auto g=u::layout(width,height,false);
        const int rowWidth=4*g.controlWidth+3*g.gap;
        check(rowWidth<=width && g.controlsTop+g.controlHeight<=height-24,"four complete controls do not overlap the measured status at the smallest content size");
        check(g.graphHeight>=96 && width-2*g.margin>=600 && g.graphTop+g.graphHeight<=g.controlsTop,"complete measured spectrum remains readable");
        g=u::layout(width,height,true);
        check(g.columns>=4 && g.controlsTop+g.controlHeight<=height-24 && g.canvasHeight>height-g.controlsTop,"Advanced preserves a full first row and scrolls to all controls");
    }
    std::cout<<"PASS Flanger UI model: "<<checks<<" checks; source freshness, fallback, bus conditions, minimum four-control layout\n";
}
