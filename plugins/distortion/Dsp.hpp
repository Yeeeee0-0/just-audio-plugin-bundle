#pragma once
#include "Parameters.hpp"
#include <limits>
namespace just::distortion {
inline constexpr double pi=3.14159265358979323846;
enum class Model {Clean,Soft,Hard,Asym,Fold,Crush};
inline double dbGain(double dB) noexcept {return std::pow(10.0,dB/20.0);}
inline double cleanTiny(double x) noexcept {return std::abs(x)<1e-18?0:x;}
// Original, explicit static transfer functions. Bias is meaningful only in Asym/Fold.
inline double transfer(Model m,double u,double bias,double shape) noexcept {
    u=std::clamp(u,-1e6,1e6);bias=std::clamp(bias,-.5,.5);shape=std::clamp(shape,0.0,1.0);
    switch(m) {
    case Model::Clean:return u;
    case Model::Soft:{double k=.5+shape;return std::tanh(k*u)/k;}
    case Model::Hard:{
        double width=.5*shape,a=std::abs(u),v;
        if(!width || a<=1-width)v=std::min(a,1.0);
        else if(a>=1+2*width)v=1;
        else {double t=(a-(1-width))/(3*width);v=1-width+width*(3*t-3*t*t+t*t*t);}
        return std::copysign(v,u);
    }
    case Model::Asym:{
        auto f=[shape](double x){double threshold=x>=0?1-.5*shape:1;return threshold*std::tanh(x/threshold);};
        return f(u+bias)-f(bias);
    }
    case Model::Fold:{double k=pi*(.5+1.5*shape);return (std::sin(k*(u+bias))-std::sin(k*bias))/k;}
    case Model::Crush:return u;
    }
    return 0;
}
// Exact integral of this module's Hard cubic. Even primitive, odd derivative.
inline double hardPrimitive(double x,double shape) noexcept {
    double a=std::abs(x),w=.5*shape,l=1-w,h=1+2*w;
    if(!w)return a<=1?.5*a*a:a-.5;
    if(a<=l)return .5*a*a;
    double t=std::min(1.,(a-l)/(3*w));
    double area=.5*l*l+3*w*(l*t+w*(1.5*t*t-t*t*t+.25*t*t*t*t));
    return a>h?area+a-h:area;
}
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void reset() noexcept {z1=z2=0;}
    double tick(double x) noexcept {double y=b0*x+z1;z1=cleanTiny(b1*x-a1*y+z2);z2=cleanTiny(b2*x-a2*y);return cleanTiny(y);}
    void pass(double hz,double fs,bool high) noexcept {
        double w=2*pi*std::clamp(hz,1.0,.45*fs)/fs,c=std::cos(w),s=std::sin(w),alpha=s/std::sqrt(2.0),a0=1+alpha;
        b0=(high?(1+c):(1-c))*.5/a0;b1=(high?-(1+c):(1-c))/a0;b2=b0;a1=-2*c/a0;a2=(1-alpha)/a0;
    }
    void shelf(double hz,double fs,double dB,bool high) noexcept {
        double A=std::pow(10.0,dB/40),w=2*pi*std::min(hz,.45*fs)/fs,c=std::cos(w),s=std::sin(w),t=std::sqrt(2*A)*s;
        double a0;
        if(high){
            b0=A*((A+1)+(A-1)*c+t);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-t);
            a0=(A+1)-(A-1)*c+t;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-t;
        } else {
            b0=A*((A+1)-(A-1)*c+t);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-t);
            a0=(A+1)+(A-1)*c+t;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-t;
        }
        b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0;
    }
};
template<std::size_t N> struct Delay {
    std::array<double,N> memory{};std::size_t at=0;
    double tick(double x,std::size_t length) noexcept {
        if(!length)return x;
        double out=memory[at];memory[at]=x;if(++at==length)at=0;return out;
    }
    void reset() noexcept {memory.fill(0);at=0;}
};
// 129-tap Blackman-windowed sinc; polyphase interpolation + base-rate decimation.
// Each stage's center is 64 high-rate samples. Total delay = 128 / factor.
struct Oversampler {
    static constexpr unsigned taps=129;
    std::array<double,taps> coefficients{},input{},high{};
    unsigned inputAt=0,highAt=0,factor=4;
    double previousHard=0,previousAdaa=0;
    std::array<double,5> equalizer{};unsigned equalizerAt=0;
    void prepare(unsigned f) noexcept {
        factor=f;double cutoff=.45/f,sum=0;
        for(unsigned n=0;n<taps;++n){
            double x=int(n)-64,w=.42-.5*std::cos(2*pi*n/128)+.08*std::cos(4*pi*n/128);
            coefficients[n]=w*(x==0?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x));sum+=coefficients[n];
        }
        for(auto& v:coefficients)v/=sum;
        // Preserve DC gain exactly for each interpolation phase.
        for(unsigned phase=0;phase<f;++phase){double s=0;for(unsigned n=phase;n<taps;n+=f)s+=coefficients[n];for(unsigned n=phase;n<taps;n+=f)coefficients[n]/=(f*s);}
        reset();
    }
    void reset() noexcept {input.fill(0);high.fill(0);inputAt=highAt=0;previousHard=previousAdaa=0;equalizer.fill(0);equalizerAt=0;}
    template<class F> double tick(double x,F shape,unsigned emitPhase=0) noexcept {
        if(factor==1)return shape(x);
        inputAt=(inputAt+1)%taps;input[inputAt]=x;double out=0;
        for(unsigned phase=0;phase<factor;++phase){
            double up=0;
            for(unsigned n=phase;n<taps;n+=factor)up+=coefficients[n]*input[(inputAt+taps-n/factor)%taps];
            highAt=(highAt+1)%taps;high[highAt]=shape(up*factor);
            if(phase==emitPhase)for(unsigned n=0;n<taps;++n)out+=coefficients[n]*high[(highAt+taps-n)%taps];
        }
        return cleanTiny(out);
    }
    double hard(double x,double shape,bool antialias=true) noexcept {
        if(factor!=4 || !antialias)return tick(x,[shape](double u){return transfer(Model::Hard,u,0,shape);});
        return tick(x,[&](double u){
            u=std::clamp(u,-1e6,1e6);double delta=u-previousHard;
            double y=std::abs(delta)<1e-6*std::max({1.,std::abs(u),std::abs(previousHard)})?
                transfer(Model::Hard,.5*(u+previousHard),0,shape):(hardPrimitive(u,shape)-hardPrimitive(previousHard,shape))/delta;
            previousHard=u;double half=.5*(y+previousAdaa);previousAdaa=y;
            equalizerAt=(equalizerAt+1)%5;equalizer[equalizerAt]=half;
            static constexpr double correction[]={.0625,-.5,1.875,-.5,.0625};double result=0;
            for(unsigned n=0;n<5;++n)result+=correction[n]*equalizer[(equalizerAt+5-n)%5];
            return result;
        },3); // ADAA0.5 + average0.5 + correction2 =3 high samples; phase3 cancels.
    }
};
}
