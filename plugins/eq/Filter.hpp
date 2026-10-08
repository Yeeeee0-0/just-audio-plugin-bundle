#pragma once
#include "Parameters.hpp"
#include <complex>
namespace just::eq {
inline constexpr double pi=3.14159265358979323846;
inline double effectiveFrequency(double hz,double fs) noexcept {return std::clamp(hz,std::min(20.0,fs*0.01),std::min(20000.0,fs*0.45));}
struct Coefficients {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    double magnitude(double hz,double fs) const noexcept {
        auto z=std::polar(1.0,-2*pi*hz/fs);
        return std::abs((b0+b1*z+b2*z*z)/(1.0+a1*z+a2*z*z));
    }
};
// Original implementation of the published RBJ/W3C Audio EQ Cookbook equations.
inline Coefficients design(Shape shape,double hz,double gain,double q,double fs) noexcept {
    if(int(shape)<=2 && gain==0)return {};
    hz=effectiveFrequency(hz,fs);q=std::clamp(q,0.1,20.0);
    double w=2*pi*hz/fs,c=std::cos(w),s=std::sin(w),alpha=s/(2*q),A=std::pow(10.0,gain/40.0);
    double b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;
    switch(shape) {
    case Shape::bell: b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;break;
    case Shape::notch: b0=1;b1=-2*c;b2=1;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case Shape::highPass: b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case Shape::lowPass: b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case Shape::lowShelf: {
        double t=2*std::sqrt(A)*alpha;
        b0=A*((A+1)-(A-1)*c+t);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-t);
        a0=(A+1)+(A-1)*c+t;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-t;break;
    }
    case Shape::highShelf: {
        double t=2*std::sqrt(A)*alpha;
        b0=A*((A+1)+(A-1)*c+t);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-t);
        a0=(A+1)-(A-1)*c+t;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-t;break;
    }}
    return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}
inline Coefficients firstOrder(bool high,double hz,double fs) noexcept {
    double k=std::tan(pi*effectiveFrequency(hz,fs)/fs),d=1/(1+k);
    return high?Coefficients{d,-d,0,(k-1)*d,0}:Coefficients{k*d,k*d,0,(k-1)*d,0};
}
inline Coefficients bandpass(double hz,double q,double fs) noexcept {
    double w=2*pi*effectiveFrequency(hz,fs)/fs,a=std::sin(w)/(2*q),d=1/(1+a);
    return {a*d,0,-a*d,-2*std::cos(w)*d,(1-a)*d};
}
struct Biquad {
    Coefficients c{};double x1=0,x2=0,y1=0,y2=0;
    void reset() noexcept {x1=x2=y1=y2=0;}
    double tick(double x) noexcept {
        double y=c.b0*x+c.b1*x1+c.b2*x2-c.a1*y1-c.a2*y2;
        if(!std::isfinite(y)){reset();return 0;}
        x2=x1;x1=x;y2=y1;y1=std::abs(y)<1e-30?0:y;return y1;
    }
};
struct FilterBank {
    // Fixed storage accommodates the reviewed 96 dB/oct extension without any
    // audio-thread allocation. Internal modes 0..4 retain the legacy path.
    std::array<Biquad,8> sections{};int count=1;
    void reset() noexcept {for(auto& s:sections)s.reset();}
    void update(Shape shape,double hz,double gain,double q,int slope,double fs) noexcept {
        count=1;
        if(shape==Shape::highPass || shape==Shape::lowPass) {
            bool high=shape==Shape::highPass;
            if(slope==0){sections[0].c=firstOrder(high,hz,fs);return;}
            if(slope==5){ // 18 dB/oct: one real pole plus a Butterworth pair.
                count=2;sections[0].c=firstOrder(high,hz,fs);
                sections[1].c=design(shape,hz,0,q/0.7071067811865476,fs);return;
            }
            if(slope>=6){
                count=slope==6?6:8;const int order=count*2;
                for(int i=0;i<count;++i){double butterworthQ=1/(2*std::cos((2*i+1)*pi/(2*order)));sections[i].c=design(shape,hz,0,butterworthQ*q/0.7071067811865476,fs);}
                return;
            }
            count=slope;int order=count*2;
            for(int i=0;i<count;++i) {
                double butterworthQ=1/(2*std::cos((2*i+1)*pi/(2*order)));
                sections[i].c=design(shape,hz,0,butterworthQ*q/0.7071067811865476,fs);
            }
        } else sections[0].c=design(shape,hz,gain,q,fs);
    }
    double tick(double x) noexcept {for(int i=0;i<count;++i)x=sections[i].tick(x);return x;}
    double db(double hz,double fs) const noexcept {
        double mag=1;for(int i=0;i<count;++i)mag*=sections[i].c.magnitude(hz,fs);
        return 20*std::log10(std::max(1e-12,mag));
    }
};
inline double downwardGain(double db,double threshold,double knee,double range) noexcept {
    double x=db-threshold,over=0;
    if(knee>0 && x>-knee/2 && x<knee/2)over=(x+knee/2)*(x+knee/2)/(2*knee);
    else if(x>=knee/2)over=x;
    return -std::min(range,std::max(0.0,over*0.5));
}
}
