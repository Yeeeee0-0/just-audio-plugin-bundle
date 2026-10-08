#include "ReverbEngine.hpp"
#include "Presets.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <cstring>
using namespace just;using namespace just::reverb;
static void writeWav(const std::filesystem::path& name,const std::vector<float>& data) {
    std::ofstream file(name,std::ios::binary);auto integer=[&](std::uint32_t x,int n){for(int i=0;i<n;++i)file.put(char(x>>(i*8)));};
    std::uint32_t bytes=std::uint32_t(data.size()*sizeof(float));file.write("RIFF",4);integer(36+bytes,4);file.write("WAVEfmt ",8);integer(16,4);integer(3,2);integer(2,2);integer(48000,4);integer(48000*8,4);integer(8,2);integer(32,2);file.write("data",4);integer(bytes,4);file.write(reinterpret_cast<const char*>(data.data()),bytes);
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    constexpr double fs=48000,pi=3.14159265358979323846;
    for(std::size_t preset=0;preset<std::size(presets);++preset) {
        auto state=presetState(preset);set(state,Mix,100);set(state,ModDepth,0);
        ReverbEngine e;e.prepare({fs,256,2,2,0,SampleFormat::float64,true});e.applyTargets(state,0);
        const std::size_t n=std::size_t(fs*(presets[preset].decay*2+2));
        std::vector<double> wave(n),l(256),r(256),a(256),b(256);
        for(std::size_t at=0;at<n;at+=256) {
            l.assign(256,0);r.assign(256,0);if(!at)l[0]=r[0]=1;
            e.process(AudioBlock<double>{{l.data(),r.data()},{a.data(),b.data()},2,2,256,at?3ull:0ull},{});
            for(std::size_t i=0;i<std::min<std::size_t>(256,n-at);++i)wave[at+i]=a[i];
        }
        std::ofstream file(out/(std::to_string(preset)+"-impulse.f64"),std::ios::binary);file.write(reinterpret_cast<const char*>(wave.data()),wave.size()*8);
    }
    std::vector<float> comparison;
    for(int part=0;part<4;++part) {
        std::size_t choice=part==1?1:part==2?5:8;
        auto state=presetState(choice);if(!part)set(state,Mix,0);else set(state,Mix,35);
        ReverbEngine e;e.prepare({fs,256,2,2,0,SampleFormat::float64,true});e.applyTargets(state,0);
        std::mt19937 random(43);std::uniform_real_distribution<double> noise(-1,1);
        std::vector<float> wave;std::array<double,256> l{},r{},a{},b{};
        const int frames=int(fs*(part==0?3:8));
        for(int at=0;at<frames;at+=256) {
            for(int i=0;i<256;++i) {
                double t=(at+i)/fs,x=0;
                for(double onset:{.3,.85,1.1})if(t>=onset && t<onset+.3) {
                    const double dt=t-onset;x+=.16*noise(random)*std::exp(-dt*95)+.18*std::sin(2*pi*(140*dt-32*dt*dt))*std::exp(-dt*24);
                }
                l[i]=r[i]=x;
            }
            e.process(AudioBlock<double>{{l.data(),r.data()},{a.data(),b.data()},2,2,256},{});
            for(int i=0;i<256 && at+i<frames;++i){wave.push_back(float(a[i]));wave.push_back(float(b[i]));}
        }
        writeWav(out/(part==0?"Dry.wav":part==1?"Room.wav":part==2?"Hall.wav":"Plate.wav"),wave);
        comparison.insert(comparison.end(),wave.begin(),wave.end());
    }
    writeWav(out/"Dry-Room-Hall-Plate.wav",comparison);
    std::cout<<"PASS rendered deterministic test transients: Dry (3s), Room (8s), Hall (8s), Plate (8s)\n";
}
