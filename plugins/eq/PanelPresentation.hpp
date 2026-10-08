#pragma once
#include <algorithm>
#include <cstdint>

namespace just::eq {
// View-only clock/intent state. The native adapter supplies monotonic ms and
// actual interaction events; neither telemetry nor sound parameters enter here.
class PanelPresentation {
public:
    enum class Phase {hidden,visible,fading};
    enum class Form {full,compact};
    enum class Choice {automatic,full,compact};
    enum class Activity:std::uint8_t {nodeDrag=1,knobDrag=2,textFocus=4,solo=8,menu=16};
    static constexpr double idleMilliseconds=3000,fadeMilliseconds=650;
private:
    bool shown=false;
    double lastAction=0;
    std::uint8_t engaged=0;
    Form currentForm=Form::full;
    Choice userChoice=Choice::automatic;
public:
    // Always call on a real node selection, including reselecting the same node.
    void select(double now) noexcept {
        shown=true;lastAction=now;engaged=0;
        currentForm=Form::full;userChoice=Choice::automatic;
    }
    void hide() noexcept {shown=false;engaged=0;}
    Phase phase(double now) const noexcept {
        if(!shown)return Phase::hidden;
        if(engaged)return Phase::visible;
        const double age=std::max(0.,now-lastAction);
        return age>=idleMilliseconds+fadeMilliseconds?Phase::hidden:
            age>=idleMilliseconds?Phase::fading:Phase::visible;
    }
    double fade(double now) const noexcept {
        if(phase(now)==Phase::hidden)return 0;
        if(engaged)return 1;
        return std::clamp(1-(now-lastAction-idleMilliseconds)/fadeMilliseconds,0.,1.);
    }
    // Hover affects opacity only. It never writes the inactivity deadline,
    // cancels a fade, or wakes an expired/hidden card.
    double backgroundAlpha(double now,bool hovered) const noexcept {
        return fade(now)*(hovered || engaged?1:.72);
    }
    bool acceptsInput(double now) const noexcept {return phase(now)!=Phase::hidden;}
    void interact(double now) noexcept {
        if(acceptsInput(now))lastAction=now;
    }
    void activity(Activity activity,bool active,double now) noexcept {
        const auto bit=static_cast<std::uint8_t>(activity);
        if(active){
            if(!acceptsInput(now)){shown=false;return;}
            if(!(engaged&bit)){engaged|=bit;lastAction=now;}
        }else if(engaged&bit){
            engaged&=static_cast<std::uint8_t>(~bit);
            if(!engaged)lastAction=now; // resume with a fresh full idle period
        }
    }
    bool active(Activity activity) const noexcept {return engaged&static_cast<std::uint8_t>(activity);}
    bool layoutLocked() const noexcept {
        constexpr auto lock=static_cast<std::uint8_t>(Activity::knobDrag)|
            static_cast<std::uint8_t>(Activity::textFocus)|
            static_cast<std::uint8_t>(Activity::solo)|static_cast<std::uint8_t>(Activity::menu);
        return engaged&lock;
    }
    void choose(Form form,double now) noexcept {
        if(!acceptsInput(now))return;
        currentForm=form;userChoice=form==Form::full?Choice::full:Choice::compact;
        lastAction=now;
    }
    void automaticForm(Form form) noexcept {
        if(userChoice==Choice::automatic)currentForm=form;
    }
    Form form() const noexcept {return currentForm;}
    Choice choice() const noexcept {return userChoice;}
};
}
