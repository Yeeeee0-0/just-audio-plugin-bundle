// Cross-runtime comparison against the unchanged accepted Mac fixtures.
// This test does not claim bit equality between macOS libm and Windows UCRT.
#include "plugins/limiter/Dsp.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <string>
using namespace just;
using namespace just::limiter;
static void check(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}
}
int main() {
    constexpr double tolerance=8*std::numeric_limits<double>::epsilon();
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
        double largestError=0,largestScaledError=0,sumSquares=0;
        unsigned unequalSamples=0;
        for(int i=0;i<2048;++i) {
            double l=i<1400?(i%29==0?3.7:.67*std::sin(i*.13)):0,r=-.43*l,out[2],expected[2];
            AudioBlock<double> block{{&l,&r},{out,out+1},2,2,1};engine.process(block,{});
            reference.read(reinterpret_cast<char*>(expected),sizeof(expected));check(bool(reference),"read baseline samples");
            for(int ch=0;ch<2;++ch) {
                check(std::isfinite(out[ch]) && std::isfinite(expected[ch]),"finite fixture and rendered audio");
                const double error=std::abs(expected[ch]-out[ch]);
                largestError=std::max(largestError,error);
                largestScaledError=std::max(largestScaledError,error/std::max(1.,std::abs(expected[ch])));
                sumSquares+=error*error;
                unequalSamples+=out[ch]!=expected[ch];
            }
        }
        check(reference.peek()==std::char_traits<char>::eof(),"complete fixture consumed without trailing samples");
        std::cout<<std::setprecision(17)<<"legacy mode="<<legacyMode<<" max_error="<<largestError
                 <<" max_scaled_error="<<largestScaledError<<" tolerance="<<tolerance
                 <<" rms_error="<<std::sqrt(sumSquares/4096)<<" unequal_samples="<<unequalSamples<<std::endl;
        check(largestScaledError<=tolerance,"old complete sound state matches Mac golden within eight double-precision epsilons");
        std::cout<<"PASS Windows numerical legacy mode "<<legacyMode<<": 2048 stereo frames; Mac bit-exact claim NOT MADE; PDC "<<engine.latencySamples()<<'\n';
    }
}
