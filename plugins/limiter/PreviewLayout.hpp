#pragma once
#include <algorithm>

namespace just::limiter {
// Module body geometry from the approved 1120px preview. The shared shell owns
// header/footer, scale, theme, and replaceable artwork.
inline constexpr const char* controlLabelsZh[]={"旁路","输入增益","限幅上限","释放","瞬态","前瞻","比较衰减","处理模式","响度参考","输出增益"};
struct PreviewRect { double x=0,y=0,width=0,height=0; };
struct PreviewLayout {
    PreviewRect input,output,card,plot,readout,details,status;
    static PreviewLayout make(double width,double height,bool advanced) noexcept {
        PreviewLayout result;
        const double sx=width/1120.;
        const double detailsHeight=advanced?std::min(205.,height*.38):0.;
        const double bodyHeight=std::max(180.,height-detailsHeight-24.);
        const double pad=30*sx,gap=27*sx,side=132*sx;
        const double top=std::min(32.,bodyHeight*.075);
        const double cardHeight=std::max(140.,bodyHeight-top-18.);
        result.input={pad,top,side,cardHeight};
        result.output={width-pad-side,top,side,cardHeight};
        result.card={pad+side+gap,top,width-2*(pad+side+gap),cardHeight};
        const auto& c=result.card;
        result.plot={c.x+22*sx,c.y+68,c.width-44*sx,std::max(35.,c.height-162.)};
        result.readout={c.x+16*sx,c.y+c.height-78,c.width-32*sx,64};
        result.details={pad,bodyHeight,width-2*pad,detailsHeight};
        result.status={pad,height-22,width-2*pad,20};
        return result;
    }
};
}
