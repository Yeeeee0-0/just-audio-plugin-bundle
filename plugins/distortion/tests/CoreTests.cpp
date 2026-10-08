#include "plugins/distortion/Engine.hpp"
#include "LegacyCleanState.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "common/parameters/Automation.hpp"
#include "common/ui/Model.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <new>
#include <cstdlib>
using namespace just;using namespace just::distortion;
static bool inAudio=false;static unsigned allocations=0;
void* operator new(std::size_t n){if(inAudio)++allocations;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(inAudio)++allocations;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
static void check(bool x,const char* message){if(!x){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static SoundState init(){return initialState(pluginIdentities[9].processor,registry);}
static void set(SoundState& s,ID id,double v){auto i=registry.index(id);s.targets[i]=parameters[i].toNormalized(v);}
template<class T> static void process(DistortionEngine& e,const std::vector<T>& in,std::vector<T>& out,unsigned block=128,bool inPlace=false){
    out.resize(in.size());if(inPlace)out=in;
    for(unsigned at=0;at<in.size();){unsigned n=std::min<unsigned>(block,in.size()-at);
        AudioBlock<T> b;b.inputChannels=b.outputChannels=1;b.samples=n;b.inputs[0]=(inPlace?out.data():in.data())+at;b.outputs[0]=out.data()+at;
        inAudio=true;e.process(b,{});e.endBlock();inAudio=false;at+=n;
    }
}
// Engine has lock-free queues, so helpers below construct locally instead of copy.
static std::vector<double> render(const SoundState& s,const std::vector<double>& in,double fs=48000,unsigned factor=4,unsigned block=128){
    DistortionEngine e(factor);check(e.prepare({fs,2048,1,1}),"prepare");e.applyTargets(s,0);std::vector<double> out;process(e,in,out,block);return out;
}
int main(int argc,char** argv){
    check(registry.valid(),"valid explicit registry");
    check(physical(init(),model)==1,"new instances default to Soft without enum reordering");
    auto legacyBytes=std::vector<std::uint8_t>(legacyCleanState.begin(),legacyCleanState.end());SoundState legacyRestored;
    check(decodeState(legacyBytes.data(),legacyBytes.size(),pluginIdentities[9].processor,registry,legacyRestored)==StateResult::ok
          && physical(legacyRestored,model)==0 && encodeState(legacyRestored,registry)==legacyBytes,"immutable 0be3 complete Clean state restores byte-for-byte");
    auto legacyClean=legacyRestored;
    for(const auto& p:parameters)for(double f:{0.,.1,.5,.9,1.}){double x=p.minimum+(p.maximum-p.minimum)*f;double r=p.toPhysical(p.toNormalized(x));if(!p.stepCount)check(std::abs(r-x)<1e-7*std::max(1.,std::abs(x)),"mapping round trip");}
    auto state=init();set(state,soft_shape,91);set(state,crush_hold_hz,12345);set(state,quality,3);state.seed=20261002;
    auto encoded=encodeState(state,registry);SoundState decoded;
    check(decodeState(encoded.data(),encoded.size(),state.plugin,registry,decoded)==StateResult::ok,"full state round trip");
    check(decoded.targets==state.targets && decoded.seed==state.seed,"all hidden targets and seed retained");
    auto before=decoded;encoded.pop_back();check(decodeState(encoded.data(),encoded.size(),state.plugin,registry,decoded)==StateResult::malformed && decoded.targets==before.targets,"malformed restore transactional");
    check(physical(state,crush_hold_hz)==physical(decoded,crush_hold_hz),"absolute Hz held independent of sample rate");
    for(double rate:{44100.,48000.,96000.,192000.})for(unsigned block:{16u,64u,128u,731u,1024u}){
        std::vector<double> input(4096),output;for(unsigned i=0;i<input.size();++i)input[i]=std::sin(.02*i)*2;
        DistortionEngine e;check(e.prepare({rate,2048,1,1}),"rate matrix");e.applyTargets(legacyClean,0);process(e,input,output,block,true);
        for(unsigned i=0;i<input.size();++i)if(output[i]!=(i<32?0:input[i-32])){std::cerr<<"Clean mismatch rate="<<rate<<" block="<<block<<" at="<<i<<" output="<<output[i]<<" expected="<<(i<32?0:input[i-32])<<'\n';check(false,"Clean exact delayed null incl headroom");}
        auto dry=init();set(dry,model,1);set(dry,distortion::mix,0);set(dry,input_db,6);set(dry,output_db,-3);
        e.prepare({rate,2048,1,1});e.applyTargets(dry,0);process(e,input,output,block);
        for(unsigned i=32;i<input.size();++i)check(std::abs(output[i]-input[i-32]*dbGain(3))<1e-14,"Mix0 retains trims and delay");
    }
    for(int m=0;m<=5;++m){
        auto s=init();set(s,model,m);set(s,drive_db,36);set(s,boost,1);set(s,bias,.5);set(s,crush_dither,1);
        std::vector<double> zeros(48000);auto out=render(s,zeros);for(auto x:out)check(x==0,"zero-input exact silence all models/dither gating");
        std::vector<double> input(50000,0);input[0]=2;out=render(s,input);for(auto x:out)check(std::isfinite(x),"impulse output finite");
        for(unsigned i=40000;i<out.size();++i)check(out[i]==0,"declared finite tail clears");
    }
    for(Model m:{Model::Soft,Model::Hard,Model::Asym,Model::Fold})check(transfer(m,0,.5,.5)==0,"bias subtracts f(bias)");
    check(transfer(Model::Hard,2,0,0)==1 && transfer(Model::Hard,-2,0,0)==-1,"hard clipping thresholds");
    {Oversampler os;os.prepare(4);std::vector<double> impulse(256);impulse[0]=1;double peak=0;unsigned where=0;
        for(unsigned i=0;i<impulse.size();++i){double x=os.tick(impulse[i],[](double y){return y;});if(std::abs(x)>peak){peak=std::abs(x);where=i;}}
        check(where==32,"measured FIR impulse delay matches PDC32");}
    {auto s=init();set(s,model,5);set(s,crush_bits,4);set(s,crush_hold_hz,12000);
        std::vector<double> x(2048);for(unsigned i=0;i<x.size();++i)x[i]=double(i%8)/32;
        auto a=render(s,x,48000,4,16),b=render(s,x,48000,4,731);check(a==b,"Crush phase and quantizer independent of block segmentation");
        set(s,crush_dither,1);s.seed=123;check(render(s,x)==render(s,x),"seeded dither reproducible");}
    {auto s=init();set(s,model,0);DistortionEngine e;e.prepare({48000,128,2,2});e.applyTargets(s,0);
        double left[128]={},right[128]={},ol[128],orr[128];left[0]=1;right[0]=-.75;
        AudioBlock<double> b;b.inputs={left,right};b.outputs={ol,orr};b.inputChannels=b.outputChannels=2;b.samples=128;
        e.process(b,{});check(ol[32]==1 && orr[32]==-.75,"independent stereo");
        e.reset(ResetReason::explicitReset);e.applyTargets(s,0);b.inputSilenceFlags=3;e.process(b,{});for(unsigned i=0;i<128;++i)check(ol[i]==0 && orr[i]==0,"silence flags respected");}
    {auto s=init();set(s,model,0);DistortionEngine e;e.prepare({48000,128,1,2});e.applyTargets(s,0);double x[128]={},r[128];x[0]=1;
        AudioBlock<double> b;b.inputs={x,nullptr};b.outputs={x,r};b.inputChannels=1;b.outputChannels=2;b.samples=128;e.process(b,{});check(x[32]==1 && r[32]==1,"in-place mono to stereo reads before writes");}
    {auto s=init();set(s,model,2);DistortionEngine e;e.prepare({48000,128,1,1});e.applyTargets(s,0);
        double x[128]={},y[128];x[0]=std::numeric_limits<double>::infinity();x[1]=std::nan("");
        AudioBlock<double> b;b.inputs[0]=x;b.outputs[0]=y;b.inputChannels=b.outputChannels=1;b.samples=128;e.process(b,{});for(auto v:y)check(std::isfinite(v),"nonfinite isolation");}
    {auto s=init();set(s,model,1);set(s,drive_db,12);std::vector<float>x(2048,.1f),y;DistortionEngine e;e.prepare({48000,128,1,1});e.applyTargets(s,0);process(e,x,y);for(float v:y)check(std::isfinite(v),"float32 path");}
    // Compare two engines with UI-only mutations; DSP never reads a view object.
    {auto s=init();set(s,model,1);set(s,bias,.3);set(s,fold_shape,77);DistortionEngine a,b;a.prepare({48000,128,1,1});b.prepare({48000,128,1,1});a.applyTargets(s,0);b.applyTargets(s,0);
        std::vector<double>x(8192);for(unsigned i=0;i<x.size();++i)x[i]=.2*std::sin(.13*i);std::vector<double>ya,yb;
        EditorViewState view;for(int i=0;i<13;++i)view.advanced=!view.advanced;process(a,x,ya);process(b,x,yb);check(ya==yb,"Simple Advanced changes no DSP state");}
    for(double fs:{44100.,48000.,96000.,192000.}){
        {Oversampler os;os.prepare(4);double peak=0;unsigned where=0;for(unsigned n=0;n<256;++n){double y=os.hard(n==0?.05:0,.5);if(std::abs(y)>peak){peak=std::abs(y);where=n;}}check(where==32,"Hard ADAA+correction actual delay matches32 at everyFs");}
        for(int m=0;m<=5;++m){auto s=init();set(s,model,m);set(s,drive_db,36);set(s,boost,1);set(s,bias,.5);set(s,hard_softness,0);set(s,quality,3);
            std::vector<double>x(2048);for(unsigned n=0;n<x.size();++n)x[n]=2*std::sin(.117*n);
            auto a=render(s,x,fs,4,16),b=render(s,x,fs,4,731);check(a==b,"all-model extreme block determinism acrossFs");for(auto y:a)check(std::isfinite(y),"strong/extreme finite acrossFs");
            for(double softness:{0.,100.}){set(s,hard_softness,softness);std::vector<float>xf(x.begin(),x.end()),yf;DistortionEngine f;f.prepare({fs,2048,1,1,0,SampleFormat::float32});f.applyTargets(s,0);process(f,xf,yf,64,true);for(auto y:yf)check(std::isfinite(y),"float32 extreme all models/Fs/in-place");}
            DistortionEngine e;check(e.prepare({fs,128,1,1}),"prepared fixed quality");e.applyTargets(s,0);AudioBlock<double> empty;empty.inputChannels=empty.outputChannels=1;empty.samples=0;e.process(empty,{});check(e.latencySamples()==32,"pending quality/zero block do not change PDC");
            set(s,bypass,1);auto out=render(s,x,fs,4,64);for(unsigned n=32;n<x.size();++n)check(out[n]==x[n-32],"bypass exact aligned raw input every model/Fs");
        }
        {auto s=init();set(s,model,3);set(s,bias,.5);std::vector<double>x(unsigned(fs*.6),0);x[0]=1;auto y=render(s,x,fs);double residual=0;for(unsigned n=unsigned(fs*.5);n<y.size();++n)residual=std::max(residual,std::abs(y[n]));check(residual<1e-4,"Asym impulse DC below-80dBFS after500ms");}
        {auto s=init();set(s,model,5);set(s,crush_bits,24);set(s,crush_hold_hz,12345);unsigned size=unsigned(fs)+64;std::vector<double>x(size);for(unsigned n=0;n<size;++n)x[n]=.01+n*1e-6;auto y=render(s,x,fs);
            double previousY=0,recovered=0,previousHeld=0,r=std::exp(-2*pi*10/fs);unsigned captures=0;
            for(unsigned n=32;n<unsigned(fs)+32;++n){recovered+=y[n]-r*previousY;previousY=y[n];if(std::abs(recovered-previousHeld)>1e-7)++captures;previousHeld=recovered;}
            check(std::abs(double(captures)-12345)<=2,"Crush absolute hold capture count within2samples/second");}
        // Dense targets exercise coalescing two model paths and soft bypass; no UI access.
        {DistortionEngine e;e.prepare({fs,128,1,1});auto s=init();e.applyTargets(s,0);double input=.7,output;AudioBlock<double> b;b.inputChannels=b.outputChannels=1;b.samples=1;b.inputs[0]=&input;b.outputs[0]=&output;
            for(unsigned n=0;n<4096;++n){set(s,model,(n/23)%6);set(s,drive_db,(n%37));set(s,hard_softness,(n%101));set(s,bias,(n%2)?.5:-.5);set(s,bypass,(n/71)%2);
                inAudio=true;e.applyTargets(s,n);e.process(b,{});e.endBlock();inAudio=false;check(std::isfinite(output),"dense model/shape/drive/bypass targets finite and RT safe");}
        }
    }
    check(allocations==0,"zero C++ new/delete in audio render/endBlock");
    std::cout<<"PASS mappings/state, Clean null/trims, Hard4xADAA PDC32 fourFs, six models silence/tails/extremes, Crush rate, stereo/in-place, NaNInf, float32/64, bypass exact delay, dense automation, view independence, RT new/delete guard\n";
    if(argc>1){
        std::ofstream file(argv[1]);file.precision(17);file<<"model,frequency,input,oneX,fourX\n";
        constexpr unsigned count=4096;std::vector<double> x(count*8);
        for(int m:{1,2,4})for(unsigned bin:{85u,853u,1450u}){
            double frequency=48000.0*bin/count;
            for(unsigned i=0;i<x.size();++i)x[i]=.2*std::sin(2*pi*bin*i/count);
            auto s=init();set(s,model,m);set(s,drive_db,12);auto a=render(s,x,48000,1),b=render(s,x,48000,4);
            for(unsigned i=count*6;i<count*7;++i)file<<m<<','<<frequency<<','<<x[i]<<','<<a[i]<<','<<b[i+32]<<'\n';
        }
    }
}
