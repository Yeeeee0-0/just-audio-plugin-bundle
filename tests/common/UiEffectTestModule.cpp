// Test-only causal effect. Never built as a product or installed in a host path.
#include "common/vst3/Module.hpp"
#include "common/ui/Controls.hpp"
#include "common/ui/AnalysisView.hpp"
namespace just {
namespace {
constexpr ParameterSpec specs[]={
    {"fixture.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0},
    {"fixture.gain",1,"Gain","",0,1,0.6,Mapping::linear,0,true,"all",Transition::continuous,0}
};
class UiEffect final:public Engine {
    double history[2]{},gain=.6;bool bypass=false,audition=false;
    template<class S> void run(AudioBlock<S> b,const ProcessContext& context) noexcept {
        for(unsigned i=0;i<b.samples;++i){EffectAnalysisSample meter;meter.validFields=analysisWet|analysisReduction|analysisModulation;
        for(unsigned ch=0;ch<b.outputChannels;++ch){
            auto x=b.inputs[ch] && !(b.inputSilenceFlags&(1ull<<ch))?double(b.inputs[ch][i]):0;
            history[ch]=.94*history[ch]+.06*x;
            if(b.outputs[ch])b.outputs[ch][i]=S(bypass?x:(audition?x:gain*history[ch]));
            meter.wet[ch]=bypass?0:gain*history[ch];meter.modulation[ch]=bypass?1:gain;
        }
        meter.reductionDb=bypass?0:-20*std::log10(std::max(gain,1e-12));
        if(context.analysis)context.analysis->pushSample(context.blockSampleOffset+i,meter);}
    }
public:
    bool prepare(const PrepareSpec& s) override{return s.valid();}
    void reset(ResetReason) noexcept override{history[0]=history[1]=0;}
    void applyTargets(const SoundState& s,std::int32_t) noexcept override{bypass=s.targets[0]>=.5;gain=s.targets[1];}
    void process(AudioBlock<float> b,const ProcessContext& c) noexcept override{run(b,c);}
    void process(AudioBlock<double> b,const ProcessContext& c) noexcept override{run(b,c);}
    std::uint32_t latencySamples() const noexcept override{return 0;}
    Tail tailSamples() const noexcept override{return {TailKind::finite,2048};}
    bool readTelemetry(Telemetry&) noexcept override{return false;}
    bool setAudition(ParamID target,bool enabled) noexcept override{if(target!=1)return false;audition=enabled;return true;}
};
Engine* create(){return new UiEffect;}
class FixtureEditor final:public EditorContent {
    std::unique_ptr<RotaryControl> gain;std::unique_ptr<AnalysisView> plot;
public:
    bool attach(void* parent,const EditorServices& s) override{gain=RotaryControl::create(parent,s,specs[1]);plot=AnalysisView::create(parent,s,AnalysisViewMode::envelope);return gain && plot;}
    void resize(int width,int height) override{
        const int plotHeight=std::max(60,height-ControlGeometry::cellHeight);
        plot->resize(0,0,width,plotHeight);
        gain->resize((width-ControlGeometry::cellWidth)/2,plotHeight);
    }
    void refresh(const EditorViewState&,const StatusSnapshot&) override{gain->refresh();plot->refresh();}
};
EditorContent* createEditor(){return new FixtureEditor;}
bool presetA(Uid uid,SoundState& s){s=initialState(uid,{specs,std::size(specs)});s.targets[1]=.25;s.seed=123456;s.configurationCount=1;s.configurations[0]={1,.75};return true;}
bool presetB(Uid uid,SoundState& s){s=initialState(uid,{specs,std::size(specs)});s.targets[1]=.85;s.seed=987654;return true;}
const FactoryPreset presets[]={{"quiet","Quiet","Fixture",presetA},{"loud","Loud","Fixture",presetB}};
const ModuleDefinition def=[](){ModuleDefinition d{{specs,std::size(specs)},create,nullptr,0,nullptr,0,createEditor,nullptr,{},
    "Test fixture bypass retains filter history; zero reported latency"};d.factoryPresets=presets;d.factoryPresetCount=2;d.validatePresetState=[](const SoundState& s){return s.configurationCount<=1;};d.validateAuditionTarget=[](ParamID id){return id==1;};return d;}();
}
const ModuleDefinition& moduleDefinition(){return def;}
}
