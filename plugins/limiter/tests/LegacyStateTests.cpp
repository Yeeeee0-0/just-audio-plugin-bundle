#include "plugins/limiter/Dsp.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
using namespace just;
using namespace just::limiter;
static void check(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}
}
int main() {
    for(int legacyMode=0;legacyMode<2;++legacyMode) {
        const auto stem=std::filesystem::path(__FILE__).parent_path()/"fixtures"/("legacy-"+std::to_string(legacyMode));
        std::ifstream stream(stem.string()+".state",std::ios::binary);
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
        check(bytes.size()==152,"original nine-parameter state remains intact");
        SoundState restored;
        check(decodeState(bytes.data(),bytes.size(),{},registry,restored)==StateResult::ok,"old state decodes");
        check(restored.targets[algorithmVersion]==0,"missing Algorithm Version ID10 restores legacy path");
        check(restored.targets[mode]==legacyMode,"old mode restored, never replaced by new default");
        check(restored.targets[ceiling]==parameters[ceiling].toNormalized(-2.7),"old Ceiling automation mapping retained");
        check(restored.targets[output]==1,"missing Output ID9 restores neutral 0 dB");
        LimiterEngine engine;check(engine.prepare({48000,1,2,2}),"prepare legacy fixture");
        engine.applyTargets(restored,0);
        std::ifstream reference(stem.string()+".f64",std::ios::binary);
        double largestError=0;
        for(int i=0;i<2048;++i) {
            double l=i<1400?(i%29==0?3.7:.67*std::sin(i*.13)):0,r=-.43*l,out[2],expected[2];
            AudioBlock<double> block{{&l,&r},{out,out+1},2,2,1};engine.process(block,{});
            reference.read(reinterpret_cast<char*>(expected),sizeof(expected));check(bool(reference),"read baseline samples");
            for(int ch=0;ch<2;++ch)largestError=std::max(largestError,std::abs(expected[ch]-out[ch]));
        }
        std::cout<<"legacy mode="<<legacyMode<<" max_error="<<largestError<<std::endl;
        check(largestError==0,"old complete sound state renders bit exact to 0be3 production");
        std::cout<<"PASS legacy mode "<<legacyMode<<": 2048 stereo frames, max error "<<largestError<<", PDC "<<engine.latencySamples()<<'\n';
    }
}
