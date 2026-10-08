#pragma once
#include "SimpleMacros.hpp"
namespace just::reverb {
inline const char* detailLabelZh(ParamID id) noexcept {
    constexpr const char* labels[]={"旁路","衰减时间","预延时","高切","混合","空间类型","空间大小","后期反射比例","扩散",
        "低频衰减","中频衰减","高频衰减","低频分频点","高频分频点","调制速率","调制深度","低切",
        "均衡 1 频率","均衡 1 增益","均衡 1 Q","均衡 1 开关","均衡 2 频率","均衡 2 增益","均衡 2 Q","均衡 2 开关",
        "立体声宽度","湿声电平","输入增益","输出增益","闪避量","闪避阈值","闪避启动","闪避释放","冻结","预延时同步","预延时节拍"};
    static_assert(std::size(labels)==std::size(parameters));
    const auto index=registry.index(id);return index<registry.count?labels[index]:nullptr;
}
inline const char* macroHelp(SimpleMacro macro,bool chinese) noexcept {
    constexpr const char* en[]={"Brightness: wet tone and high-frequency decay","Character: modulation and diffusion texture",
        "Distance: proximity through early/late balance and diffusion","Space: base decay and room size within the selected style",
        "Decay Rate: scales low/mid/high decay together; its available range follows their current proportions",
        "Stereo Width: wet stereo spread, 0–150%","Mix: dry/wet blend, 0–100%"};
    constexpr const char* zh[]={"明亮度：湿声音色与高频衰减","质感：调制与扩散的纹理","距离：通过早晚反射比例与扩散调整远近",
        "空间：当前空间类型内的基准衰减时间与大小","衰减速率：同时调整低、中、高频衰减，可达范围取决于当前三段比例",
        "立体声宽度：湿声的立体声展开，0–150%","混合：干声与湿声比例，0–100%"};
    return (chinese?zh:en)[static_cast<unsigned>(macro)];
}
}
