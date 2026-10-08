#include "ReverbEngine.hpp"
#include <limits>
namespace just::reverb {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr std::size_t ix(ID id) {
    for(std::size_t i=0;i<std::size(parameters);++i)if(parameters[i].id==id)return i;
    return 0;
}
double db(double v) noexcept {return std::pow(10.,v/20.);}
double safeHz(double f,double fs) noexcept {return std::clamp(f,1.,fs*.45);}
// The exact maximum magnitude of two first-order shelves. In the bilinear
// frequency variable t=tan(w/2)^2, |H|^2 is a ratio of quadratics. Evaluate
// the endpoints and every positive stationary point, rather than assuming
// that the endpoint gains alone guarantee feedback stability.
double shelfBound(double gl,double gm,double gh,double wl,double wh) noexcept {
    const double a=wl*wl,b=wh*wh,r=gh/gm;
    const double n2=gm*gm*r*r,n1=gm*gm*b+gl*gl*a*r*r,n0=gl*gl*a*b;
    const double d1=a+b,d0=a*b;
    double maximum=std::max(gl*gl,gh*gh);
    auto test=[&](double t){if(t>0 && std::isfinite(t))maximum=std::max(maximum,(n2*t*t+n1*t+n0)/(t*t+d1*t+d0));};
    const double A=n2*d1-n1,B=2*(n2*d0-n0),C=n1*d0-n0*d1;
    if(std::abs(A)<1e-30){if(std::abs(B)>1e-30)test(-C/B);}
    else if(B*B-4*A*C>=0) {
        const double disc=std::sqrt(B*B-4*A*C);
        test((-B+disc)/(2*A));test((-B-disc)/(2*A));
    }
    return std::sqrt(maximum);
}
}
double DelayLine::read(double samples) const noexcept {
    if(data.empty() || !std::isfinite(samples))return 0;
    samples=std::clamp(samples,1.,double(data.size()-2));
    const auto n=std::size_t(samples);const double frac=samples-n;
    if(n>filled)return 0;
    auto at=[&](std::size_t age){return age<=filled?data[(write+data.size()-age)%data.size()]:0.;};
    return at(n)*(1-frac)+at(n+1)*frac;
}
void DelayLine::push(double x) noexcept {
    if(data.empty())return;
    data[write]=std::abs(x)<1e-30?0:x;
    write=(write+1)%data.size();filled=std::min(filled+1,data.size()-1);
}
void OnePoleShelf::configure(double low,double high,double f,double fs) noexcept {
    const double k=std::tan(pi*safeHz(f,fs)/fs);
    b0=(high+low*k)/(1+k);b1=(low*k-high)/(1+k);a1=(k-1)/(k+1);
}
void Biquad::configure(double fs,double f,bool highpass) noexcept {
    const double w=2*pi*safeHz(f,fs)/fs,c=std::cos(w),s=std::sin(w),alpha=s/std::sqrt(2.),a0=1+alpha;
    b0=(highpass?1+c:1-c)/(2*a0);b1=(highpass?-(1+c):1-c)/a0;b2=b0;
    a1=-2*c/a0;a2=(1-alpha)/a0;
}
void Biquad::bell(double fs,double f,double gain,double q) noexcept {
    const double A=std::pow(10.,gain/40.),w=2*pi*safeHz(f,fs)/fs,alpha=std::sin(w)/(2*q),c=std::cos(w),a0=1+alpha/A;
    b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0;
}
void ReverbEngine::Network::prepare(double rate) {
    fs=rate;
    for(auto& d:delay)d.prepare(std::size_t(fs*.24));
    for(auto& d:early)d.prepare(std::size_t(fs*.25));
    for(auto& d:diffuser)d.prepare(std::size_t(fs*.04));
    configure(0,100);clear();
}
void ReverbEngine::Network::configure(int next,double scale) noexcept {
    style=next;size=scale;
    constexpr double lengths[3][8]={
        {19.7,23.9,29.3,31.7,37.1,41.9,47.3,53.9},
        {37.9,43.7,53.3,61.1,67.7,73.1,83.9,97.3},
        {17.3,21.7,26.9,32.3,38.9,44.3,51.1,59.3}};
    for(std::size_t i=0;i<8;++i)length[i]=std::max(4.,std::round(lengths[style][i]*.001*fs*size/100.));
    for(std::size_t i=0;i<4;++i)diffuserLength[i]=std::size_t(std::max(2.,std::round((2.3+i*1.63)*(style==1?1.8:style==2?.7:1.)*.001*fs*size/100.)));
}
void ReverbEngine::Network::clear() noexcept {
    for(auto& d:delay)d.clear();for(auto& d:diffuser)d.clear();for(auto& d:early)d.clear();
    for(auto& f:lowShelf)f.clear();for(auto& f:highShelf)f.clear();
    for(std::size_t i=0;i<8;++i)phase[i]=i*.73;
    recovery=1;protection=false;
}
void ReverbEngine::Network::damping(double freeze,const std::array<double,std::size(parameters)>& v) noexcept {
    const double low=safeHz(v[ix(LowXover)],fs),high=std::max(low+1,safeHz(v[ix(HighXover)],fs));
    for(std::size_t i=0;i<8;++i) {
        auto gain=[&](ID multiplier){
            const double rt=std::clamp(v[ix(Decay)]*v[ix(multiplier)],.1,20.);
            // Freeze uses fixed-delay, bounded quasi-lossless feedback. No
            // claim of exact mathematical infinite preservation is made.
            return std::pow(10.,-3*length[i]/(fs*rt))*(1-freeze)+.999999*freeze;
        };
        double gl=gain(LowDecay),gm=gain(MidDecay),gh=gain(HighDecay);
        double bound=shelfBound(gl,gm,gh,std::tan(pi*low/fs),std::tan(pi*high/fs));
        double scale=std::min(1.,.999999/std::max(bound,1e-20));
        lowShelf[i].configure(gl*scale,gm*scale,low,fs);
        highShelf[i].configure(1,gh/gm,high,fs);
    }
}
std::array<double,2> ReverbEngine::Network::tick(double left,double right,const std::array<double,std::size(parameters)>& v,double freeze) noexcept {
    const double inject=1-freeze;left*=inject;right*=inject;
    early[0].push(left);early[1].push(right);
    std::array<double,2> er{};
    constexpr double earlyMs[12]={3.1,5.3,7.9,11.3,13.7,17.9,23.3,29.9,37.1,43.7,53.3,67.1};
    for(std::size_t i=0;i<12;++i) {
        const double scale=(style==1?1.7:style==2?.35:1.)*size/100.;
        const double gain=(style==2?.12:.28)*std::exp(-double(i)*.12);
        er[0]+=gain*early[i%2].read(earlyMs[i]*scale*.001*fs);
        er[1]+=gain*early[1-i%2].read((earlyMs[i]*scale+.31)*.001*fs);
    }
    // Style-specific density build-up: Hall has longer diffusion delays and
    // four stages; Room two; Plate four shorter stages with stronger diffusion.
    double injection=(left+right)*.5;
    const double ap=.15+.6*v[ix(Diffusion)]*.01;
    for(std::size_t i=0;i<(style==0?2u:4u);++i) {
        double delayed=diffuser[i].read(double(diffuserLength[i]));
        double y=delayed-ap*injection;diffuser[i].push(injection+ap*y);injection=y;
    }
    std::array<double,8> taps{},mixed{};
    for(std::size_t i=0;i<8;++i) {
        const double depth=std::min(v[ix(ModDepth)]*.001*fs,length[i]*.025)*(1-freeze);
        taps[i]=delay[i].read(length[i]+depth*std::sin(phase[i]));
        phase[i]+=2*pi*v[ix(ModRate)]*(1+i*.031)/fs;
        if(phase[i]>2*pi)phase[i]-=2*pi;
        mixed[i]=highShelf[i].tick(lowShelf[i].tick(taps[i]));
    }
    // Orthonormal 8x8 Hadamard. The matrix never supplies feedback gain.
    for(std::size_t n=1;n<8;n*=2)for(std::size_t base=0;base<8;base+=2*n)for(std::size_t j=0;j<n;++j) {
        const double a=mixed[base+j],b=mixed[base+j+n];mixed[base+j]=a+b;mixed[base+j+n]=a-b;
    }
    constexpr double norm=.3535533905932737622;
    bool failed=false;
    for(std::size_t i=0;i<8;++i) {
        double x=mixed[i]*norm+injection*(i%2?-.18:.18)+(i%2?right:left)*.055;
        // Emergency bound is dormant on normal audio; it prevents propagation
        // of a nonfinite network and caps abnormal frozen energy.
        if(!std::isfinite(x) || std::abs(x)>64){x=0;failed=true;}
        delay[i].push(std::clamp(x,-64.,64.));
    }
    if(failed){clear();protection=true;recovery=0;return {};}
    recovery=std::min(1.,recovery+1/(fs*.01));
    std::array<double,2> late{
        (taps[0]+taps[2]-taps[4]+taps[6])*.65,
        (taps[1]-taps[3]+taps[5]+taps[7])*.65};
    const double balance=v[ix(EarlyLate)]*.01;
    for(std::size_t ch=0;ch<2;++ch)late[ch]=(er[ch]*std::cos(balance*pi*.5)+late[ch]*std::sin(balance*pi*.5))*recovery;
    return late;
}
bool ReverbEngine::prepare(const PrepareSpec& next) {
    if(!next.valid() || next.sampleRate<8000 || next.sampleRate>384000)return false;
    spec=next;
    for(auto& n:networks)n.prepare(spec.sampleRate);
    for(auto& d:predelay)d.prepare(std::size_t(spec.sampleRate*4.01));
    for(auto& f:inputHP)f.configure(spec.sampleRate,20,true);
    prepared=true;targetsInitialized=false;reset(ResetReason::firstActivation);return true;
}
void ReverbEngine::reset(ResetReason reason) noexcept {
    if(reason==ResetReason::seek)return; // default seek policy preserves tails
    for(auto& n:networks)n.clear();for(auto& d:predelay)d.clear();
    for(auto& f:inputHP)f.clear();for(auto& f:hp)f.clear();for(auto& f:lp)f.clear();for(auto& f:bell1)f.clear();for(auto& f:bell2)f.clear();
    active=0;fade=1;duckEnvelope=0;predelayTarget=-1;current={};effective={};coefficientCountdown=0;
    if(targetsInitialized)for(std::size_t i=0;i<value.size();++i){value[i]=target[i];smoother[i].reset(value[i]);}
    requestedStyle=targetsInitialized?int(target[ix(Style)]):0;requestedSize=targetsInitialized?target[ix(Size)]:100;
    for(auto& n:networks)n.configure(requestedStyle,requestedSize);
}
void ReverbEngine::applyTargets(const SoundState& state,std::int32_t) noexcept {
    for(std::size_t i=0;i<std::size(parameters);++i) {
        const double next=state.targets[i]==parameters[i].toNormalized(parameters[i].initial)?parameters[i].initial:parameters[i].toPhysical(state.targets[i]);
        if(!targetsInitialized){target[i]=value[i]=next;smoother[i].reset(next);}
        else if(next!=target[i]) {
            target[i]=next;
            // Discrete style/sync events are handled separately. Freeze and
            // Bypass are discrete host controls with smooth audio transitions.
            const bool smooth=parameters[i].stepCount==0 || parameters[i].id==Freeze || parameters[i].id==Bypass || parameters[i].id==Bell1On || parameters[i].id==Bell2On;
            smoother[i].setTarget(next,smooth?std::uint32_t(spec.sampleRate*parameters[i].smoothingMs*.001):0);
        }
    }
    if(!targetsInitialized) {
        for(auto& n:networks)n.configure(int(target[ix(Style)]),target[ix(Size)]);
        targetsInitialized=true;updateCoefficients();
    }
    requestNetwork(int(target[ix(Style)]),target[ix(Size)]);
}
void ReverbEngine::requestNetwork(int style,double size) noexcept {requestedStyle=style;requestedSize=size;}
void ReverbEngine::updateCoefficients() noexcept {
    for(auto& n:networks)n.damping(value[ix(Freeze)],value);
    for(std::size_t i=0;i<2;++i) {
        hp[i].configure(spec.sampleRate,value[ix(WetHP)],true);lp[i].configure(spec.sampleRate,value[ix(HighCut)],false);
        bell1[i].bell(spec.sampleRate,value[ix(Bell1Freq)],value[ix(Bell1Gain)]*value[ix(Bell1On)],value[ix(Bell1Q)]);
        bell2[i].bell(spec.sampleRate,value[ix(Bell2Freq)],value[ix(Bell2Gain)]*value[ix(Bell2On)],value[ix(Bell2Q)]);
    }
}
template<class Sample> void ReverbEngine::render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
    if(!prepared || !targetsInitialized)return;
    effective.syncUnavailable=target[ix(Sync)]>=.5 && (!context.tempoValid || !std::isfinite(context.bpm) || context.bpm<=0);
    double pre=target[ix(Predelay)];
    if(target[ix(Sync)]>=.5 && !effective.syncUnavailable)pre=60000/context.bpm*divisionBeats[std::size_t(target[ix(Division)])];
    effective.syncClamped=pre>4000;pre=std::clamp(pre,0.,4000.);effective.predelayMs=pre;
    // Tempo and sample-accurate sync events reuse the pre-delay smoother;
    // never turn Sync off or replace the stored free-time target.
    if(pre!=predelayTarget){predelayTarget=pre;smoother[ix(Predelay)].setTarget(pre,std::uint32_t(spec.sampleRate*.02));}
    for(std::uint32_t frame=0;frame<block.samples;++frame) {
        bool analysisValid=true;
        for(std::size_t p=0;p<value.size();++p)value[p]=smoother[p].tick();
        if(!coefficientCountdown){updateCoefficients();coefficientCountdown=32;}--coefficientCountdown;
        if(fade>=1 && (networks[active].style!=requestedStyle || std::abs(networks[active].size-requestedSize)>.01)) {
            const int next=1-active;networks[next].clear();networks[next].configure(requestedStyle,requestedSize);networks[next].damping(value[ix(Freeze)],value);fade=0;
        }
        std::array<double,2> raw{};
        for(std::size_t ch=0;ch<2;++ch) {
            const auto source=block.inputChannels==1?0u:unsigned(ch);
            if(source<block.inputChannels && block.inputs[source] && !(block.inputSilenceFlags&(1ull<<source)))raw[ch]=double(block.inputs[source][frame]);
            if(!std::isfinite(raw[ch])){raw[ch]=0;current.invalidInput=true;analysisValid=false;}
            // Only abnormal magnitudes are bounded. Bypass still returns the
            // finite original value; float overflow is caught at output.
            current.inputPeak=std::max(current.inputPeak,std::abs(raw[ch]));
        }
        const double inputGain=db(value[ix(InputTrim)]),outputGain=db(value[ix(OutputTrim)]);
        std::array<double,2> delayed{};
        for(std::size_t ch=0;ch<2;++ch) {
            const double x=inputHP[ch].tick(std::clamp(raw[ch]*inputGain,-64.,64.));
            // Read before write: zero delay is the current sample; an integer
            // delay of N is exactly N samples, with no hidden extra frame.
            const double delay=std::max(0.,value[ix(Predelay)]*spec.sampleRate*.001);
            if(delay<1)delayed[ch]=x*(1-delay)+predelay[ch].read(1)*delay;
            else delayed[ch]=predelay[ch].read(delay);
            predelay[ch].push(x);
        }
        auto wet=networks[active].tick(delayed[0],delayed[1],value,value[ix(Freeze)]);
        if(fade<1) {
            auto incoming=networks[1-active].tick(delayed[0],delayed[1],value,value[ix(Freeze)]);
            for(std::size_t ch=0;ch<2;++ch)wet[ch]=wet[ch]*(1-fade)+incoming[ch]*fade;
            fade=std::min(1.,fade+1/(spec.sampleRate*.1));if(fade>=1)active=1-active;
        }
        if(networks[0].protection || networks[1].protection){effective.protection=true;current.invalidInput=true;analysisValid=false;}
        for(std::size_t ch=0;ch<2;++ch)wet[ch]=bell2[ch].tick(bell1[ch].tick(lp[ch].tick(hp[ch].tick(wet[ch]))));
        const double mid=(wet[0]+wet[1])*.5,side=(wet[0]-wet[1])*.5*value[ix(Width)]*.01;
        wet[0]=mid+side;wet[1]=mid-side;
        const double detector=std::max(std::abs(raw[0]),std::abs(raw[1]));
        const double time=detector>duckEnvelope?value[ix(DuckAttack)]:value[ix(DuckRelease)];
        const double coeff=std::exp(-1/(spec.sampleRate*time*.001));duckEnvelope=detector+(duckEnvelope-detector)*coeff;
        if(duckEnvelope<1e-30)duckEnvelope=0;
        const double over=20*std::log10(std::max(duckEnvelope,1e-15))-value[ix(DuckThreshold)];
        const double reduction=over<-3?0:over>3?over*.5:(over+3)*(over+3)/24;
        effective.duckReductionDb=std::min(value[ix(DuckAmount)],reduction);
        const double wetLevel=value[ix(WetLevel)]<=-90?0:db(value[ix(WetLevel)]-effective.duckReductionDb);
        const double mix=value[ix(Mix)]*.01,bypass=value[ix(Bypass)];
        if(block.outputChannels==1)wet[0]=(wet[0]+wet[1])*.5;
        EffectAnalysisSample measured{};
        for(std::size_t ch=0;ch<block.outputChannels && ch<2;++ch) {
            const double contribution=wet[ch]*wetLevel*mix*outputGain*(1-bypass);
            double y=((raw[ch]*inputGain)*(1-mix)+wet[ch]*wetLevel*mix)*outputGain;
            y=y*(1-bypass)+raw[ch]*bypass;
            if(!std::isfinite(y) || std::abs(y)>std::numeric_limits<Sample>::max()) {y=0;effective.protection=true;current.invalidInput=true;analysisValid=false;}
            if(!std::isfinite(contribution))analysisValid=false;
            else measured.wet[ch]=contribution;
            effective.wetPeak=std::max(effective.wetPeak,std::abs(wet[ch]));
            current.outputPeak=std::max(current.outputPeak,std::abs(y));if(block.outputs[ch])block.outputs[ch][frame]=Sample(y);
        }
        if(context.analysis) {
            if(analysisValid)measured.validFields=analysisWet;
            context.analysis->pushSample(context.blockSampleOffset+frame,measured);
        }
    }
}
Tail ReverbEngine::tailSamples() const noexcept {
    if(target[ix(Freeze)]>=.5 || value[ix(Freeze)]>.000001)return {TailKind::infinite,0};
    const double rt=std::clamp(target[ix(Decay)]*std::max({target[ix(LowDecay)],target[ix(MidDecay)],target[ix(HighDecay)]}),.1,20.);
    // Reserve the full sync capacity and longest changing network. A host is
    // allowed to query this before valid tempo is delivered to process().
    const double seconds=4.0+rt*100/60.+.5;
    return {TailKind::finite,std::uint32_t(std::min(seconds*spec.sampleRate,double(0xfffffffeu)))};
}
}
