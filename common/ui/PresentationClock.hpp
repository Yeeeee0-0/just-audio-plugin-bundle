#pragma once
namespace just {
// UI thread only. Pass the same monotonic millisecond source to both methods.
// Audio/sample clocks and audition leases must never use this display clock.
class PresentationClock {
    double stoppedAt=0,omitted=0;
    bool paused=false;
public:
    double now(double monotonicMilliseconds) const noexcept {
        return (paused?stoppedAt:monotonicMilliseconds)-omitted;
    }
    void setPaused(bool next,double monotonicMilliseconds) noexcept {
        if(next==paused)return;
        if(next)stoppedAt=monotonicMilliseconds;
        else omitted+=monotonicMilliseconds-stoppedAt;
        paused=next;
    }
};
}
