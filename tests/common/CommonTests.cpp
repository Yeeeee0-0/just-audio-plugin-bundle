#include "common/parameters/Automation.hpp"
#include "common/dsp/Engine.hpp"
#include "common/ui/Model.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include <iostream>
#include <new>
#include <thread>
#include <random>
static thread_local bool realtime=false;
static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t size){if(realtime)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool result,const char* label){if(!result){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}}
bool near(double a,double b){return std::abs(a-b)<1e-10;}
using namespace just;
const char* choices[]={"A","B","C"};
ParameterSpec fixture[]={
    {"test.level",100,"Level","dB",-24,24,0,Mapping::linear,0,true,"all",Transition::continuous,5},
    {"test.cutoff",101,"Cutoff","Hz",20,20000,1000,Mapping::logarithmic,0,true,"all",Transition::continuous,5},
    {"test.hidden",102,"Hidden","",0,2,0,Mapping::linear,2,true,"all",Transition::discrete,0,choices}};
// Fixture parameters are test-only and are not effect product IDs.
ParameterRegistry registry{fixture,3};
struct Events final:EventSource {
    std::array<std::array<ParamEvent,5>,3> data{};std::array<int,3> counts{};int queues=3;
    std::int32_t queueCount() const noexcept override{return queues;}
    ParamID parameterID(std::int32_t q) const noexcept override{return fixture[q].id;}
    std::int32_t pointCount(std::int32_t q) const noexcept override{return counts[q];}
    bool point(std::int32_t q,std::int32_t p,ParamEvent& e) const noexcept override{e=data[q][p];return true;}
};
struct Sink final:EditSink {
    int starts=0,ends=0,writes=0;
    bool beginEdit(ParamID) override{++starts;return true;}
    bool performEdit(ParamID,double) override{++writes;return true;}
    void endEdit(ParamID) override{++ends;}
};
void parameterTests() {
    require(registry.valid(),"registry valid");
    for(const auto& p:fixture)for(double value:{0.0,0.25,0.5,0.75,1.0}) {
        double expected=p.stepCount?std::round(value*p.stepCount)/p.stepCount:value;
        require(near(p.toNormalized(p.toPhysical(value)),expected),"mapping round trip");
    }
    double old=0.25;require(!fixture[0].parse("bad",old) && old==0.25,"parse preserves target on failure");
    require(!fixture[0].parse("nan",old),"nonfinite parse rejected");
    require(fixture[1].parse("1000 Hz",old) && near(fixture[1].toPhysical(old),1000),"single units parser");
    char formatted[128];fixture[1].format(old,formatted,sizeof(formatted));require(near(fixture[1].toPhysical(old),1000),"format does not write target");
    auto duplicate=fixture[1];duplicate.id=fixture[0].id;ParameterSpec invalid[]={fixture[0],duplicate};
    require(!ParameterRegistry{invalid,2}.valid(),"duplicate IDs rejected");
    require(fixture[1].toPhysical(1)==20000,"saved cutoff target independent of sample rate");
}
void stateTests() {
    auto id=pluginIdentities[0].processor;auto s=initialState(id,registry);s.targets[2]=1;s.seed=123456;
    s.configurationCount=1;s.configurations[0]={88,4};
    auto bytes=encodeState(s,registry);auto destination=initialState(id,registry);
    require(decodeState(bytes.data(),bytes.size(),id,registry,destination)==StateResult::ok,"state round trip");
    require(destination.targets==s.targets && destination.seed==s.seed && destination.configurations[0].value==4,"hidden and configuration retained");
    auto saved=destination;
    require(decodeState(bytes.data(),bytes.size()-1,id,registry,destination)==StateResult::malformed,"truncated state rejected");
    require(destination.targets==saved.targets,"failed restore is transactional");
    require(decodeState(bytes.data(),bytes.size(),pluginIdentities[1].processor,registry,destination)==StateResult::wrongPlugin,"cross plugin state rejected");
    bytes[20]=2;require(decodeState(bytes.data(),bytes.size(),id,registry,destination)==StateResult::unsupportedSchema,"unknown schema diagnosed");
    bytes=encodeState(s,registry);double nan=std::numeric_limits<double>::quiet_NaN();std::memcpy(bytes.data()+48,&nan,8);
    require(decodeState(bytes.data(),bytes.size(),id,registry,destination)==StateResult::malformed,"NaN state rejected");
    bytes=encodeState(s,registry);bytes[44]=0xEE;bytes[45]=0xEE;bytes[46]=0;bytes[47]=0;
    require(decodeState(bytes.data(),bytes.size(),id,registry,destination)==StateResult::ok,"unknown fields ignored");
    require(destination.targets[0]==fixture[0].toNormalized(fixture[0].initial),"missing schema1 field gets declared Init");
    // There is no historical release schema yet. Do not claim legacy migration.
}
void automationTests() {
    auto state=initialState(pluginIdentities[0].processor,registry);state.targets[0]=0;state.targets[1]=0;state.targets[2]=0;
    Events e;e.counts={3,2,2};
    e.data[0]={ParamEvent{100,0.5,3},ParamEvent{100,0.25,5},ParamEvent{100,1,7}};
    e.data[1]={ParamEvent{101,0.75,3},ParamEvent{101,1,7}};
    e.data[2]={ParamEvent{102,1,3},ParamEvent{102,0,7}};
    Automation a(registry);a.begin(&e,state,8);
    a.evaluate(0,state);require(near(state.targets[0],0.125) && state.targets[2]==0,"implicit -1 interpolation and discrete hold");
    a.evaluate(3,state);require(state.targets[0]==0.5 && state.targets[1]==0.75 && state.targets[2]==1,"same-offset full snapshot");
    a.evaluate(5,state);require(state.targets[0]==0.25,"middle point retained");
    a.evaluate(7,state);require(state.targets[0]==1 && state.targets[2]==0,"last point retained");
    e.counts={1,0,0};e.data[0][0]={100,0.33,0};a.begin(&e,state,0);a.evaluate(0,state);require(near(state.targets[0],0.33),"zero sample flush");
    e.data[0][0]={100,std::numeric_limits<double>::infinity(),0};a.begin(&e,state,1);a.evaluate(0,state);require(near(state.targets[0],0.33),"nonfinite event ignored");
}
template<class Sample> void audioTests() {
    std::array<Sample,2048> left{},right{},outLeft{},outRight{};
    std::mt19937 random(20261001);
    for(std::size_t i=0;i<left.size();++i){left[i]=Sample((int(random()%2000)-1000)/200.0);right[i]=-left[i];}
    for(double rate:{44100,48000,96000,192000})for(unsigned channels:{1,2}) {
        PassthroughEngine engine;PrepareSpec spec;spec.sampleRate=rate;spec.inputChannels=spec.outputChannels=channels;
        spec.format=std::is_same<Sample,float>::value?SampleFormat::float32:SampleFormat::float64;
        require(engine.prepare(spec),"prepare matrix");
        auto state=initialState(pluginIdentities[0].processor,registry);
        for(unsigned n:{0,16,32,64,128,256,1024,2048,17,731}) {
            AudioBlock<Sample> b{{left.data(),right.data()},{outLeft.data(),outRight.data()},channels,channels,n,0};
            unsigned before=allocations.load();realtime=true;engine.applyTargets(state,0);engine.process(b,{});engine.endBlock();realtime=false;
            require(allocations.load()==before,"process no new/new[]");
            for(unsigned i=0;i<n;++i)require(outLeft[i]==left[i] && (channels==1 || outRight[i]==right[i]),"finite over-0dBFS pass through");
            EditorViewState view;
            for(int toggle=0;toggle<16;++toggle){view.advanced=!view.advanced;} // no Engine reference in view state
            require(state.targets==initialState(pluginIdentities[0].processor,registry).targets,"view has no sound transaction");
        }
        AudioBlock<Sample> inPlace{{left.data(),right.data()},{left.data(),right.data()},channels,channels,32,1};
        engine.process(inPlace,{});for(unsigned i=0;i<32;++i)require(left[i]==0,"silence flag zeroes input in-place");
        left[0]=std::numeric_limits<Sample>::quiet_NaN();inPlace.inputSilenceFlags=0;engine.process(inPlace,{});
        require(left[0]==0,"invalid input isolated");
        require(engine.latencySamples()==0 && engine.tailSamples().kind==TailKind::none,"actual placeholder latency and tail");
    }
}
void threadingTests() {
    struct Pair {std::uint64_t a=0,b=0;};
    AtomicSnapshot<Pair> snapshot;snapshot.publish({});
    std::atomic<bool> done{false};
    std::thread producer([&]{for(std::uint64_t i=1;i<100000;++i)snapshot.publish({i,i});done.store(true);});
    while(!done.load()){Pair p;if(snapshot.read(p))require(p.a==p.b,"concurrent snapshot not torn");}
    producer.join();
    SpscQueue<Pair,4> queue;require(queue.push({1,1}) && queue.push({2,2}) && queue.push({3,3}) && !queue.push({4,4}),"queue saturation bounded");
    Pair p;require(queue.pop(p) && p.a==1,"queue ordered");require(queue.push({4,4}),"queue resumes after drain");
}
int main() {
    parameterTests();stateTests();automationTests();audioTests<float>();audioTests<double>();threadingTests();
    Sink sink;{EditGesture gesture(sink,100);gesture.update(0.5);gesture.cancel();}
    require(sink.starts==1 && sink.ends==1 && sink.writes==1,"gesture ends exactly once on cancel");
    for(std::size_t i=0;i<pluginIdentities.size();++i) {
        require(pluginIdentities[i].simpleCount<=4,"Simple budget");
        for(std::size_t j=0;j<i;++j)require(!(pluginIdentities[i].processor==pluginIdentities[j].processor) && !(pluginIdentities[i].controller==pluginIdentities[j].controller),"unique product identities");
    }
    std::cout<<"PASS common contracts; seed=20261001; float32/64; 44.1/48/96/192kHz; mono/stereo; no process new/new[]\n";
}
