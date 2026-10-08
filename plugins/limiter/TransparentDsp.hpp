#pragma once
#include "LegacyDsp.hpp"
#include "AlgorithmPolicy.hpp"

namespace just::limiter {
// New explicit algorithm version; LegacyDsp.hpp preserves the 7d sound path.
class TransparentLimiterEngine final:public Engine {
 struct Peak {double value=0;std::int64_t sample=0;};
 class PeakTree {
  std::size_t capacity=0;std::vector<Peak> nodes;
  static Peak maximum(Peak a,Peak b) noexcept{return a.value>b.value || (a.value==b.value&&a.sample<b.sample)?a:b;}
  Peak range(std::size_t a,std::size_t b)const noexcept{Peak p;for(a+=capacity,b+=capacity+1;a<b;a/=2,b/=2){if(a&1)p=maximum(p,nodes[a++]);if(b&1)p=maximum(p,nodes[--b]);}return p;}
 public:
  void prepare(std::size_t n){capacity=n;nodes.assign(2*n,{});}
  void clear() noexcept{std::fill(nodes.begin(),nodes.end(),Peak{});}
  void put(std::size_t at,Peak value)noexcept{at+=capacity;nodes[at]=value;while((at/=2)>0)nodes[at]=maximum(nodes[at*2],nodes[at*2+1]);}
  Peak query(std::size_t start,std::size_t length)const noexcept{const auto end=start+length;return end<capacity?range(start,end):maximum(range(start,capacity-1),range(0,end-capacity));}
 } samplePeaks,truePeaks,candidatePeaks;
 struct Frame {std::array<double,2> raw{},driven{},candidate{};double primaryGain=1;};
 std::vector<Frame> frames;
 std::size_t mask=0;
 std::int64_t time=0;
 std::uint32_t maximumLookahead=0,latency=0;
 double rate=48000,ceilingLinear=1,primaryGain=1,liveGain=1,safetyGain=1;
 double targetInput=0,targetTrim=0,targetOutput=0,targetRelease=200,targetTransient=50,targetLookahead=2,targetMode=0,targetBypass=0;
 LinearSmoother inputDb,trimDb,outputDb,releaseMs,transientValue,lookaheadValue,modeValue,bypassValue;
 bool prepared=false,initialized=false;
 // Tap-major coefficients let the compiler run independent phases in SIMD
 // lanes without changing the accumulation order of any reconstructed sample.
 std::array<std::array<double,32>,129> reconstruction{};
 unsigned radius=64;
 std::array<std::array<double,2>,129> finalHistory{};
 std::size_t finalAt=0;
 double lastReduction=0,outputReconstructionPeak=0;
 MeterSnapshot meter{};AtomicSnapshot<MeterSnapshot> publication;
 SpscQueue<Telemetry,8> telemetry;
 static double gain(double db)noexcept{return std::abs(db)<1e-12?1:std::pow(10.,db/20);}
 std::size_t index(std::int64_t n)const noexcept{return std::size_t(n)&mask;}
 void update(LinearSmoother& smoother,double& old,double value)noexcept{if(old!=value){old=value;smoother.setTarget(value,std::uint32_t(std::ceil(rate*.005)));}}
 double releaseCoefficient(double release,double transient)const noexcept{return (1-transient)*std::exp(-1/(rate*release*.001))+transient*std::exp(-1/(rate*std::min(30.,release)*.001));}
 void makeKernel()noexcept{
  for(unsigned p=0;p<32;++p){double sum=0;for(unsigned k=0;k<129;++k){const double d=double(k)-64-double(p)/32;sum+=(reconstruction[k][p]=sinc(d)*blackman(d,radius));}for(auto& tap:reconstruction)tap[p]/=sum;}
 }
 double plan(Peak future,std::int64_t sample,double currentPeak,double previous,double coefficient,bool tp=false)const noexcept{
  // A 0.2 dB reconstruction reserve is used only after a genuine detector
  // excursion, never as a lowered always-on threshold for quiet material.
  const double protectedCeiling=ceilingLinear*(tp?0.9772372209558107:1);
  const double allowed=future.value>ceilingLinear*(1+1e-12)?protectedCeiling/future.value:1;
  if(allowed<previous){const auto distance=std::max<std::int64_t>(0,future.sample-sample);previous+=(allowed-previous)/double(distance+1);}
  else previous=std::min(allowed,1-(1-previous)*coefficient);
  if(currentPeak>0)previous=std::min(previous,(tp&&currentPeak>ceilingLinear*(1+1e-12)?protectedCeiling:ceilingLinear)/currentPeak);
  return previous>1-1e-14?1:previous;
 }
 template<std::size_t Taps,class Read> double reconstructed(std::int64_t center,const std::array<std::array<double,32>,Taps>& kernel,Read read)const noexcept{
  double peak=0;std::array<std::array<double,Taps>,2> samples{};
  for(unsigned k=0;k<Taps;++k){const auto sample=center-int(Taps/2)+k;samples[0][k]=read(sample,0);samples[1][k]=read(sample,1);}
  // Eight phases at a time fit their stereo accumulators into SIMD registers.
  // Each phase retains exactly k=0..128 sum order; no tap reassociation or
  // fast-math. Cache source samples once for all four phase groups.
  for(unsigned first=0;first<32;first+=8){std::array<std::array<double,8>,2> sums{};
   for(unsigned k=0;k<Taps;++k){const double l=samples[0][k],r=samples[1][k];
    for(unsigned p=0;p<8;++p){sums[0][p]+=kernel[k][first+p]*l;sums[1][p]+=kernel[k][first+p]*r;}
   }
   for(const auto& channel:sums)for(auto value:channel)peak=std::max(peak,std::abs(value));
  }
  return peak;
 }
 template<class Sample> void render(AudioBlock<Sample> b,const ProcessContext& context)noexcept{
  if(!prepared){for(unsigned ch=0;ch<b.outputChannels&&ch<2;++ch)if(b.outputs[ch])std::fill_n(b.outputs[ch],b.samples,Sample(0));return;}
  if(context.seek)reset(ResetReason::seek);
  for(std::uint32_t i=0;i<b.samples;++i,++time){
   const auto current=index(time);auto& frame=frames[current];frame={};frame.primaryGain=1;
   const double amplifier=gain(inputDb.tick()+trimDb.tick()),post=gain(outputDb.tick());
   const double release=releaseMs.tick(),transient=transientValue.tick()/100,lookahead=lookaheadValue.tick();
   const double mix=modeValue.tick(),bypass=bypassValue.tick();
   double peak=0;
   for(unsigned ch=0;ch<2;++ch){double x=ch<b.inputChannels&&b.inputs[ch]&&!(b.inputSilenceFlags&(1ull<<ch))?double(b.inputs[ch][i]):0;if(!std::isfinite(x)){x=0;meter.invalidInput=true;}frame.raw[ch]=x;frame.driven[ch]=std::clamp(x,-1e12,1e12)*amplifier;peak=std::max(peak,std::abs(frame.driven[ch]));meter.inputPeak=std::max(meter.inputPeak,std::abs(x));}
   samplePeaks.put(current,{peak,time});
   // Reconstruction is detector-only: no interpolation/filtering of audio.
   const auto detectorCenter=time-radius;
   const auto tpPeak=reconstructed(detectorCenter,reconstruction,[&](std::int64_t n,unsigned ch){return n<0?0:frames[index(n)].driven[ch];});
   truePeaks.put(index(detectorCenter),{tpPeak,detectorCenter});
   // TP reserves one detector radius and two final-verification radii.
   // Its available lookahead is explicit; Live still has the full 0..5 ms.
   const auto candidateTime=time-(latency-2*radius);
   const auto requested=std::uint32_t(std::ceil(std::clamp(lookahead,0.,5.)*rate*.001));
   const auto tpAhead=std::min(requested,latency-3*radius);
   auto future=samplePeaks.query(index(candidateTime),tpAhead);
   const auto reconstructedFuture=truePeaks.query(index(candidateTime),tpAhead);if(reconstructedFuture.value>future.value)future=reconstructedFuture;
   const double coefficient=releaseCoefficient(release,transient);
   auto& candidate=frames[index(candidateTime)];
   const double currentPeak=std::max(std::abs(candidate.driven[0]),std::abs(candidate.driven[1]));
   primaryGain=plan(future,candidateTime,currentPeak,primaryGain,coefficient,true);
   candidate.primaryGain=primaryGain;for(unsigned ch=0;ch<2;++ch)candidate.candidate[ch]=candidate.driven[ch]*primaryGain;
   const auto candidateCenter=candidateTime-radius;
   const double verifiedCandidate=reconstructed(candidateCenter,reconstruction,[&](std::int64_t n,unsigned ch){return n<0?0:frames[index(n)].candidate[ch];});
   candidatePeaks.put(index(candidateCenter),{verifiedCandidate,candidateCenter});
   const auto emittedTime=time-latency;const auto& emitted=frames[index(emittedTime)];
   const auto candidatePeak=candidatePeaks.query(index(emittedTime-radius),2*radius).value;
   // Signed reconstruction of gain-shaped audio replaces the legacy L1 bound.
   const double allowedSafety=candidatePeak>ceilingLinear*(1+1e-12)?ceilingLinear*0.9772372209558107/candidatePeak:1;
   safetyGain=std::min(allowedSafety,1-(1-safetyGain)*coefficient);if(safetyGain>1-1e-14)safetyGain=1;
   double samplePeak=0;for(auto x:emitted.candidate)samplePeak=std::max(samplePeak,std::abs(x));
   const double sampleGuard=samplePeak>0?std::min(1.,ceilingLinear/samplePeak):1;
   const double finalGain=std::min(safetyGain,sampleGuard);
   const auto liveFuture=samplePeaks.query(index(emittedTime),std::min(requested,maximumLookahead));
   const double livePeak=std::max(std::abs(emitted.driven[0]),std::abs(emitted.driven[1]));
   liveGain=plan(liveFuture,emittedTime,livePeak,liveGain,coefficient);
   const double control=liveGain*(1-mix)+emitted.primaryGain*finalGain*mix;
   std::array<double,2> out{};double reentryPeak=0;
   for(unsigned ch=0;ch<2;++ch){out[ch]=emitted.driven[ch]*control*post*(1-bypass)+emitted.raw[ch]*bypass;reentryPeak=std::max(reentryPeak,std::abs(emitted.driven[ch]*control*(1-bypass)+emitted.raw[ch]*bypass));}
   // Re-entry uses the actual ceiling. Safe below-ceiling input stays intact.
   double transition=1;if(targetBypass==0&&bypass>0&&reentryPeak>ceilingLinear){transition=ceilingLinear/reentryPeak;for(auto& x:out)x*=transition;}
   lastReduction=AppliedAttenuation::reductionDb(control,1,bypass,transition);
   meter.maxGainReductionDb=std::max(meter.maxGainReductionDb,lastReduction);
   for(unsigned ch=0;ch<2;++ch){double y=std::isfinite(out[ch])?out[ch]:0;finalHistory[finalAt][ch]=double(Sample(y));if(ch<b.outputChannels&&b.outputs[ch]){b.outputs[ch][i]=Sample(y);meter.outputPeak=std::max(meter.outputPeak,std::abs(double(b.outputs[ch][i])));}}
   // Independent final-output observation: includes Output, bypass and cast.
   // It is evidence, not a claim that a finite-rate detector proves all analog peaks.
   const double actualPeak=reconstructed(64,reconstruction,[&](std::int64_t n,unsigned ch){return finalHistory[(finalAt+1+std::size_t(n))%129][ch];});
   outputReconstructionPeak=std::max(outputReconstructionPeak,actualPeak);finalAt=(finalAt+1)%129;
   if(context.analysis){EffectAnalysisSample measured;measured.validFields=analysisReduction;measured.reductionDb=lastReduction;context.analysis->pushSample(context.blockSampleOffset+i,measured);}
  }
 }
public:
 bool prepare(const PrepareSpec& spec)override{if(!spec.valid()||spec.inputChannels!=spec.outputChannels||spec.sidechainChannels||spec.sampleRate<8000||spec.sampleRate>384000)return false;rate=spec.sampleRate;maximumLookahead=std::uint32_t(std::ceil(rate*.005));latency=fixedLatency(rate);std::size_t size=1;while(size<=latency+128)size*=2;mask=size-1;frames.assign(size,{});samplePeaks.prepare(size);truePeaks.prepare(size);candidatePeaks.prepare(size);radius=reconstructionRadius(rate);makeKernel();prepared=true;reset(ResetReason::firstActivation);return true;}
 void reset(ResetReason)noexcept override{for(auto& f:frames)f={};samplePeaks.clear();truePeaks.clear();candidatePeaks.clear();time=0;primaryGain=liveGain=safetyGain=1;finalHistory={};finalAt=0;lastReduction=outputReconstructionPeak=0;meter={};meter.latency=latency;inputDb.reset(targetInput);trimDb.reset(targetTrim);outputDb.reset(targetOutput);releaseMs.reset(targetRelease);transientValue.reset(targetTransient);lookaheadValue.reset(targetLookahead);modeValue.reset(targetMode);bypassValue.reset(targetBypass);initialized=false;}
 void applyTargets(const SoundState& state,std::int32_t)noexcept override{
  const double a=physical(state,input),b=physical(state,trim),c=physical(state,release),d=physical(state,transient),e=physical(state,lookahead),f=physical(state,mode),g=physical(state,bypass),h=physical(state,output);ceilingLinear=gain(physical(state,ceiling));
  if(!initialized){targetInput=a;targetTrim=b;targetRelease=c;targetTransient=d;targetLookahead=e;targetMode=f;targetBypass=g;targetOutput=h;inputDb.reset(a);trimDb.reset(b);releaseMs.reset(c);transientValue.reset(d);lookaheadValue.reset(e);modeValue.reset(f);bypassValue.reset(g);outputDb.reset(h);initialized=true;}
  else{update(inputDb,targetInput,a);update(trimDb,targetTrim,b);update(releaseMs,targetRelease,c);update(transientValue,targetTransient,d);update(lookaheadValue,targetLookahead,e);update(outputDb,targetOutput,h);if(f!=targetMode){targetMode=f;modeValue.setTarget(f,std::uint32_t(std::ceil(rate*.01)));}if(g!=targetBypass){targetBypass=g;bypassValue.setTarget(g,std::uint32_t(std::ceil(rate*.005)));}}
 }
 void process(AudioBlock<float> b,const ProcessContext& c)noexcept override{render(b,c);}
 void process(AudioBlock<double> b,const ProcessContext& c)noexcept override{render(b,c);}
 void endBlock()noexcept override{publication.publish(meter);telemetry.push({meter.inputPeak,meter.outputPeak,latency,meter.invalidInput});meter={};meter.latency=latency;}
 std::uint32_t latencySamples()const noexcept override{return latency;}
 Tail tailSamples()const noexcept override{return {TailKind::finite,latency+32};}
 bool readTelemetry(Telemetry& t)noexcept override{return telemetry.pop(t);}
 bool readMeter(MeterSnapshot& s)const noexcept{return publication.read(s);}
 double appliedReductionDb()const noexcept{return lastReduction;}
 double measuredReconstructionPeak()const noexcept{return outputReconstructionPeak;}
};
}
