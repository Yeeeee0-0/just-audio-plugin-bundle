#include "common/vst3/Module.hpp"
#include "Parameters.hpp"
#include "Dsp.hpp"
#include "Editor.hpp"
namespace just {
namespace {
Engine* createTremoloEngine(){return new tremolo::TremoloEngine;}
StatusSnapshot effectStatus(const SoundState& state) {
    StatusSnapshot result;result.mode="Tremolo";
    if(tremolo::physical(state,tremolo::sync)>=0.5)result.mode="Tremolo · Sync";
    else if(tremolo::physical(state,tremolo::timeMode)==1)result.mode="Transport requires Sync · Free fallback";
    return result;
}
// The shared owner approved this mapping on 2026-10-02; see parameter-golden.json.
const ModuleDefinition definition=[] {ModuleDefinition configured{
    tremolo::registry,createTremoloEngine,
    tremolo::simpleControls,std::size(tremolo::simpleControls),
    tremolo::advancedGroups,std::size(tremolo::advancedGroups),
    tremolo::createEditorContent,effectStatus
};configured.minimumEditorWidth=720;configured.minimumEditorHeight=420;return configured;}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
