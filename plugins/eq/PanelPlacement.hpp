#pragma once
#include "PanelPresentation.hpp"
#include <cmath>

namespace just::eq {
struct PanelRect {
    double x=0,y=0,w=0,h=0;
    double right() const noexcept {return x+w;}
    double bottom() const noexcept {return y+h;}
    bool contains(PanelRect b) const noexcept {
        return b.x>=x && b.y>=y && b.right()<=right() && b.bottom()<=bottom();
    }
    bool overlaps(PanelRect b) const noexcept {
        return x<b.right() && right()>b.x && y<b.bottom() && bottom()>b.y;
    }
};
// The native adapter supplies the selected node + mode badge expanded by32,
// unioned with the frequency/value label's bounds. Coordinates are logical
// content points, independent of backing pixels and host renderScale.
class PanelPlacement {
public:
    static constexpr double fullWidth=500,fullHeight=132;
    static constexpr double compactWidth=260,compactHeight=56;
    static constexpr double bottomGap=36,nodeClearance=32;
    struct Result {
        PanelRect rect{};
        PanelPresentation::Form form=PanelPresentation::Form::full;
        bool available=false,temporarilyCoversNode=false;
    };
private:
    Result previous{};
    bool compactDuringDrag=false;
    static PanelRect desired(PanelRect graph,double anchor,PanelPresentation::Form form) noexcept {
        const bool full=form==PanelPresentation::Form::full;
        const double w=full?fullWidth:compactWidth,h=full?fullHeight:compactHeight;
        if(graph.w<w || graph.h<h+bottomGap)return {};
        return {std::clamp(anchor-w/2,graph.x,graph.right()-w),graph.bottom()-bottomGap-h,w,h};
    }
    static Result safe(PanelRect graph,double anchor,PanelRect reserved,PanelPresentation::Form form) noexcept {
        auto rect=desired(graph,anchor,form);
        if(!rect.w)return {};
        if(!rect.overlaps(reserved))return {rect,form,true,false};
        Result best{};double distance=0;
        // A one-dimensional interval collision has exactly two escape edges.
        // Y remains fixed above the frequency axis; typography never shrinks.
        for(double x:{reserved.x-rect.w,reserved.right()}){
            PanelRect candidate{std::clamp(x,graph.x,graph.right()-rect.w),rect.y,rect.w,rect.h};
            if(candidate.overlaps(reserved))continue;
            double next=std::abs(candidate.x-rect.x);
            if(!best.available || next<distance){best={candidate,form,true,false};distance=next;}
        }
        return best;
    }
public:
    void reset() noexcept {previous={};compactDuringDrag=false;}
    Result place(PanelRect graph,double anchor,PanelRect reserved,bool dragging,bool editing,
                 PanelPresentation::Choice choice=PanelPresentation::Choice::automatic) noexcept {
        if(!dragging)compactDuringDrag=false;
        const bool explicitFull=choice==PanelPresentation::Choice::full;
        const bool explicitCompact=choice==PanelPresentation::Choice::compact;
        const bool sameChoice=(!explicitFull || previous.form==PanelPresentation::Form::full) &&
            (!explicitCompact || previous.form==PanelPresentation::Form::compact);
        if(previous.available && sameChoice && graph.contains(previous.rect)){
            if(editing){previous.temporarilyCoversNode=previous.rect.overlaps(reserved);return previous;}
            if(dragging && !previous.rect.overlaps(reserved)){previous.temporarilyCoversNode=false;return previous;}
        }
        if(explicitFull){
            auto rect=desired(graph,anchor,PanelPresentation::Form::full);
            previous={rect,PanelPresentation::Form::full,rect.w>0,rect.w>0 && rect.overlaps(reserved)};
            return previous; // explicit expansion must not immediately auto-collapse
        }
        Result result;
        if(!explicitCompact && !compactDuringDrag)result=safe(graph,anchor,reserved,PanelPresentation::Form::full);
        if(!result.available){
            result=safe(graph,anchor,reserved,PanelPresentation::Form::compact);
            if(dragging && result.available)compactDuringDrag=true;
        }
        // An impossible/corrupt viewport may yield no safe slot. Never cover
        // the selected node or silently scale the controls to force a fit.
        previous=result;return previous;
    }
};
}
