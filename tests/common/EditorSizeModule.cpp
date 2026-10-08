// Test-only adapter. Original causal fixture and editor remain unchanged.
#define moduleDefinition sizeFixtureOriginalDefinition
#include "UiEffectTestModule.cpp"
#undef moduleDefinition
namespace just {
const ModuleDefinition& moduleDefinition(){
    static const auto sized=[](){auto m=sizeFixtureOriginalDefinition();m.minimumEditorWidth=900;m.minimumEditorHeight=620;return m;}();
    return sized;
}
}
