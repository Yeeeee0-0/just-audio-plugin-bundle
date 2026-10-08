#include "plugins/fake_stereo/EditorModel.hpp"
#include <iostream>
#include <cstdlib>
using namespace just;using namespace just::stereo;
static void check(bool ok,const char* m){if(!ok){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
struct Sink {
    SoundState state=initialState({},registry);unsigned begins=0,writes=0,ends=0;ParamID active=~ParamID(0);EditorViewState view;
    EditorServices services(){return {this,&view,
        [](void* p,ParamID id){auto& s=*static_cast<Sink*>(p);return s.state.targets[registry.index(id)];},
        [](void* p,ParamID id){auto& s=*static_cast<Sink*>(p);check(s.active==~ParamID(0),"no overlapping gesture");s.active=id;++s.begins;return true;},
        [](void* p,ParamID id,double n){auto& s=*static_cast<Sink*>(p);check(s.active==id,"same gesture ParamID");s.state.targets[registry.index(id)]=n;++s.writes;return true;},
        [](void* p,ParamID id){auto& s=*static_cast<Sink*>(p);check(s.active==id,"end same gesture");s.active=~ParamID(0);++s.ends;}};}
};
int main() {
    check(registry.valid() && registry.count==18,"complete explicit registry");
    constexpr ParamID golden[]={0,10,11,20,21,22,30,31,32,40,41,50,60,61,62,63,64,65};
    constexpr double init[]={0,0,0,60,100,100,150,20000,0,0,0,0,1,0,0,0,0,0};
    for(std::size_t i=0;i<registry.count;++i) {
        const auto& s=parameters[i];check(s.id==golden[i] && s.initial==init[i],"golden IDs and V2 Init");
        for(double n:{0.,.01,.3,.5,.99,1.}) {
            double round=s.toNormalized(s.toPhysical(n));
            check(std::abs(round-(s.stepCount?std::round(n*s.stepCount)/s.stepCount:n))<1e-12,"normalized roundtrip");
        }
    }
    check(std::abs(spec(Low).toPhysical(.5)-std::sqrt(30.*500.))<1e-10,"fixed logarithmic Hz mapping");
    check(spec(Character).toPhysical(0)==0 && spec(Character).toPhysical(1)==1,"golden Character ordinals");
    Sink sink;SoundState complete=sink.state;complete.seed=20261002;
    for(std::size_t i=0;i<registry.count;++i)complete.targets[i]=double(i+1)/(registry.count+1);
    auto bytes=encodeState(complete,registry);SoundState decoded;
    check(decodeState(bytes.data(),bytes.size(),{},registry,decoded)==StateResult::ok && decoded.targets==complete.targets && decoded.seed==complete.seed,"all hidden targets and seed restore exactly");
    SoundState saved=decoded;check(decodeState(bytes.data(),bytes.size()-1,{},registry,decoded)==StateResult::malformed && decoded.targets==saved.targets,"truncated restore transactional");
    // Off and a custom retained cutoff survive independently, without Fs remapping.
    sink.state.targets[registry.index(High)]=spec(High).toNormalized(12345);
    sink.state.targets[registry.index(HighEnabled)]=0;
    const auto initial=sink.state.targets;
    {
        EditorModel model(sink.services());
        for(int i=0;i<100;++i){sink.view.advanced=!sink.view.advanced;for(auto id:golden)model.read(id);}
        check(sink.writes==0 && sink.state.targets==initial,"view/read/construction cannot write sound");
        check(!model.text(Width,"nan") && !model.text(Width,"201%") && sink.writes==0,"invalid numeric edit preserves target");
        check(model.text(Width,"86%") && model.text(Character,"Diffuse"),"valid numeric/enum gestures");
        check(model.holdMono() && model.read(Mono)==1,"temporary Mono Check");model.releaseMono();
        check(model.read(Mono)==0,"release restores unlocked state");
        model.toggleMono();check(model.read(Mono)==1,"locked Mono Check");
        check(model.holdMono(),"hold while locked");model.releaseMono();check(model.read(Mono)==1,"release preserves lock");
        check(model.holdMono(),"hold then another control");check(model.write(Mix,.37),"another gesture releases audition safely");
        model.releaseMono();check(model.read(Mix)==.37,"release never writes another ParamID");
        check(model.begin(Low) && model.update(.7),"open continuous gesture");
    }
    check(sink.begins==sink.ends && sink.active==~ParamID(0),"close cancels unfinished gesture");
    check(sink.state.targets[registry.index(High)]==initial[registry.index(High)] && sink.state.targets[registry.index(HighEnabled)]==0,"unrelated UI preserves disabled Hz");
    std::cout<<"PASS golden parameters, complete transactional state, hidden values, pure views, numeric parsing, balanced gestures and locked/temporary Mono\n";
}
