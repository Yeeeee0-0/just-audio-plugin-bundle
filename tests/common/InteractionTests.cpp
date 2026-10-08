#include "common/ui/Controls.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
static void require(bool ok){if(!ok){std::cerr<<"FAIL contract assertion\n";std::abort();}}
int main(){
    using namespace just;
    require(rotaryAngle(0)==-135 && rotaryAngle(.5)==0 && rotaryAngle(1)==135);
    RotaryDrag drag;drag.begin(.5,100);require(std::abs(drag.move(90,false)-.55)<1e-14);
    require(std::abs(drag.move(80,true)-.556)<1e-14);require(std::abs(drag.move(90,false)-.506)<1e-14);
    drag.begin(.01,100);require(drag.move(200,false)==0);require(std::abs(drag.move(199,false)-.005)<1e-14);
    require(RotaryLayout::fit(74,102).diameter==56 && RotaryLayout::fit(186,218).diameter==172);
    ParameterSpec spec{"gain",1,"Gain","dB",-60,12,0,Mapping::linear,0,true,"all",Transition::continuous,0};
    TextEditSession session;DisplayPolicy policy;const double original=.501234567891234;auto exact=session.begin(spec,policy,original);double next=-1;
    require(!session.changed(spec,policy,exact.c_str(),next));require(session.changed(spec,policy,"-10.123456789",next));require(std::abs(spec.toPhysical(next)+10.123456789)<1e-12);
    struct Sink {std::vector<int> trace;int fail=0;} sink;
    EditorServices services;services.owner=&sink;services.beginEdit=[](void* p,ParamID id){auto& s=*static_cast<Sink*>(p);s.trace.push_back(id);return id!=s.fail;};services.endEdit=[](void* p,ParamID id){static_cast<Sink*>(p)->trace.push_back(-int(id));};services.performEdit=[](void*,ParamID,double){return true;};
    MultiParameterGesture fanout(services);ParamID ids[]={120,121,122};require(fanout.begin(ids,3));double values[]={.4,.5,.6};require(fanout.write(values,3));fanout.end();require((sink.trace==std::vector<int>{120,121,122,-122,-121,-120}));
    sink.trace.clear();sink.fail=121;require(!fanout.begin(ids,3));require((sink.trace==std::vector<int>{120,121,-120}));
    ParamID duplicate[]={1,1};sink.trace.clear();require(!fanout.begin(duplicate,2)&&sink.trace.empty());
    policy.context=&next;policy.formatWithContext=[](void* p,double,DisplayContext,char* out,std::size_t n){std::snprintf(out,n,"%.3f",*static_cast<double*>(p));};char out[128];formatDisplay(spec,.5,DisplayContext::simple,policy,out,sizeof(out));require(std::string(out)!="-24.0");
    std::cout<<"PASS rotary direction/angle, continuous Shift, endpoint recovery, compact/hero geometry, precise unchanged edit, bounded multi-ID failure balance, contextual format\n";
}
