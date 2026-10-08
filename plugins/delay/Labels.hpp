#pragma once
#include "Parameters.hpp"

namespace just::delay {
// Display text only: no parameter key, enum ordinal or sound-state change.
inline const char* chineseLabel(ParamID id) noexcept {
    switch(id){
        case bypass:return "旁通";case timeL:return "左侧时间";case timeR:return "右侧时间";
        case syncL:return "左侧同步";case syncR:return "右侧同步";case noteL:return "左侧分音符";case noteR:return "右侧分音符";
        case route:return "路由";case start:return "起始侧";case feedback:return "反馈";case crossfeed:return "交叉反馈";
        case highPass:return "低切";case highCut:return "高切";case mix:return "混合";case localTempo:return "备用速度";
        case timeChange:return "时间变换";case slew:return "过渡时间";case input:return "输入";case output:return "输出";
        case drive:return "湿声驱动";case tilt:return "湿声倾斜";case modRate:return "调制速率";case modDepth:return "调制深度";
        case modPhase:return "调制立体声相位";case duckAmount:return "闪避量";case duckThreshold:return "闪避阈值";
        case duckAttack:return "闪避启动";case duckRelease:return "闪避释放";case width:return "湿声宽度";
        case wetLevel:return "湿声电平";case freeze:return "冻结";
        case tap1Enabled:return "抽头 1 开关";case tap1Level:return "抽头 1 电平";case tap1Pan:return "抽头 1 声像";
        case tap2Enabled:return "抽头 2 开关";case tap2Level:return "抽头 2 电平";case tap2Pan:return "抽头 2 声像";
        case tap3Enabled:return "抽头 3 开关";case tap3Time:return "抽头 3 时间";case tap3Level:return "抽头 3 电平";case tap3Pan:return "抽头 3 声像";
        case tap4Enabled:return "抽头 4 开关";case tap4Time:return "抽头 4 时间";case tap4Level:return "抽头 4 电平";case tap4Pan:return "抽头 4 声像";
        default:return "";
    }
}
inline constexpr const char* chineseGroups[]={"时间与同步","路由与反馈","抽头","音色与调制","闪避","电平"};
}
