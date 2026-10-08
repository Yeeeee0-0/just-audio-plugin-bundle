#pragma once
#include "Dsp.hpp"
#include "common/dsp/Engine.hpp"
namespace just::distortion {
class DistortionEngine final:public Engine {
    struct Path {
        Model model=Model::Clean;Oversampler os;Delay<129> pad;
        Biquad hp,tiltLow,tiltHigh,bass,treble,lp,crushAA;
        double dcX=0,dcY=0,hold=0,phase=1;std::uint64_t random=1;
        void reset() noexcept {os.reset();pad.reset();hp.reset();tiltLow.reset();tiltHigh.reset();bass.reset();treble.reset();lp.reset();crushAA.reset();dcX=dcY=hold=0;phase=1;}
        double uniform() noexcept {random^=random<<13;random^=random>>7;random^=random<<17;return double(random>>11)*(1.0/9007199254740992.0);}
    };
    std::array<std::array<Path,2>,2> paths{}; // exactly two prepared model paths
    std::array<Delay<129>,2> dryDelay{},rawDelay{};
    std::array<LinearSmoother,std::size(parameters)> smooth{};
    std::array<double,std::size(parameters)> last{},value{};
    SpscQueue<Telemetry,8> telemetry;Telemetry meter{};
    double fs=48000,dcR=0;unsigned factor=4,latency=32,active=0,fadeAt=0,fadeLength=1;
    Model requested=Model::Clean;bool fading=false,initialized=false;std::uint64_t seed=0;
    bool hardAntialias=true;
    std::uint32_t silentSamples=0,tail=0;
    double get(ID id) const noexcept {return value[registry.index(id)];}
    void start(Model model) noexcept {
        unsigned next=1-active;
        for(unsigned ch=0;ch<2;++ch){auto& p=paths[next][ch];p.reset();p.model=model;p.random=seed^(0x9e3779b97f4a7c15ull*(ch+1));if(!p.random)p.random=1;}
        fading=true;fadeAt=0;
    }
    void clearSignal() noexcept {
        for(auto& pair:paths)for(auto& p:pair)p.reset();
        for(auto& p:dryDelay)p.reset();for(auto& p:rawDelay)p.reset();
    }
    double wet(Path& p,double input) noexcept {
        if(p.model==Model::Clean)return p.pad.tick(input,latency);
        // Coefficient calculation uses smoothed physical values; no allocation.
        p.hp.pass(get(pre_hp_hz),fs,true);
        double hp=p.hp.tick(input);input+=get(pre_hp_enabled)*(hp-input);
        p.tiltLow.shelf(1000,fs,-.5*get(pre_tilt_db),false);
        p.tiltHigh.shelf(1000,fs,.5*get(pre_tilt_db),true);
        input=p.tiltHigh.tick(p.tiltLow.tick(input));
        double y;
        if(p.model==Model::Crush){
            double rate=std::min(get(crush_hold_hz),fs);
            p.crushAA.pass(std::max(1.0,.45*rate),fs,false);
            double filtered=p.crushAA.tick(input);input+=get(crush_aa)*(filtered-input);
            if(p.phase>=1){
                p.phase-=std::floor(p.phase);
                double levels=std::ldexp(1.0,int(get(crush_bits))-1);
                double dither=(get(crush_dither)>=.5 && input!=0)?(p.uniform()-p.uniform())/levels:0;
                p.hold=std::round(std::clamp(input+dither,-1e6,1e6)*levels)/levels;
            }
            p.phase+=rate/fs;y=p.pad.tick(p.hold,latency);
        } else {
            double driven=input*dbGain(get(drive_db)+12*get(boost));
            double shape=p.model==Model::Soft?get(soft_shape):p.model==Model::Hard?get(hard_softness):p.model==Model::Asym?get(asym_shape):get(fold_shape);
            double biasValue=get(distortion::bias);
            y=p.model==Model::Hard?p.os.hard(driven,shape*.01,hardAntialias):p.os.tick(driven,[&](double x){return transfer(p.model,x,biasValue,shape*.01);});
        }
        double dc=y-p.dcX+dcR*p.dcY;p.dcX=y;p.dcY=cleanTiny(dc);y=p.dcY;
        p.bass.shelf(150,fs,get(post_bass_db),false);p.treble.shelf(6000,fs,get(post_treble_db),true);
        y=p.treble.tick(p.bass.tick(y));
        p.lp.pass(std::min(get(post_lp_hz),.45*fs),fs,false);double filtered=p.lp.tick(y);
        y+=get(post_lp_enabled)*(filtered-y);
        double compensation=(p.model==Model::Crush?0:-.5*(get(drive_db)+12*get(boost)))*get(drive_comp);
        return y*dbGain(get(makeup_db)+get(match_gain_db)+compensation);
    }
    template<class Sample> void render(AudioBlock<Sample> block,const ProcessContext& context) noexcept {
        if(context.seek)clearSignal();
        for(std::uint32_t i=0;i<block.samples;++i){
            for(std::size_t n=0;n<value.size();++n)value[n]=smooth[n].tick();
            if(!fading && paths[active][0].model!=requested)start(requested);
            double fade=fading?double(fadeAt)/fadeLength:0;
            bool inputSilent=true;
            // Read both channels before writes: supports in-place mono->stereo.
            double raw[2]={};
            for(unsigned ch=0;ch<block.outputChannels;++ch){unsigned src=ch<block.inputChannels?ch:0;
                if(block.inputChannels && block.inputs[src] && !(block.inputSilenceFlags&(1ull<<src)))raw[ch]=block.inputs[src][i];
                if(!std::isfinite(raw[ch]) || std::abs(raw[ch])>1e100){raw[ch]=0;meter.invalidInput=true;paths[active][ch].reset();paths[1-active][ch].reset();dryDelay[ch].reset();rawDelay[ch].reset();}
                inputSilent&=raw[ch]==0;meter.inputPeak=std::max(meter.inputPeak,std::abs(raw[ch]));
            }
            for(unsigned ch=0;ch<block.outputChannels;++ch){
                double input=raw[ch]*dbGain(get(input_db));
                double dry=dryDelay[ch].tick(input,latency),rawBypass=rawDelay[ch].tick(raw[ch],latency);
                double processed=wet(paths[active][ch],input);
                if(fading){double next=wet(paths[1-active][ch],input);processed+=(next-processed)*fade;}
                double mix=get(distortion::mix)*.01;
                double output=(dry+mix*(processed-dry))*dbGain(get(output_db));
                double bypassMix=get(bypass);
                if(bypassMix>=1)output=rawBypass;
                else if(bypassMix>0)output+=bypassMix*(rawBypass-output);
                if(!std::isfinite(output) || std::abs(output)>std::numeric_limits<Sample>::max()){output=0;meter.invalidInput=true;}
                if(block.outputs[ch])block.outputs[ch][i]=static_cast<Sample>(output);
                meter.outputPeak=std::max(meter.outputPeak,std::abs(output));
            }
            if(fading && ++fadeAt>=fadeLength){active=1-active;fading=false;}
            if(!inputSilent)silentSamples=0;
            else if(silentSamples<tail && ++silentSamples==tail)clearSignal();
        }
    }
public:
    // Test-only constructor selects a fixed prepared rate. Shipping factory is 4x.
    explicit DistortionEngine(unsigned fixedFactor=4,bool useHardAdaa=true):factor(fixedFactor),hardAntialias(useHardAdaa){}
    bool prepare(const PrepareSpec& s) override {
        if(!s.valid() || s.sampleRate<8000 || s.sampleRate>192000 || (factor!=1 && factor!=2 && factor!=4 && factor!=8))return false;
        fs=s.sampleRate;latency=factor==1?0:128/factor;fadeLength=std::max(1u,unsigned(.03*fs));dcR=std::exp(-2*pi*10/fs);
        tail=static_cast<std::uint32_t>(std::ceil(.75*fs))+latency;
        for(auto& pair:paths)for(auto& p:pair)p.os.prepare(factor);
        reset(ResetReason::explicitReset);return true;
    }
    void reset(ResetReason) noexcept override {clearSignal();initialized=false;fading=false;active=0;silentSamples=0;meter={};}
    void applyTargets(const SoundState& state,std::int32_t) noexcept override {
        for(std::size_t n=0;n<value.size();++n){
            double next=physical(state,static_cast<ID>(parameters[n].id));
            if(!initialized){smooth[n].reset(next);value[n]=last[n]=next;}
            else if(next!=last[n]){last[n]=next;smooth[n].setTarget(next,unsigned((parameters[n].id==bypass?10:parameters[n].smoothingMs)*.001*fs));}
        }
        requested=static_cast<Model>(int(physical(state,model)));
        if(!initialized){for(auto& pair:paths)for(auto& p:pair)p.model=requested;}
        if(!initialized || seed!=state.seed){seed=state.seed;for(unsigned slot=0;slot<2;++slot)for(unsigned ch=0;ch<2;++ch){auto& p=paths[slot][ch];p.random=seed^(0x9e3779b97f4a7c15ull*(ch+1));if(!p.random)p.random=1;}}
        initialized=true;
    }
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override {render(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override {render(b,c);}
    void endBlock() noexcept override {meter.latencySamples=latency;telemetry.push(meter);meter={};}
    std::uint32_t latencySamples() const noexcept override {return latency;}
    Tail tailSamples() const noexcept override {return {TailKind::finite,tail};}
    bool readTelemetry(Telemetry& t) noexcept override {return telemetry.pop(t);}
};
}
