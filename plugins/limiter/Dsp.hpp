#pragma once
#include "LegacyDsp.hpp"
#include "TransparentDsp.hpp"
namespace just::limiter {
// The appended version parameter is the sole routing decision. Legacy state
// remains byte-compatible and reaches the unmodified 7d arithmetic.
class LimiterEngine final:public Engine {
 LegacyLimiterEngine legacy;TransparentLimiterEngine transparent;
 std::array<bool,2> running{{false,false}};
 std::array<std::uint32_t,2> warming{};
 LinearSmoother blend;
 unsigned wanted=1,blendTarget=1,processed=0;
 double rate=48000;
 bool initialized=false,transitionBlock=false,lastTransition=false;
 Engine& engine(unsigned version)noexcept{return version?static_cast<Engine&>(transparent):static_cast<Engine&>(legacy);}
 template<class T>void render(AudioBlock<T> block,const ProcessContext& context)noexcept{
  if(running[wanted]&&!running[1-wanted]&&!warming[wanted]){engine(wanted).process(block,context);processed|=1u<<wanted;return;}
  transitionBlock=true;
  for(std::uint32_t i=0;i<block.samples;++i){
   if(!warming[wanted]&&blendTarget!=wanted){blend.setTarget(wanted,std::uint32_t(std::ceil(rate*.01)));blendTarget=wanted;}
   const double mix=blend.tick();std::array<std::array<T,2>,2> outputs{};
   for(unsigned v=0;v<2;++v)if(running[v]){
    AudioBlock<T> b=block;b.samples=1;for(unsigned ch=0;ch<2;++ch){b.inputs[ch]=block.inputs[ch]?block.inputs[ch]+i:nullptr;b.outputs[ch]=&outputs[v][ch];}
    auto c=context;c.blockSampleOffset+=i;c.seek=context.seek&&i==0;
    // During a crossfade there is no single limiter control gain. Audio I/O
    // remains captured by common, while this brief mixed GR is unavailable.
    if((mix!=0&&mix!=1)||(mix==0&&v!=0)||(mix==1&&v!=1))c.analysis=nullptr;
    engine(v).process(b,c);processed|=1u<<v;if(warming[v])--warming[v];
   }
   for(unsigned ch=0;ch<block.outputChannels&&ch<2;++ch)if(block.outputs[ch])block.outputs[ch][i]=T(outputs[0][ch]*(1-mix)+outputs[1][ch]*mix);
   if(!warming[wanted]&&mix==double(wanted))running[1-wanted]=false;
  }
 }
public:
 bool prepare(const PrepareSpec& s)override{rate=s.sampleRate;const bool ok=legacy.prepare(s)&&transparent.prepare(s);initialized=false;running={false,false};warming={};wanted=blendTarget=1;blend.reset(1);processed=0;transitionBlock=lastTransition=false;return ok;}
 void reset(ResetReason r)noexcept override{legacy.reset(r);transparent.reset(r);running={false,false};running[wanted]=true;warming={};blendTarget=wanted;blend.reset(wanted);processed=0;transitionBlock=lastTransition=false;}
 void applyTargets(const SoundState& state,std::int32_t offset)noexcept override{
  const unsigned next=physical(state,algorithmVersion)>=.5?1:0;
  if(!initialized){wanted=blendTarget=next;blend.reset(next);running[next]=true;initialized=true;}
  else if(next!=wanted){wanted=next;if(!running[next]){engine(next).reset(ResetReason::explicitReset);running[next]=true;warming[next]=latencySamples();}if(!warming[next]){blend.setTarget(next,std::uint32_t(std::ceil(rate*.01)));blendTarget=next;}}
  for(unsigned v=0;v<2;++v)if(running[v])engine(v).applyTargets(state,offset);
 }
 void process(AudioBlock<float> b,const ProcessContext& c)noexcept override{render(b,c);}
 void process(AudioBlock<double> b,const ProcessContext& c)noexcept override{render(b,c);}
 void endBlock()noexcept override{for(unsigned v=0;v<2;++v)if(processed&(1u<<v))engine(v).endBlock();lastTransition=transitionBlock;transitionBlock=false;processed=0;}
 std::uint32_t latencySamples()const noexcept override{return transparent.latencySamples();}
 Tail tailSamples()const noexcept override{return wanted?transparent.tailSamples():legacy.tailSamples();}
 bool readTelemetry(Telemetry& t)noexcept override{
  Telemetry discarded;for(unsigned v=0;v<2;++v)if(v!=wanted||lastTransition)while(engine(v).readTelemetry(discarded)){}
  return !lastTransition&&engine(wanted).readTelemetry(t);
 }
 bool readMeter(MeterSnapshot& s)const noexcept{return !lastTransition&&(wanted?transparent.readMeter(s):legacy.readMeter(s));}
 double appliedReductionDb()const noexcept{return wanted?transparent.appliedReductionDb():legacy.appliedReductionDb();}
 double measuredReconstructionPeak()const noexcept{return wanted?transparent.measuredReconstructionPeak():0;}
};
}
