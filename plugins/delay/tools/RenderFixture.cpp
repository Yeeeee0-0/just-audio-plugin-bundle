#include "../DelayEngine.hpp"
#include <fstream>
#include <random>
#include <iostream>
using namespace just;using namespace just::delay;
int main(int argc,char** argv){
    if(argc!=2)return 1;
    constexpr unsigned rate=48000,length=rate*8;
    DelayEngine e;auto s=initialState({},registry);set(s,route,2);set(s,timeL,250);set(s,timeR,250);set(s,feedback,45);set(s,mix,100);
    if(!e.prepare({rate,256,1,2,0,SampleFormat::float64,false}))return 2;e.applyTargets(s,0);
    std::vector<double> mono(length),left(length),right(length);
    std::mt19937 random(20261002);
    for(unsigned event:{rate,rate*4})for(unsigned i=0;i<1200;++i){
        double t=double(i)/rate,envelope=std::exp(-t*150);
        double noise=(double(random()%100000)/50000-1)*0.16*std::exp(-t*450);
        mono[event+i]=0.4*std::sin(2*pi*(event==rate?600:900)*t)*envelope+noise;
    }
    for(unsigned i=0;i<length;i+=256){unsigned n=std::min(256u,length-i);e.process(AudioBlock<double>{{mono.data()+i,nullptr},{left.data()+i,right.data()+i},1,2,n,0},{});e.endBlock();}
    double peak=0;for(unsigned i=0;i<length;++i)peak=std::max({peak,std::abs(left[i]),std::abs(right[i])});
    if(!std::isfinite(peak) || peak>1)return 3;
    std::ofstream file(argv[1],std::ios::binary);auto integer=[&](std::uint32_t value,int bytes){for(int i=0;i<bytes;++i)file.put(char(value>>(8*i)));};
    file.write("RIFF",4);integer(36+length*6,4);file.write("WAVEfmt ",8);integer(16,4);integer(1,2);integer(2,2);integer(rate,4);integer(rate*6,4);integer(6,2);integer(24,2);file.write("data",4);integer(length*6,4);
    for(unsigned i=0;i<length;++i)for(double x:{left[i],right[i]})integer(std::uint32_t(std::int32_t(std::llround(x*8388607))),3);
    std::cout<<"Fixture: stereo 48kHz/24bit 8s, wet Ping-Pong L/R alternating, peak="<<peak<<"\n";return file?0:4;
}
