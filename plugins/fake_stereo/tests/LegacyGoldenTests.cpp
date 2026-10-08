#include "plugins/fake_stereo/Dsp.hpp"
#include "plugins/fake_stereo/Parameters.hpp"
#include "common/state/State.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
using namespace just;using namespace just::stereo;
static void check(bool ok,const char* m){if(!ok){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
template<class S> static void render(std::vector<std::uint8_t>& bytes){
    for(double fs:{44100.,48000.,96000.})for(auto bus:{std::pair<unsigned,unsigned>{1,1},{1,2},{2,2}}){
        Core core;check(core.prepare({fs,128,bus.first,bus.second}),"prepare legacy compatible bus");
        std::array<S,128> l{},r{},ol{},orr{};AudioBlock<S> b;b.samples=128;b.inputChannels=bus.first;b.outputChannels=bus.second;b.inputs={l.data(),r.data()};b.outputs={ol.data(),orr.data()};
        Targets t;std::uint32_t rng=0x125678ab;auto noise=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (double(rng)/4294967295.-.5)*.6;};
        for(unsigned block=0;block<100;++block){
            if(block==12){t.inputDb=-3.12;t.outputDb=-.1;t.width=178.13;t.existingSide=31.27;t.mix=67.8;t.lowHz=287.6;t.highHz=6432.1;t.highEnabled=true;}
            if(block==24)t.diffuse=true;if(block==37)t.swap=true;if(block==50)t.mono=true;if(block==63)t.bypass=true;if(block==76){t.bypass=false;t.mono=false;t.swap=false;}
            core.setTargets(t);for(unsigned i=0;i<128;++i){l[i]=S(noise());r[i]=S(noise());}ProcessContext c;c.seek=block==89;core.process(b,c);
            for(unsigned ch=0;ch<bus.second;++ch){const auto* begin=reinterpret_cast<const std::uint8_t*>(ch?orr.data():ol.data());bytes.insert(bytes.end(),begin,begin+sizeof(S)*128);}
        }
    }
}
int main(int argc,char** argv){check(argc==3,"usage --record|--verify golden-file");std::vector<std::uint8_t> actual;render<float>(actual);render<double>(actual);
    if(std::string(argv[1])=="--record"){std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(actual.data()),actual.size());check(bool(out),"write golden");std::cout<<"Recorded baseline audio "<<actual.size()<<" bytes\n";return 0;}
    std::ifstream in(argv[2],std::ios::binary);std::vector<std::uint8_t> expected((std::istreambuf_iterator<char>(in)),{});check(actual==expected,"new neutral transforms produce bit-identical legacy audio");
    ParameterRegistry old{parameters,12};SoundState source=initialState({},old);for(unsigned i=0;i<12;++i)source.targets[i]=double(i+1)/15.;source.seed=0x12345678;
    auto bytes=encodeState(source,old);SoundState decoded;check(decodeState(bytes.data(),bytes.size(),{},registry,decoded)==StateResult::ok,"old schema loads");
    for(unsigned i=0;i<12;++i)check(decoded.targets[i]==source.targets[i],"old normalized targets exact");
    for(unsigned i=12;i<registry.count;++i)check(decoded.targets[i]==parameters[i].toNormalized(parameters[i].initial),"new absent IDs use neutral init");
    auto newBytes=encodeState(decoded,registry);SoundState reverse;check(decodeState(newBytes.data(),newBytes.size(),{},old,reverse)==StateResult::ok && reverse.seed==source.seed,"old decoder accepts new state and seed");
    for(unsigned i=0;i<12;++i)check(reverse.targets[i]==source.targets[i],"reverse old IDs exact");
    std::cout<<"PASS legacy audio "<<actual.size()<<" bytes exact, 3 Fs, float32/64, 3 buses, automation/seek, state forward/reverse\n";
}
