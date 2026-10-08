#pragma once
#include "AppliedAttenuation.hpp"
#include "common/dsp/Engine.hpp"
#include "Parameters.hpp"
#include <vector>
namespace just::limiter {
inline constexpr double pi=3.14159265358979323846;
inline double sinc(double x) noexcept {return std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);}
inline double blackman(double x,double radius) noexcept {
 return std::abs(x)>=radius?0:0.42+0.5*std::cos(pi*x/radius)+0.08*std::cos(2*pi*x/radius);
}
struct Kernels {
 std::array<std::array<double,17>,4> up{};
 std::array<double,65> down{};
 std::array<std::array<double,33>,8> guard{};
 void prepare() noexcept {
  for(unsigned p=0;p<4;++p) {
   double sum=0;
   for(unsigned k=0;k<17;++k){double d=double(4*k+p)-32;up[p][k]=sinc(d/4)*blackman(d,32);sum+=up[p][k];}
   for(auto& h:up[p])h/=sum;
  }
  double sum=0;
  for(unsigned k=0;k<65;++k){double d=double(k)-32;down[k]=0.25*sinc(d/4)*blackman(d,32);sum+=down[k];}
  for(auto& h:down)h/=sum;
  for(unsigned p=0;p<8;++p) {
   sum=0;
   for(unsigned k=0;k<33;++k){double d=double(k)-16-double(p)/8;guard[p][k]=sinc(d)*blackman(d,16);sum+=guard[p][k];}
   for(auto& h:guard[p])h/=sum;
  }
 }
};
// Preallocated trees provide bounded O(log capacity) variable-window queries.
class Peaks {
 std::size_t capacity=0;
 std::vector<double> tree;
 double range(std::size_t a,std::size_t b) const noexcept {
  double result=0;
  for(a+=capacity,b+=capacity+1;a<b;a/=2,b/=2){if(a&1)result=std::max(result,tree[a++]);if(b&1)result=std::max(result,tree[--b]);}
  return result;
 }
public:
 void prepare(std::size_t n){capacity=n;tree.assign(n*2,0);}
 void clear() noexcept {std::fill(tree.begin(),tree.end(),0);}
 void set(std::size_t at,double value) noexcept {at+=capacity;tree[at]=value;while((at/=2)>0)tree[at]=std::max(tree[at*2],tree[at*2+1]);}
 double query(std::size_t from,std::size_t length) const noexcept {
  const auto end=from+length;
  return end<capacity?range(from,end):std::max(range(from,capacity-1),range(0,end-capacity));
 }
};
struct MeterSnapshot {double inputPeak=0,outputPeak=0,maxGainReductionDb=0;std::uint32_t latency=0;bool invalidInput=false;};
class LegacyLimiterEngine final:public Engine {
 struct Frame {std::array<double,2> raw{};std::array<std::array<double,4>,2> up{};};
 Kernels kernels;
 std::vector<Frame> frames;
 std::vector<std::array<double,2>> rawDelay;
 Peaks mixPeaks,livePeaks;
 std::array<std::array<double,17>,2> inputHistory{};
 std::array<std::array<double,65>,2> downHistory{};
 std::array<std::array<double,2>,16> liveDelay{};
 std::array<std::array<double,2>,64> candidates{};
 std::array<double,64> bounds{};
 std::size_t frameMask=0,frameAt=0,rawAt=0,inputAt=0,downAt=0,liveAt=0,candidateAt=0;
 std::uint32_t maximumLookahead=0,latency=0;
 double sampleRate=48000,ceilingLinear=1,mixGain=1,liveGain=1;
 double targetInput=0,targetTrim=0,targetOutput=0,targetRelease=200,targetTransient=50,targetLookahead=2,targetMode=0,targetBypass=0;
 bool prepared=false,targetsInitialized=false;
 LinearSmoother inputDb,trimDb,outputDb,releaseMs,transientValue,lookaheadValue,modeValue,bypassValue;
 SpscQueue<Telemetry,8> telemetry;
 MeterSnapshot meter{};
 AppliedAttenuation appliedAttenuation;
 double lastReductionDb=0;
 AtomicSnapshot<MeterSnapshot> meterPublication;
 static double dbGain(double d) noexcept{return std::pow(10.,d/20);}
 void update(LinearSmoother& s,double& old,double next) noexcept {
  if(next!=old){old=next;s.setTarget(next,static_cast<std::uint32_t>(std::ceil(sampleRate*0.005)));}
 }
 double envelope(double peak,double& gain,double releaseValue,double transientValue) noexcept {
  const double allowed=peak>ceilingLinear?ceilingLinear/peak:1;
  const double coefficient=(1-transientValue)*std::exp(-1/(sampleRate*releaseValue*0.001))+
      transientValue*std::exp(-1/(sampleRate*std::min(30.,releaseValue)*0.001));
  gain=std::min(allowed,1-(1-gain)*coefficient);
  if(gain>1-1e-14)gain=1;
  return gain;
 }
 template<class Sample> void render(AudioBlock<Sample> b,const ProcessContext& context) noexcept {
  if(!prepared){for(unsigned ch=0;ch<b.outputChannels && ch<2;++ch)if(b.outputs[ch])std::fill_n(b.outputs[ch],b.samples,Sample(0));return;}
  if(context.seek)reset(ResetReason::seek);
  for(std::uint32_t i=0;i<b.samples;++i) {
   std::array<double,2> raw{};
   for(unsigned ch=0;ch<b.inputChannels && ch<2;++ch) {
    double x=b.inputs[ch] && !(b.inputSilenceFlags&(1ull<<ch))?double(b.inputs[ch][i]):0;
    if(!std::isfinite(x)){x=0;meter.invalidInput=true;}
    raw[ch]=x;meter.inputPeak=std::max(meter.inputPeak,std::abs(raw[ch]));
   }
   const auto dry=rawDelay[rawAt];rawDelay[rawAt]=raw;rawAt=(rawAt+1)%rawDelay.size();
   const double amplifier=dbGain(inputDb.tick()+trimDb.tick());
   const double postGain=dbGain(outputDb.tick());
   const double releaseNow=releaseMs.tick(),transientNow=transientValue.tick()/100,lookaheadNow=lookaheadValue.tick();
   const double mix=modeValue.tick(),bypassMix=bypassValue.tick();
   Frame current;double mixPeak=0,livePeak=0;
   for(unsigned ch=0;ch<2;++ch) {
    // Protect gain/FIR arithmetic without modifying finite raw bypass audio.
    current.raw[ch]=std::clamp(raw[ch],-1e12,1e12)*amplifier;inputHistory[ch][inputAt]=current.raw[ch];
    livePeak=std::max(livePeak,std::abs(current.raw[ch]));
    for(unsigned p=0;p<4;++p) {
     double value=0;for(unsigned k=0;k<17;++k)value+=kernels.up[p][k]*inputHistory[ch][(inputAt+17-k)%17];
     current.up[ch][p]=value;mixPeak=std::max(mixPeak,std::abs(value));
    }
   }
   inputAt=(inputAt+1)%17;frames[frameAt]=current;mixPeaks.set(frameAt,mixPeak);livePeaks.set(frameAt,livePeak);
   const auto read=(frameAt+frames.size()-maximumLookahead)&frameMask;
   const auto mixAhead=static_cast<std::size_t>(std::ceil(std::clamp(lookaheadNow,0.1,5.)*sampleRate*0.001));
   const auto liveAhead=static_cast<std::size_t>(std::ceil(std::clamp(lookaheadNow,0.,1.)*sampleRate*0.001));
   const double mg=envelope(mixPeaks.query(read,std::min<std::size_t>(mixAhead,maximumLookahead)),mixGain,releaseNow,transientNow);
   const double lg=envelope(livePeaks.query(read,std::min<std::size_t>(liveAhead,maximumLookahead)),liveGain,releaseNow,transientNow);
   std::array<double,2> down{},live=liveDelay[liveAt];
   for(unsigned ch=0;ch<2;++ch)liveDelay[liveAt][ch]=frames[read].raw[ch]*lg;
   liveAt=(liveAt+1)%16;
   for(unsigned p=0;p<4;++p) {
    for(unsigned ch=0;ch<2;++ch) {
     downHistory[ch][downAt]=frames[read].up[ch][p]*mg;
     if(p==0)for(unsigned k=0;k<65;++k)down[ch]+=kernels.down[k]*downHistory[ch][(downAt+65-k)%65];
    }
    downAt=(downAt+1)%65;
   }
   std::array<double,2> candidate{},out{};
   for(unsigned ch=0;ch<2;++ch)candidate[ch]=live[ch]*(1-mix)+down[ch]*mix;
   candidates[candidateAt]=candidate;
   // Centre t-16 is known at time t. Emitting n=t-32 gives all centres
   // whose 33-tap support includes n, including cross-block FIR context.
   double bound=0;
   for(unsigned p=0;p<8;++p)for(unsigned ch=0;ch<2;++ch) {
    double sum=0;for(unsigned k=0;k<33;++k)sum+=std::abs(kernels.guard[p][k])*std::abs(candidates[(candidateAt+64-32+k)%64][ch]);
    bound=std::max(bound,sum);
   }
   bounds[(candidateAt+64-16)%64]=bound;
   double maximumBound=0;const auto emit=(candidateAt+64-32)%64;
   for(unsigned k=0;k<33;++k)maximumBound=std::max(maximumBound,bounds[(emit+64-16+k)%64]);
   // Every contribution is <= ceiling / absolute-sum bound. Triangle
   // inequality then bounds this 8x FIR reconstruction after downsampling.
   // This conservative prototype is not an ITU/EBU compliance claim.
   double safetyGain=1;
   // Settled Live uses only the sample ceiling; never the conservative Mix
   // absolute-sum bound. This is why low-level Live audio nulls after PDC.
   if(mix>0 || targetMode>0)safetyGain=maximumBound>0?std::min(1.,ceilingLinear*dbGain(-0.2)/maximumBound):1;
   const double samplePeak=std::max(std::abs(candidates[emit][0]),std::abs(candidates[emit][1]));
   if(samplePeak>0)safetyGain=std::min(safetyGain,ceilingLinear/samplePeak);
   double transitionPeak=0;
   for(unsigned ch=0;ch<2;++ch) {
    transitionPeak=std::max(transitionPeak,std::abs(candidates[emit][ch]*safetyGain*(1-bypassMix)+dry[ch]*bypassMix));
    out[ch]=candidates[emit][ch]*safetyGain*postGain;
    out[ch]=out[ch]*(1-bypassMix)+dry[ch]*bypassMix;
   }
   double transitionGain=1;
   if(targetBypass==0 && bypassMix>0) {
    const double cap=ceilingLinear*0.25;
    // Keep the legacy transition protection independent of the new post gain,
    // so an Output edit cannot alter the reported limiter attenuation.
    if(transitionPeak>cap){transitionGain=cap/transitionPeak;for(auto& x:out)x*=transitionGain;}
   }
   const double emittedControl=appliedAttenuation.advance(lg,mg,mix,kernels.down);
   lastReductionDb=AppliedAttenuation::reductionDb(emittedControl,safetyGain,bypassMix,transitionGain);
   meter.maxGainReductionDb=std::max(meter.maxGainReductionDb,lastReductionDb);
   if(context.analysis){EffectAnalysisSample measured;measured.validFields=analysisReduction;measured.reductionDb=lastReductionDb;context.analysis->pushSample(context.blockSampleOffset+i,measured);}
   for(unsigned ch=0;ch<b.outputChannels && ch<2;++ch)if(b.outputs[ch]) {
    double x=std::isfinite(out[ch])?out[ch]:0;
    b.outputs[ch][i]=static_cast<Sample>(x);meter.outputPeak=std::max(meter.outputPeak,std::abs(double(b.outputs[ch][i])));
   }
   candidateAt=(candidateAt+1)%64;frameAt=(frameAt+1)&frameMask;
  }
 }
public:
 bool prepare(const PrepareSpec& spec) override {
  if(!spec.valid() || spec.inputChannels!=spec.outputChannels || spec.sidechainChannels || spec.sampleRate<8000 || spec.sampleRate>384000)return false;
  sampleRate=spec.sampleRate;maximumLookahead=static_cast<std::uint32_t>(std::ceil(sampleRate*0.005));latency=maximumLookahead+48;
  std::size_t size=1;while(size<maximumLookahead+1)size*=2;frameMask=size-1;
  frames.assign(size,{});rawDelay.assign(latency,{});mixPeaks.prepare(size);livePeaks.prepare(size);kernels.prepare();prepared=true;
  reset(ResetReason::firstActivation);return true;
 }
 void reset(ResetReason) noexcept override {
  appliedAttenuation.reset();lastReductionDb=0;
  for(auto& f:frames)f={};for(auto& f:rawDelay)f={};mixPeaks.clear();livePeaks.clear();
  inputHistory={};downHistory={};liveDelay={};candidates={};bounds={};
  frameAt=rawAt=inputAt=downAt=liveAt=candidateAt=0;mixGain=liveGain=1;meter={};meter.latency=latency;
  inputDb.reset(targetInput);trimDb.reset(targetTrim);outputDb.reset(targetOutput);releaseMs.reset(targetRelease);transientValue.reset(targetTransient);lookaheadValue.reset(targetLookahead);modeValue.reset(targetMode);bypassValue.reset(targetBypass);targetsInitialized=false;
 }
 void applyTargets(const SoundState& state,std::int32_t) noexcept override {
  const double a=physical(state,input),b=physical(state,trim),c=physical(state,release),d=physical(state,transient),e=physical(state,lookahead),f=physical(state,mode),g=physical(state,bypass),h=physical(state,output);
  ceilingLinear=dbGain(physical(state,ceiling));
  if(!targetsInitialized) {
   targetInput=a;targetTrim=b;targetOutput=h;targetRelease=c;targetTransient=d;targetLookahead=e;targetMode=f;targetBypass=g;
   inputDb.reset(a);trimDb.reset(b);outputDb.reset(h);releaseMs.reset(c);transientValue.reset(d);lookaheadValue.reset(e);modeValue.reset(f);bypassValue.reset(g);targetsInitialized=true;
  } else {
   update(inputDb,targetInput,a);update(trimDb,targetTrim,b);update(outputDb,targetOutput,h);update(releaseMs,targetRelease,c);update(transientValue,targetTransient,d);update(lookaheadValue,targetLookahead,e);
   if(f!=targetMode){targetMode=f;modeValue.setTarget(f,static_cast<std::uint32_t>(std::ceil(sampleRate*0.01)));}
   if(g!=targetBypass){targetBypass=g;bypassValue.setTarget(g,static_cast<std::uint32_t>(std::ceil(sampleRate*0.005)));}
  }
 }
 void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
 void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
 void endBlock() noexcept override {meterPublication.publish(meter);telemetry.push({meter.inputPeak,meter.outputPeak,latency,meter.invalidInput});meter={};meter.latency=latency;}
 std::uint32_t latencySamples() const noexcept override{return latency;}
 Tail tailSamples() const noexcept override{return {TailKind::finite,latency+16};}
 bool readTelemetry(Telemetry& t) noexcept override{return telemetry.pop(t);}
 double appliedReductionDb() const noexcept{return lastReductionDb;} // audio-thread observation; no UI access
 bool readMeter(MeterSnapshot& s) const noexcept{return meterPublication.read(s);}
};
}
