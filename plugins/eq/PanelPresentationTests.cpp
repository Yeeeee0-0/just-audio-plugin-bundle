#include "PanelPresentation.hpp"
#include "PanelPlacement.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
using Panel=just::eq::PanelPresentation;
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
static bool near(double a,double b){return std::abs(a-b)<1e-12;}
int main(){
    Panel p;
    check(!p.acceptsInput(0) && near(p.backgroundAlpha(0,true),0),"initial hidden card cannot intercept input or hover");
    p.select(100);
    check(p.phase(3099)==Panel::Phase::visible,"full 3000ms idle before fade");
    check(p.phase(3100)==Panel::Phase::fading && near(p.fade(3100),1),"fade begins without opacity discontinuity");
    check(near(p.backgroundAlpha(3425,false),.36) && near(p.backgroundAlpha(3425,true),.5),"650ms fade takes priority over hover alpha");
    check(p.phase(3750)==Panel::Phase::hidden && !p.acceptsInput(3750),"completed fade disables hit testing and focus eligibility");
    p.interact(4000);p.activity(Panel::Activity::textFocus,true,4000);
    check(p.phase(4000)==Panel::Phase::hidden && near(p.backgroundAlpha(4000,true),0),"hover or stale interaction cannot revive expired card");
    p.select(5000);
    for(double now:{5000.,6000.,7000.,8000.,8200.})p.backgroundAlpha(now,true);
    check(p.phase(8650)==Panel::Phase::hidden,"stationary hover does not extend deadline");
    p.select(9000);p.select(12325);
    check(p.phase(12650)==Panel::Phase::visible && near(p.fade(12650),1),"same-node reselect during fade cancels prior deadline");
    check(p.phase(15325)==Panel::Phase::fading,"reselect starts a new exact idle period");
    for(auto activity:{Panel::Activity::nodeDrag,Panel::Activity::knobDrag,Panel::Activity::textFocus,Panel::Activity::solo,Panel::Activity::menu}){
        Panel held;held.select(0);held.activity(activity,true,2999);
        check(held.phase(20000)==Panel::Phase::visible && near(held.backgroundAlpha(20000,false),1),"real held interaction pauses idle and is opaque");
        check(held.layoutLocked()==(activity!=Panel::Activity::nodeDrag),"editing locks position while node dragging allows collision avoidance");
        held.activity(activity,false,20000);
        check(held.phase(22999)==Panel::Phase::visible && held.phase(23000)==Panel::Phase::fading && held.phase(23650)==Panel::Phase::hidden,"interaction end restarts full idle/fade interval");
    }
    Panel overlap;overlap.select(0);overlap.activity(Panel::Activity::textFocus,true,100);overlap.activity(Panel::Activity::menu,true,200);
    overlap.activity(Panel::Activity::menu,false,10000);
    check(overlap.phase(20000)==Panel::Phase::visible,"one activity ending cannot unpause another");
    overlap.activity(Panel::Activity::textFocus,false,21000);
    check(overlap.phase(24000)==Panel::Phase::fading,"last active interaction controls restart time");
    Panel form;form.select(0);form.automaticForm(Panel::Form::compact);form.choose(Panel::Form::full,500);form.automaticForm(Panel::Form::compact);
    check(form.form()==Panel::Form::full && form.choice()==Panel::Choice::full,"explicit expand survives automatic collision collapse request");
    form.choose(Panel::Form::compact,700);form.automaticForm(Panel::Form::full);
    check(form.form()==Panel::Form::compact,"explicit collapse is not immediately undone");
    form.select(1000);form.automaticForm(Panel::Form::compact);
    check(form.choice()==Panel::Choice::automatic && form.form()==Panel::Form::compact,"reselection releases previous manual form choice");
    check(form.phase(4000)==Panel::Phase::fading && form.phase(4650)==Panel::Phase::hidden,"compact shares full card timing");
    form.select(5000);form.choose(Panel::Form::full,5000);
    check(form.phase(8650)==Panel::Phase::hidden,"manual form selection does not silently disable inactivity rule");
    form.select(9000);form.activity(Panel::Activity::solo,true,9100);form.hide();
    check(!form.active(Panel::Activity::solo) && !form.acceptsInput(9100),"explicit hide clears presentation activity without any sound-state access");
    using Placement=just::eq::PanelPlacement;using Rect=just::eq::PanelRect;
    const Rect graph{40,20,680,384};
    auto zone=[](double x,double y){return Rect{x-70,y-60,140,92};};
    Placement geometry;
    auto high=geometry.place(graph,380,zone(380,70),false,false);
    check(high.available && high.form==Panel::Form::full && graph.contains(high.rect),"full overlay when high node leaves a safe bottom slot");
    check(near(high.rect.bottom(),graph.bottom()-36),"overlay stays above axis without reserving an extra row");
    auto low=geometry.place(graph,380,zone(380,300),false,false);
    check(low.available && low.form==Panel::Form::compact && !low.rect.overlaps(zone(380,300)),"low central node forces a genuinely safe compact card");
    geometry.reset();auto left=geometry.place(graph,100,zone(100,300),false,false);
    check(left.available && left.form==Panel::Form::full && left.rect.x>=zone(100,300).right(),"full card moves to the available side");
    auto heldPosition=geometry.place(graph,650,zone(650,70),true,false);
    check(near(heldPosition.rect.x,left.rect.x),"safe old location remains stable during node drag");
    auto collapse=geometry.place(graph,380,zone(380,300),true,false);
    auto staysCompact=geometry.place(graph,100,zone(100,300),true,false);
    check(collapse.form==Panel::Form::compact && staysCompact.form==Panel::Form::compact,"drag collision collapses once without expand-collapse oscillation");
    auto release=geometry.place(graph,100,zone(100,300),false,false);
    check(release.form==Panel::Form::full && !release.rect.overlaps(zone(100,300)),"release expands only when the full card is safe");
    auto locked=geometry.place(graph,380,zone(380,300),false,true);
    check(near(locked.rect.x,release.rect.x) && locked.temporarilyCoversNode,"active editing locks position and exposes temporary overlap state");
    geometry.reset();auto forced=geometry.place(graph,380,zone(380,300),false,false,Panel::Choice::full);
    check(forced.form==Panel::Form::full && forced.temporarilyCoversNode,"explicit user expansion is retained despite collision");
    auto cleared=geometry.place(graph,380,zone(380,70),true,false,Panel::Choice::full);
    check(!cleared.temporarilyCoversNode,"temporary overlap indicator clears as soon as the node is safe");
    auto collapsed=geometry.place(graph,380,zone(380,300),false,false,Panel::Choice::compact);
    check(collapsed.form==Panel::Form::compact && !collapsed.rect.overlaps(zone(380,300)),"explicit collapse returns to safe compact placement");
    for(double x:{40.,100.,240.,380.,520.,660.,720.})for(double y:{20.,70.,200.,300.,404.}){
        geometry.reset();const auto reserved=zone(x,y);auto placed=geometry.place(graph,x,reserved,false,false);
        check(placed.available && graph.contains(placed.rect) && !placed.rect.overlaps(reserved),"minimum logical graph preserves selected node/badge clearance across edges and gain range");
    }
    std::cout<<"PASS "<<checks<<" fake-clock/placement checks; no AppKit, audio or parameter writes\n";
}
