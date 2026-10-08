// Optional acceptance adapter over an immutable module source snapshot. Never a product target.
#include "common/vst3/Module.hpp"
#include "Presets.hpp"
#include <utility>
// Retain the snapshot's actual engine/editor/bindings/status and add only the
// new optional common catalog hooks. This never edits its Module.cpp.
#define moduleDefinition reverbAcceptanceOriginalDefinition
#include "Module.cpp"
#undef moduleDefinition
namespace just {
namespace {
template<std::size_t I> bool buildPreset(Uid uid,SoundState& out){out=reverb::presetState(I,uid);return true;}
template<std::size_t... I> auto catalog(std::index_sequence<I...>){return std::array<FactoryPreset,sizeof...(I)>{{{reverb::presets[I].name,reverb::presets[I].name,reverb::presets[I].category,buildPreset<I>}...}};}
const auto factory=catalog(std::make_index_sequence<std::size(reverb::presets)>{});
const ModuleDefinition testDefinition=[](){ModuleDefinition d=reverbAcceptanceOriginalDefinition();
    d.factoryPresets=factory.data();d.factoryPresetCount=factory.size();d.bypassTooltip="Reverb acceptance fixture";
    d.validatePresetState=[](const SoundState& s){return s.configurationCount==0;};return d;}();
}
const ModuleDefinition& moduleDefinition(){return testDefinition;}
}
