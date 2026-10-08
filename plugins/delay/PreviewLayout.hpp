#pragma once
#include <algorithm>

namespace just::delay {
// Module content coordinates; the shared shell supplies its own header/footer.
struct PreviewRect {int x=0,y=0,width=0,height=0;};
struct PreviewLayout {
    PreviewRect graph,ping,cells[3],note;
    int dial=0;
    static PreviewLayout fit(int width,int height) noexcept {
        PreviewLayout out;
        const int margin=width>=900?36:20;
        const int noteHeight=height>=350?24:0;
        out.dial=std::clamp((height-150)/2,64,116);
        const int cellHeight=out.dial+46; // official shared RotaryLayout value row
        const int graphHeight=std::clamp(height-cellHeight-noteHeight-86,64,224);
        out.graph={margin,16,width-2*margin,graphHeight};
        out.ping={(width-180)/2,16+graphHeight+12,180,32};
        const int rowY=out.ping.y+out.ping.height+12;
        const int cellWidth=std::min(160,(width-2*margin)/3);
        const int spacing=std::min(72,std::max(12,(width-2*margin-3*cellWidth)/2));
        const int rowWidth=3*cellWidth+2*spacing;
        for(int i=0;i<3;++i)out.cells[i]={(width-rowWidth)/2+i*(cellWidth+spacing),rowY,cellWidth,cellHeight};
        out.note={margin,height-noteHeight-8,width-2*margin,noteHeight};
        return out;
    }
};
}
