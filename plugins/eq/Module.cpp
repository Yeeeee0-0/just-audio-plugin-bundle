#include "common/vst3/Module.hpp"
#include "Engine.hpp"
#include "EditorLayout.hpp"
namespace just {
#ifdef JUST_EQ_NATIVE_EDITOR
EditorContent* createEqEditor();
#endif
namespace {
Engine* createEngine(){return new eq::EqEngine;}
const SimpleControlBinding bindings[]={
    {"Frequency",{eq::id(0,eq::frequency)},1,true,""},
    {"Gain",{eq::id(0,eq::gain)},1,true,""},
    {"Q",{eq::id(0,eq::q)},1,true,""},
    {"Shape",{eq::id(0,eq::type)},1,true,""}
};
const ParamID band01[]={eq::id(0,eq::enabled),eq::id(0,eq::type),eq::id(0,eq::target),eq::id(0,eq::frequency),eq::id(0,eq::gain),eq::id(0,eq::q),eq::id(0,eq::slope),eq::id(0,eq::dynamicEnabled),eq::id(0,eq::range),eq::id(0,eq::threshold),eq::id(0,eq::knee),eq::id(0,eq::attack),eq::id(0,eq::release),eq::id(0,eq::detector),eq::id(0,eq::source),eq::id(0,eq::slopeExtension)};
const ParamID band02[]={eq::id(1,eq::enabled),eq::id(1,eq::type),eq::id(1,eq::target),eq::id(1,eq::frequency),eq::id(1,eq::gain),eq::id(1,eq::q),eq::id(1,eq::slope),eq::id(1,eq::dynamicEnabled),eq::id(1,eq::range),eq::id(1,eq::threshold),eq::id(1,eq::knee),eq::id(1,eq::attack),eq::id(1,eq::release),eq::id(1,eq::detector),eq::id(1,eq::source),eq::id(1,eq::slopeExtension)};
const ParamID band03[]={eq::id(2,eq::enabled),eq::id(2,eq::type),eq::id(2,eq::target),eq::id(2,eq::frequency),eq::id(2,eq::gain),eq::id(2,eq::q),eq::id(2,eq::slope),eq::id(2,eq::dynamicEnabled),eq::id(2,eq::range),eq::id(2,eq::threshold),eq::id(2,eq::knee),eq::id(2,eq::attack),eq::id(2,eq::release),eq::id(2,eq::detector),eq::id(2,eq::source),eq::id(2,eq::slopeExtension)};
const ParamID band04[]={eq::id(3,eq::enabled),eq::id(3,eq::type),eq::id(3,eq::target),eq::id(3,eq::frequency),eq::id(3,eq::gain),eq::id(3,eq::q),eq::id(3,eq::slope),eq::id(3,eq::dynamicEnabled),eq::id(3,eq::range),eq::id(3,eq::threshold),eq::id(3,eq::knee),eq::id(3,eq::attack),eq::id(3,eq::release),eq::id(3,eq::detector),eq::id(3,eq::source),eq::id(3,eq::slopeExtension)};
const ParamID band05[]={eq::id(4,eq::enabled),eq::id(4,eq::type),eq::id(4,eq::target),eq::id(4,eq::frequency),eq::id(4,eq::gain),eq::id(4,eq::q),eq::id(4,eq::slope),eq::id(4,eq::dynamicEnabled),eq::id(4,eq::range),eq::id(4,eq::threshold),eq::id(4,eq::knee),eq::id(4,eq::attack),eq::id(4,eq::release),eq::id(4,eq::detector),eq::id(4,eq::source),eq::id(4,eq::slopeExtension)};
const ParamID band06[]={eq::id(5,eq::enabled),eq::id(5,eq::type),eq::id(5,eq::target),eq::id(5,eq::frequency),eq::id(5,eq::gain),eq::id(5,eq::q),eq::id(5,eq::slope),eq::id(5,eq::dynamicEnabled),eq::id(5,eq::range),eq::id(5,eq::threshold),eq::id(5,eq::knee),eq::id(5,eq::attack),eq::id(5,eq::release),eq::id(5,eq::detector),eq::id(5,eq::source),eq::id(5,eq::slopeExtension)};
const ParamID band07[]={eq::id(6,eq::enabled),eq::id(6,eq::type),eq::id(6,eq::target),eq::id(6,eq::frequency),eq::id(6,eq::gain),eq::id(6,eq::q),eq::id(6,eq::slope),eq::id(6,eq::dynamicEnabled),eq::id(6,eq::range),eq::id(6,eq::threshold),eq::id(6,eq::knee),eq::id(6,eq::attack),eq::id(6,eq::release),eq::id(6,eq::detector),eq::id(6,eq::source),eq::id(6,eq::slopeExtension)};
const ParamID band08[]={eq::id(7,eq::enabled),eq::id(7,eq::type),eq::id(7,eq::target),eq::id(7,eq::frequency),eq::id(7,eq::gain),eq::id(7,eq::q),eq::id(7,eq::slope),eq::id(7,eq::dynamicEnabled),eq::id(7,eq::range),eq::id(7,eq::threshold),eq::id(7,eq::knee),eq::id(7,eq::attack),eq::id(7,eq::release),eq::id(7,eq::detector),eq::id(7,eq::source),eq::id(7,eq::slopeExtension)};
const ParamID band09[]={eq::id(8,eq::enabled),eq::id(8,eq::type),eq::id(8,eq::target),eq::id(8,eq::frequency),eq::id(8,eq::gain),eq::id(8,eq::q),eq::id(8,eq::slope),eq::id(8,eq::dynamicEnabled),eq::id(8,eq::range),eq::id(8,eq::threshold),eq::id(8,eq::knee),eq::id(8,eq::attack),eq::id(8,eq::release),eq::id(8,eq::detector),eq::id(8,eq::source),eq::id(8,eq::slopeExtension)};
const ParamID band10[]={eq::id(9,eq::enabled),eq::id(9,eq::type),eq::id(9,eq::target),eq::id(9,eq::frequency),eq::id(9,eq::gain),eq::id(9,eq::q),eq::id(9,eq::slope),eq::id(9,eq::dynamicEnabled),eq::id(9,eq::range),eq::id(9,eq::threshold),eq::id(9,eq::knee),eq::id(9,eq::attack),eq::id(9,eq::release),eq::id(9,eq::detector),eq::id(9,eq::source),eq::id(9,eq::slopeExtension)};
const ParamID band11[]={eq::id(10,eq::enabled),eq::id(10,eq::type),eq::id(10,eq::target),eq::id(10,eq::frequency),eq::id(10,eq::gain),eq::id(10,eq::q),eq::id(10,eq::slope),eq::id(10,eq::dynamicEnabled),eq::id(10,eq::range),eq::id(10,eq::threshold),eq::id(10,eq::knee),eq::id(10,eq::attack),eq::id(10,eq::release),eq::id(10,eq::detector),eq::id(10,eq::source),eq::id(10,eq::slopeExtension)};
const ParamID band12[]={eq::id(11,eq::enabled),eq::id(11,eq::type),eq::id(11,eq::target),eq::id(11,eq::frequency),eq::id(11,eq::gain),eq::id(11,eq::q),eq::id(11,eq::slope),eq::id(11,eq::dynamicEnabled),eq::id(11,eq::range),eq::id(11,eq::threshold),eq::id(11,eq::knee),eq::id(11,eq::attack),eq::id(11,eq::release),eq::id(11,eq::detector),eq::id(11,eq::source),eq::id(11,eq::slopeExtension)};
const ParamID levels[]={1,2};
const AdvancedGroup groups[]={{"Band 01",band01,std::size(band01)},{"Band 02",band02,std::size(band02)},{"Band 03",band03,std::size(band03)},{"Band 04",band04,std::size(band04)},{"Band 05",band05,std::size(band05)},{"Band 06",band06,std::size(band06)},{"Band 07",band07,std::size(band07)},{"Band 08",band08,std::size(band08)},{"Band 09",band09,std::size(band09)},{"Band 10",band10,std::size(band10)},{"Band 11",band11,std::size(band11)},{"Band 12",band12,std::size(band12)},{ "Levels",levels,std::size(levels)}};
StatusSnapshot status(const SoundState&) {StatusSnapshot s;s.mode="Minimum phase / 0 samples";return s;}
bool validateAudition(ParamID anchor){for(std::size_t b=0;b<eq::bandCount;++b)if(eq::id(b,eq::frequency)==anchor)return true;return false;}
const ModuleDefinition definition{eq::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),
#ifdef JUST_EQ_NATIVE_EDITOR
    createEqEditor,
#else
    nullptr,
#endif
    status,{true,true,false,true},nullptr,nullptr,nullptr,0,nullptr,validateAudition,
    eq::minimumEditorWidth,eq::minimumEditorHeight,eq::defaultEditorWidth,eq::defaultEditorHeight,true};
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
