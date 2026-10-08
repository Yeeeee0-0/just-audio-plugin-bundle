#pragma once
#include <algorithm>
#include <cmath>
namespace just {
// Coordinates are clockwise from twelve o'clock, matching the approved SVG.
inline double rotaryAngle(double normalized) noexcept {return -135.+270.*std::clamp(normalized,0.,1.);}
struct RotaryDrag {
    double accumulator=0,previousY=0;
    void begin(double normalized,double y) noexcept {accumulator=std::clamp(normalized,0.,1.);previousY=y;}
    double move(double y,bool fine) noexcept {
        accumulator=std::clamp(accumulator+(previousY-y)*(fine?.0006:.005),0.,1.);
        previousY=y;return accumulator;
    }
};
struct RotaryLayout {
    double diameter,top,valueTop,valueHeight;
    static RotaryLayout fit(double width,double height,double valueHeight=22,double labelHeight=20) noexcept {
        const double d=std::max(24.,std::min(width-14.,height-labelHeight-4-valueHeight));
        return {d,labelHeight+2,labelHeight+4+d,valueHeight};
    }
};
}
