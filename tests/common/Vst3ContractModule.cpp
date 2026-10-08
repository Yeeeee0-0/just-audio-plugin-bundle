// Dedicated no-SC, zero-PDC/tail pass-through fixture for Vst3Tests.cpp.
// Never a product target. Real EQ and other effects keep their own module suites.
#include "common/vst3/Module.hpp"
namespace just {
namespace {
constexpr ParameterSpec parameters[]={
    {"fixture.bypass",0,"Bypass","",0,1,0,Mapping::linear,1,true,"all",Transition::discrete,0}
};
Engine* createEngine(){return new PassthroughEngine;}
const ModuleDefinition definition=[](){ModuleDefinition m;
    m.parameters={parameters,std::size(parameters)};m.createEngine=createEngine;
    m.buses={true,true,false,false};return m;}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
