#pragma once
#include <algorithm>

namespace just::eq {
// Logical points: the common shell reserves 84 above and 40 below the content.
// The graph fills this viewport; the selected-band controls overlay its bottom.
inline constexpr int minimumEditorWidth=760;
inline constexpr int minimumContentHeight=436;
inline constexpr int minimumEditorHeight=84+minimumContentHeight+40;
inline constexpr int defaultEditorWidth=1000,defaultEditorHeight=720;
inline int mainViewHeight(int viewportHeight,bool advanced) noexcept {
    return advanced?std::clamp(viewportHeight,minimumContentHeight,460):
        std::max(viewportHeight,minimumContentHeight);
}
}
