#include "plugins/fake_stereo/FieldModel.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;using namespace just::stereo;
static void check(bool ok,const char* m){if(!ok){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
int main(){
    SampleFrame s;s.header={1,1,1,0,4,0,48000,2,2,0,analysisPlaying,1};s.count=4;
    FieldMeasurement f;
    for(unsigned i=0;i<4;++i)s.samples[2][i]=s.samples[3][i]=(i%2?-.5f:.5f);
    check(f.accept(s) && f.correlationValid && f.correlation==1 && f.peakMS[1]==0,"dual mono is measured centered and fully correlated");
    for(unsigned i=0;i<4;++i){check(f.points[i].side==0 && f.points[i].mid==.5,"folding preserves real dual mono");s.samples[3][i]=-s.samples[2][i];}
    check(f.accept(s) && f.correlation==-1 && f.peakMS[0]==0 && f.peakMS[1]==.5,"anti phase has real side and negative correlation");
    s.samples[2]={.5f,-.5f,0,0};s.samples[3]={0,0,.5f,-.5f};check(f.accept(s),"L/R impulses accepted");
    check(f.points[0].side<0 && f.points[1].side<0 && f.points[2].side>0 && f.points[3].side>0,"L left / R right through antipodal folding");
    for(auto dims:{std::pair<double,double>{360,292},{568,292},{910,390}}){auto g=FieldGeometry::fit(dims.first,dims.second);auto zero=g.pixel({},1),x=g.pixel({.5,0},1),y=g.pixel({0,.5},1);check(std::abs((x.side-zero.side)-(zero.mid-y.mid))<1e-12,"equal pixel scale at every layout size");}
    s.samples[2][0]=4;check(f.accept(s) && f.scale>1 && f.peakLR[0]==4,"over-unity sample rescales both axes and preserves actual meters");
    s.samples[2][0]=NAN;check(!f.accept(s) && !f.count && !f.valid,"nonfinite frame clears plot");
    s={};check(!f.accept(s),"unavailable data cannot create a plot");
    std::cout<<"PASS measured field: dual mono, anti-phase, L/R direction, equal geometry, peaks, overload, invalid input\n";
}
