#include "Filter.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;using namespace just::eq;
static int checks=0;
static void check(bool b,const char* message){++checks;if(!b){std::cerr<<"FAIL enum golden: "<<message<<"\n";std::exit(1);}}
// Independent literal expectations: neither generated from Parameters.hpp nor JSON.
static constexpr const char* boolGolden[]={"Off","On"};
static constexpr const char* shapeGolden[]={"Bell","Low Shelf","High Shelf","High Pass","Low Pass","Notch"};
static constexpr const char* targetGolden[]={"Stereo","Mid","Side"};
static constexpr const char* slopeGolden[]={"6","12","24","36","48"};
static constexpr const char* extensionGolden[]={"Legacy","18 dB/oct","72 dB/oct","96 dB/oct"};
static constexpr const char* detectorGolden[]={"Peak","RMS"};
static constexpr const char* sourceGolden[]={"Internal","External"};
static void verify(const ParameterSpec& p,const char* const* golden,std::size_t count){
    check(p.enumLabels && p.stepCount+1==count,"enum size/step count");
    for(std::size_t ordinal=0;ordinal<count;++ordinal){
        double normalized=double(ordinal)/(count-1);char text[64];p.format(normalized,text,sizeof(text));
        check(std::strcmp(p.enumLabels[ordinal],golden[ordinal])==0 && std::strcmp(text,golden[ordinal])==0,"ordinal label and host display");
        double parsed=-1;check(p.parse(golden[ordinal],parsed) && parsed==normalized,"host label parse returns exact ordinal");
        check(p.toPhysical(normalized)==double(ordinal),"host plain enum ordinal");
    }
}
int main(){
    check(int(Shape::bell)==0 && int(Shape::lowShelf)==1 && int(Shape::highShelf)==2 && int(Shape::highPass)==3 && int(Shape::lowPass)==4 && int(Shape::notch)==5,"DSP Shape ordinals");
    check(int(Target::stereo)==0 && int(Target::mid)==1 && int(Target::side)==2,"DSP Target ordinals");
    verify(parameters[0],boolGolden,2);
    for(std::size_t b=0;b<12;++b){
        verify(parameters[index(b,slopeExtension)],extensionGolden,4);
        check(index(b,slopeExtension)==183+b && id(b,slopeExtension)==115+32*b,"append-only extension index and reviewed ID");
        verify(parameters[index(b,enabled)],boolGolden,2);verify(parameters[index(b,type)],shapeGolden,6);
        verify(parameters[index(b,target)],targetGolden,3);verify(parameters[index(b,slope)],slopeGolden,5);
        verify(parameters[index(b,dynamicEnabled)],boolGolden,2);verify(parameters[index(b,detector)],detectorGolden,2);verify(parameters[index(b,source)],sourceGolden,2);
    }
    const double dbPerOctave[]={6,12,24,36,48};
    for(int ordinal=0;ordinal<5;++ordinal){FilterBank f;f.update(Shape::highPass,1000,0,0.7071067811865476,ordinal,192000);double expected=(dbPerOctave[ordinal]/6)*20*std::log10(2.0);check(std::abs(f.db(100,192000)-f.db(50,192000)-expected)<0.05,"Slope ordinal drives expected dB/oct response (6 dB label is rounded 20log10(2))");}
    std::cout<<"PASS "<<checks<<" independent enum golden checks\n";
}
