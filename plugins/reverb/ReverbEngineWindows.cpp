// Windows translation-unit adapter: keep the accepted DSP implementation intact.
// MSVC must emit both render specializations for callers of inline process().
// Compile this file instead of ReverbEngine.cpp, never alongside it.
#include "ReverbEngine.cpp"

namespace just::reverb {
template void ReverbEngine::render<float>(AudioBlock<float>, const ProcessContext&) noexcept;
template void ReverbEngine::render<double>(AudioBlock<double>, const ProcessContext&) noexcept;
}
