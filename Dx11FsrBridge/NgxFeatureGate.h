// NgxFeatureGate.h —— NGX「初始化前能力准入」的**纯决策层**（零 D3D / 零 NGX / 零全局 / 零 I/O）。
//
// ============================================================================
// 这一层解决什么
// ============================================================================
// `NgxBackend.cpp` 的 `ensure_init()` 第 5b 步已经在用 `NVSDK_NGX_D3D11_GetFeatureRequirements`
// 问「本机的 DLSS SuperSampling 支不支持」，第 6 步才 `Init_Ext`。两者的答案在日志里可以并排读，
// 但**没有任何一处把前者当判据**：不同原因（GPU 架构不够 / OS 版本过低 / 驱动过旧 / feature
// 未实现 / 查询本身失败）在第 6 步失败时退化成同一句 `init ok=0 rc=0x… reason=init`，说不出为什么。
//
// 本文件就是那个判据：只回答「这一局要不要在 `Init_Ext` **之前**收场」。
// 它不认识 D3D、不认识 NGX、不读任何全局 —— 输入是 `ensure_init` 第 5b 步已经算出来的三个
// 观测值，输出是 `Decision`。拒绝动作本身（打日志 / 置既有放手位 / 5 s 退避 / return false）
// 留在后端里，因为那是「既有失败终态」的一部分，必须与 `fail_init` 保持同一处定义。
//
// ============================================================================
// 决策表（**唯一规格**；`NgxFeatureGateTest.cpp` 的期望表照此手抄，不反向调用本文件推期望）
// ============================================================================
//     Refuse  ⟺  proxy == 0  &&  ok == 1  &&  (raw == 4 || raw == 8)   // **精确等于**，不是位与
//     Proceed ⟺  其余全部
//
// 逐条的「为什么」（每行都有对应的单测用例）：
//   · proxy == -1 —— 不知道是谁在回答（入口路径取不到，或 provider kind 是 nvngx_load / unknown）
//                    ⇒ 没有证据就不下结论（AGENTS.md §五.1：判据强度必须匹配动作强度）。
//   · proxy == 1  —— 代理（OptiScaler 一类）会**硬编码**答「支持」，它的答案不是 NGX 的事实；
//                    而代理用户正是靠它兜底，不能因为代理的回答被拒。
//   · ok == 0     —— 查询没成功（无入口 / 取不到适配器 / 非成功码 / 厂商实现 AV）
//                    ⇒ **观测失败 ≠ 能力不足**，两者永不可混为一谈。
//   · raw == 0    —— Supported。
//   · raw == 1    —— CheckNotPresent：语义是「这个接口这次没生效」，不是「不支持」。
//   · raw == 2    —— DriverVersionUnsupported：本批**不拒绝**。驱动维度零样本，属已知未覆盖项
//                    （不是遗漏；依据见 docs/OPEN-ISSUES.md 的 NGX 准入条目）。
//   · raw == 4    —— AdapterUnsupported（GPU 架构不够）⇒ 本批两个拒绝位之一。
//   · raw == 8    —— OSVersionBelowMinimumSupported ⇒ 本批两个拒绝位之一。
//   · raw == 16   —— NotImplemented：环境问题（找不到 feature DLL），不是硬件不支持。
//   · 组合位（12 / 5 / 9 / 3 …）、未知位（32 / 0x100 / …）—— 语义未证，不得据此关功能。
//
// 为什么不加第二道锁（例如要求厂商填的 MinHWArchitecture != 0）：
//   `ensure_init` 里那个出参是 `NVSDK_NGX_FeatureRequirement req {};`（值初始化全部 264 字节），
//   而 `raw` 只在 `req_rc == NVSDK_NGX_Result_Success` 分支被赋值，其余分支保持哨兵 0
//   ⇒ 厂商没写结构体时 `raw` 必然是 0 ⇒ 本文件返回 Proceed。零填充的假答案在**拒绝方向**上
//   不可能伪装成 4/8，它只会表现为 0 而被放行。反过来，`raw == 8`（OS 维度）时厂商实现不保证
//   填 `MinHWArchitecture`，若它合法为 0，多一道锁会让 OS 拒绝**永不触发**。
//   ⇒ 第二道锁结构性不必要，且会引入新的误判面。
//
// ============================================================================
// 形态纪律
// ============================================================================
// · 只 include <cstdint>（与 NgxProviderPick.h 的「零依赖才能在任何机器上跑单测」同一条纪律）。
//   两个函数都是 `inline constexpr`：测试 target 与主 target 各编译一次，头内定义不产生重复符号。
// · 判定位比较**只允许出现在本文件**：`NgxBackend.cpp` 里出现 `raw == 4` / `raw & 4` 一律违规
//   （AGENTS.md §五.2「判据常量单一来源」）。那边的 static_assert 把本文件的两个常量与
//   `NVSDK_NGX_FeatureSupportResult_*` 绑在一起 —— SDK 枚举漂移会在**编译期**报错，而不是静默。
//
// 2026-11-29（#11 阶段 B，L2）：新增。阶段 A（初始化前查询只记日志、不参与判定）已在 b2886d8
//   落地；本文件是阶段 B 的准入判据，由键 `NgxRefuseUnsupported` 控制，**默认 0**
//   ⇒ 默认行为与阶段 A 逐字相同。

#pragma once

#include <cstdint>

namespace ngx_gate
{

enum class Decision
{
    Proceed,   // 继续初始化（默认；绝大多数输入落在这里）
    Refuse,    // 在 Init_Ext 之前收场（仅在 proxy==0 ∧ ok==1 ∧ raw∈{4,8} 时）
};

// 两个拒绝位。与 NVSDK_NGX_FeatureSupportResult_* 的编译期绑定在 NgxBackend.cpp 的 static_assert 里
//   （本文件零依赖，不能 include NGX 头；反过来把 SDK 枚举拉进来就没有「任意机器可跑」的优势了）。
inline constexpr unsigned kAdapterUnsupported = 4u;   // NVSDK_NGX_FeatureSupportResult_AdapterUnsupported
inline constexpr unsigned kOsBelowMin        = 8u;   // NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported

// 纯函数：给定第 5b 步的三个观测值，回答「要不要在 Init_Ext 之前收场」。
//   proxy：1 = 非 _nvngx.dll 在回答 / 0 = 真 NGX / -1 = 取不到（不知道）
//   ok   ：1 = 查询成功（厂商答了）/ 0 = 没答上来
//   raw  ：厂商答的 FeatureSupported 位域（ok != 1 时无意义）
inline constexpr Decision decide(int proxy, int ok, unsigned raw) noexcept
{
    if (proxy != 0)
        return Decision::Proceed;
    if (ok != 1)
        return Decision::Proceed;
    if (raw == kAdapterUnsupported || raw == kOsBelowMin)
        return Decision::Refuse;
    return Decision::Proceed;
}

// 给拒绝日志用的原因串（调用点是 `reason=%hs`）。
// ★ **永不返回 nullptr**：调用点会无条件格式化它，而 `%hs` 配空指针在 MSVC 上不是「安全地打
//   (null)」，是未定义行为；且非 {4,8} 的取值也会走到这里（例如合成触发测 fail-open 时的 raw==0）。
//   故兜底返回 "not_eligible" 而不是 nullptr。
// ★ 返回 `const char *` 而不是宽串 / std::string：`log_ngx_error` 是 `_vsnwprintf_s` 的可变参数
//   接口，MSVC 对实参类型**零检查** ⇒ 类型写错 = 无声乱码日志。
inline constexpr const char *refusal_reason(unsigned raw) noexcept
{
    if (raw == kAdapterUnsupported)
        return "adapter_unsupported";
    if (raw == kOsBelowMin)
        return "os_below_min";
    return "not_eligible";
}

} // namespace ngx_gate
