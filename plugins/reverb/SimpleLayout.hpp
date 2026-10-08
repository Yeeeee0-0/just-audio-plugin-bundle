#pragma once
#include "SimpleMacros.hpp"
namespace just::reverb {
struct UiRect {int x=0,y=0,width=0,height=0;};
struct SimpleLayout {
    std::array<UiRect,simpleMacroCount> controls{};
    UiRect graph{};
    int height=0;
    static SimpleLayout fit(int width) noexcept {
        SimpleLayout result;const double scale=double(width)/1120.;
        auto rect=[&](double x,double y,double w,double h){return UiRect{int(std::round(x*scale)),int(std::round(y*scale)),int(std::round(w*scale)),int(std::round(h*scale))};};
        result.graph=rect(36,20,1048,188);result.height=int(std::round(460*scale));
        double x=35;constexpr double gap=(1050-6*113-182)/6.;
        for(unsigned n=0;n<simpleMacroCount;++n){const bool hero=n==3;result.controls[n]=rect(x+(hero?-2:5.5),hero?216:258,hero?186:102,hero?218:134);x+=(hero?182:113)+gap;}
        return result;
    }
};
}
