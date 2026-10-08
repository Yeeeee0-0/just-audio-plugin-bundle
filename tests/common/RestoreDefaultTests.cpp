#include "common/state/State.hpp"
#include <iostream>
#include <limits>
using namespace just;
static unsigned checks=0;
static void check(bool b,const char* s){++checks;if(!b){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}}
static constexpr const char* names[]={"t0","t1","t2","t3","t4","t5","t6","t7","t8","t9","t10"};
static void u32(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(int n=0;n<4;++n)b[at+n]=std::uint8_t(v>>(8*n));}
int main(){
    std::array<ParameterSpec,11> specs{};
    for(unsigned i=0;i<specs.size();++i)specs[i]={names[i],i,names[i],"",0,1,.25,Mapping::linear,0,true,"test",Transition::continuous,0};
    specs[10].initial=1;specs[10].stepCount=1;specs[10].automatable=false;specs[10].restoreMissingDefault=0.;
    ParameterRegistry current{specs.data(),specs.size()};const Uid id{{1,2,3,4}};
    check(current.valid(),"optional physical restore default accepted");
    check(initialState(id,current).targets[10]==1,"fresh instance and Init retain new default1");
    for(std::size_t count:{9u,10u}){
        ParameterRegistry old{specs.data(),count};auto state=initialState(id,old);
        for(std::size_t i=0;i<count;++i)state.targets[i]=double(i)/10.;
        state.targets[7]=1;state.seed=0x123456789abcdefULL;state.configurationCount=1;state.configurations[0]={91,.625};
        auto bytes=encodeState(state,old),original=bytes;auto restored=initialState(id,current);
        check(decodeState(bytes.data(),bytes.size(),id,current,restored)==StateResult::ok,"old9/10-ID schema1 state decodes");
        for(std::size_t i=0;i<count;++i)check(restored.targets[i]==state.targets[i],"all present old targets, including mode ID7, preserved exactly");
        check(restored.targets[10]==0 && (count==10 || restored.targets[9]==.25),"missing opt-in restores0; other missing parameter still uses Init");
        check(restored.seed==state.seed && restored.configurationCount==1 && restored.configurations[0].id==91 && restored.configurations[0].value==.625,"seed/configuration preserved");
        check(restored.schemaVersion==1 && restored.engineVersion==1 && bytes==original,"wire versions and input bytes unchanged");
        auto saved=encodeState(restored,current);SoundState twice;
        check(decodeState(saved.data(),saved.size(),id,current,twice)==StateResult::ok && twice.targets==restored.targets,"resave explicitly retains compatibility choice");
        std::cout<<"PASS old_count="<<count<<" missing_algorithm="<<restored.targets[10]<<" new_init="<<initialState(id,current).targets[10]<<"\n";
    }
    for(double explicitValue:{0.,1.}){
        auto s=initialState(id,current);s.targets[10]=explicitValue;auto b=encodeState(s,current);SoundState out;
        check(decodeState(b.data(),b.size(),id,current,out)==StateResult::ok && out.targets[10]==explicitValue,"explicit encoded algorithm0/1 overrides missing-default policy");
    }
    auto unset=specs;unset[10].restoreMissingDefault.reset();ParameterRegistry unchanged{unset.data(),unset.size()};
    auto old=initialState(id,{specs.data(),10});auto valid=encodeState(old,{specs.data(),10});SoundState destination;
    check(decodeState(valid.data(),valid.size(),id,unchanged,destination)==StateResult::ok && destination.targets[10]==1,"unset option preserves old decoder behavior");
    auto sentinel=initialState(id,current);sentinel.targets[1]=.987;sentinel.seed=876;sentinel.configurationCount=1;sentinel.configurations[0]={19,2};
    const auto sentinelBytes=encodeState(sentinel,current);
    auto reject=[&](std::vector<std::uint8_t> b,StateResult result,Uid expected,ParameterRegistry r){
        auto out=sentinel;check(decodeState(b.data(),b.size(),expected,r,out)==result,"malformed/unsupported/foreign state rejected");
        check(encodeState(out,current)==sentinelBytes,"failed restore leaves complete destination unchanged");
    };
    auto bad=valid;bad.pop_back();reject(bad,StateResult::malformed,id,current);
    bad=valid;u32(bad,20,2);reject(bad,StateResult::unsupportedSchema,id,current);
    bad=valid;u32(bad,24,2);reject(bad,StateResult::unsupportedSchema,id,current);
    reject(valid,StateResult::wrongPlugin,Uid{{5,2,3,4}},current);
    bad=valid;u32(bad,56,0);reject(bad,StateResult::malformed,id,current);
    bad=valid;double nan=std::numeric_limits<double>::quiet_NaN();std::memcpy(bad.data()+48,&nan,8);reject(bad,StateResult::malformed,id,current);
    for(double invalid:{-1.,2.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto broken=specs;broken[10].restoreMissingDefault=invalid;ParameterRegistry r{broken.data(),broken.size()};
        check(!r.valid(),"nonfinite/out-of-range restore default is invalid");reject(valid,StateResult::malformed,id,r);
    }
    bad=valid;u32(bad,44,777);SoundState unknown;
    check(decodeState(bad.data(),bad.size(),id,current,unknown)==StateResult::ok && unknown.targets[0]==.25 && unknown.targets[10]==0,"unknown IDs still ignored; missing policies remain per-parameter");
    auto physical=specs;physical[10].minimum=10;physical[10].maximum=1000;physical[10].initial=1000;physical[10].mapping=Mapping::logarithmic;physical[10].stepCount=0;physical[10].restoreMissingDefault=100;
    ParameterRegistry logRegistry{physical.data(),physical.size()};SoundState logState;
    check(logRegistry.valid() && decodeState(valid.data(),valid.size(),id,logRegistry,logState)==StateResult::ok && std::abs(logState.targets[10]-.5)<1e-12,"restore default uses physical units and existing mapping");
    std::cout<<"PASS "<<checks<<" optional missing-restore/default/schema/transaction checks\n";
}
