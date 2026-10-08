#include "common/vst3/Module.hpp"
#include "common/vst3/PluginIdentities.hpp"
#include "Engine.hpp"
#include "EditorModel.hpp"
namespace just {
namespace {
Engine* createEngine(){return new distortion::DistortionEngine;}
bool validatePresetState(const SoundState& state){
    if(!validCompleteState(state,pluginIdentities[9].processor,distortion::registry) || state.configurationCount!=0)return false;
    // Preset transactions support only the existing fixed 4x preparation.
    // Other saved Quality requests remain host-state/pending values; accepting
    // them here would imply a prepare/PDC operation that is not implemented.
    const auto index=distortion::registry.index(distortion::quality);
    return state.targets[index]==distortion::parameters[index].toNormalized(2);
}
const SimpleControlBinding bindings[]={
    {"Model",{distortion::model},1,true,nullptr},
    {"Drive / Bits",{distortion::drive_db,distortion::crush_bits},2,true,nullptr},
    {"Mix",{distortion::mix},1,true,nullptr}
};
const ParamID pre[]={distortion::input_db,distortion::pre_hp_enabled,distortion::pre_hp_hz,distortion::pre_tilt_db};
const ParamID nonlinear[]={distortion::drive_db,distortion::boost,distortion::bias,distortion::soft_shape,distortion::hard_softness,distortion::asym_shape,distortion::fold_shape,distortion::crush_bits,distortion::crush_hold_hz,distortion::crush_aa,distortion::crush_dither};
const ParamID post[]={distortion::post_bass_db,distortion::post_treble_db,distortion::post_lp_enabled,distortion::post_lp_hz,distortion::makeup_db,distortion::drive_comp,distortion::match_gain_db,distortion::output_db,distortion::quality};
const AdvancedGroup groups[]={{"Pre",pre,std::size(pre)},{"Drive / Model",nonlinear,std::size(nonlinear)},{"Post / Output",post,std::size(post)}};
StatusSnapshot status(const SoundState& s){
    static constexpr const char* modes[]={"Clean · 4x · 32 samples","Soft · 4x · 32 samples","Hard · 4x · 32 samples","Asym · 4x · 32 samples","Fold · 4x · 32 samples","Crush · 4x · 32 samples"};
    StatusSnapshot result;result.mode=modes[int(distortion::physical(s,distortion::model))];result.qualityPending=distortion::physical(s,distortion::quality)!=2;return result;
}
const ModuleDefinition definition=[] {
    ModuleDefinition configured{distortion::registry,createEngine,bindings,std::size(bindings),groups,std::size(groups),distortion::createEditorContent,status,{true,true,true,false}};
    configured.simpleHasCustom=distortion::simpleHasCustom;
    configured.validatePresetState=validatePresetState;
    configured.minimumEditorWidth=880;
    configured.minimumEditorHeight=584;
    return configured;
}();
}
const ModuleDefinition& moduleDefinition(){return definition;}
}
