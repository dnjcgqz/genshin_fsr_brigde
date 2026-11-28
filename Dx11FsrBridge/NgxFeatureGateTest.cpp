// NgxFeatureGateTest.cpp —— `NgxFeatureGate` 的零依赖穷举单测（#11 阶段 B）。
//
// 为什么要有它：这条判据决定「玩家会不会在超分初始化之前被收场」，而真实触发条件
// （厂商答 raw∈{4,8}）在本机**制造不出来**（只有一块 RTX 4070，raw 恒为 0）
// ⇒ 「实现 == 决策表」这一半只能在单测里钉死。本 target 不需要 GPU / 游戏 / NGX 头 / 注册表。
//
// ★ oracle 独立性（口径同本目录 CMakeLists.txt 对 NgxParamMapTest 的约定）：
//   下面的期望表是照 NgxFeatureGate.h 的决策表**手抄**的（只有两格白名单），
//   **不调用 decide() 反推期望**。若某天 decide() 改了而手抄表没跟着改，本测试会红 —— 那正是它的用途。
//
// 断言风格与 NgxProviderPickTest.cpp 一致（自计数 + 失败行），不引第三方框架。

#include "NgxFeatureGate.h"

#include <cstdio>
#include <cstring>

using namespace ngx_gate;

namespace
{
int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char *what, int line)
{
    ++g_checks;
    if (!cond)
    {
        ++g_failures;
        std::printf("FAIL [line %d] %s\n", line, what);
    }
}

// 决策表的手抄形态：**只有两格**是 Refuse —— (proxy,ok,raw) = (0,1,4) 与 (0,1,8)。
// 写成白名单 switch 而不是位判定，是为了让「精确等于、不是位与」这条要求在测试里有独立表述：
// 若实现被误改成 `raw & 4`，12 会开始被拒，而本表照样只认 4 / 8 ⇒ 立刻红。
bool expected_refuse(int proxy, int ok, unsigned raw)
{
    if (proxy != 0 || ok != 1)
        return false;
    switch (raw)
    {
    case 4u:   // AdapterUnsupported
    case 8u:   // OSVersionBelowMinimumSupported
        return true;
    default:
        return false;
    }
}

// 同样手抄：只有 4 / 8 有专名，其余任何取值（含非拒绝路径上的 0 / 16 / 大值）一律 not_eligible。
const char *expected_reason(unsigned raw)
{
    if (raw == 4u)
        return "adapter_unsupported";
    if (raw == 8u)
        return "os_below_min";
    return "not_eligible";
}

// 单格断言：把实际 Decision 与手抄期望对照。失败时把三个输入一起打出来 ——
//   穷举里出现失败而没有输入，等于没法定位（§三 #24：先证明输入到位，再下结论）。
void check_cell(int proxy, int ok, unsigned raw)
{
    ++g_checks;
    const bool want = expected_refuse(proxy, ok, raw);
    const bool got  = (decide(proxy, ok, raw) == Decision::Refuse);
    if (got != want)
    {
        ++g_failures;
        std::printf("FAIL [decide] proxy=%d ok=%d raw=0x%X want_refuse=%d got_refuse=%d\n", proxy, ok,
                    raw, want ? 1 : 0, got ? 1 : 0);
    }
}

// refusal_reason 的单点断言：三个失败形态分开报（nullptr / 空串 / 名字不对），
//   因为调用点 `reason=%hs` 对 nullptr 是未定义行为，与「名字写错」不是同一类缺陷。
void check_reason(unsigned raw)
{
    ++g_checks;
    const char *s    = refusal_reason(raw);
    const char *want = expected_reason(raw);
    if (s == nullptr)
    {
        ++g_failures;
        std::printf("FAIL [reason] raw=0x%X returned nullptr (want \"%s\")\n", raw, want);
        return;
    }
    if (s[0] == '\0')
    {
        ++g_failures;
        std::printf("FAIL [reason] raw=0x%X returned empty string (want \"%s\")\n", raw, want);
        return;
    }
    if (std::strcmp(s, want) != 0)
    {
        ++g_failures;
        std::printf("FAIL [reason] raw=0x%X got \"%s\" want \"%s\"\n", raw, s, want);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __LINE__)
} // namespace

// ---- 判据常量：与 NGX SDK 枚举的编译期绑定在 NgxBackend.cpp 的 static_assert 里 ----
static void test_constants()
{
    CHECK_EQ(kAdapterUnsupported, 4u);
    CHECK_EQ(kOsBelowMin, 8u);
}

// ---- 决策表逐行：决策表里的**每一行**都要有对应用例 ----
static void test_decision_table_rows()
{
    // proxy == -1：不知道谁在回答 ⇒ 无证据不下结论
    CHECK(decide(-1, 1, 4u) == Decision::Proceed);
    CHECK(decide(-1, 1, 8u) == Decision::Proceed);
    // proxy == 1：代理硬编码答「支持」，它的答案不是 NGX 的事实 ⇒ 放行（代理用户靠它兜底）
    CHECK(decide(1, 1, 4u) == Decision::Proceed);
    CHECK(decide(1, 1, 8u) == Decision::Proceed);
    // ok == 0：观测失败 ≠ 能力不足
    CHECK(decide(0, 0, 4u) == Decision::Proceed);
    CHECK(decide(0, 0, 8u) == Decision::Proceed);
    // raw == 0：Supported
    CHECK(decide(0, 1, 0u) == Decision::Proceed);
    // raw == 1：CheckNotPresent（「这个接口这次没生效」，不是「不支持」）
    CHECK(decide(0, 1, 1u) == Decision::Proceed);
    // raw == 2：DriverVersionUnsupported —— 本批不拒绝（驱动维度零样本，属已知未覆盖项）
    CHECK(decide(0, 1, 2u) == Decision::Proceed);
    // raw == 4 / 8：本批唯一的两个拒绝位
    CHECK(decide(0, 1, 4u) == Decision::Refuse);
    CHECK(decide(0, 1, 8u) == Decision::Refuse);
    // raw == 16：NotImplemented（环境问题，不是硬件不支持）
    CHECK(decide(0, 1, 16u) == Decision::Proceed);
    // 组合位：语义未证 ⇒ 一律放行（4|8=12 是最接近「同时不支持」的组合，也不得拒）
    CHECK(decide(0, 1, 12u) == Decision::Proceed);
    CHECK(decide(0, 1, 5u) == Decision::Proceed);
    CHECK(decide(0, 1, 9u) == Decision::Proceed);
    CHECK(decide(0, 1, 3u) == Decision::Proceed);
    // 未知位：含义未知 ⇒ 放行
    CHECK(decide(0, 1, 32u) == Decision::Proceed);
    CHECK(decide(0, 1, 0x100u) == Decision::Proceed);
    CHECK(decide(0, 1, 0xFFFFFFFFu) == Decision::Proceed);
}

// ---- 边界三例：钉住「非 0 一律放行」与「只认 ok==1」这两条 ----
//   proxy 的**生产取值集合**是 {-1,0,1}。±2 是故意越界的探针：它证明实现判的是
//   `proxy == 0`（等号），而不是 `proxy != 1`（「不是代理就算真 NGX」）那种错误写法。
static void test_boundaries()
{
    check(decide(2, 1, 4u) == Decision::Proceed, "proxy=+2 不得被当成真 NGX", __LINE__);
    check(decide(2, 1, 8u) == Decision::Proceed, "proxy=+2 不得被当成真 NGX", __LINE__);
    check(decide(-2, 1, 4u) == Decision::Proceed, "proxy=-2 不得被当成真 NGX", __LINE__);
    check(decide(-2, 1, 8u) == Decision::Proceed, "proxy=-2 不得被当成真 NGX", __LINE__);
    // ok 的**生产取值集合**是 {0,1}。2 是越界探针：证明判的是 `ok == 1`，不是 `ok != 0`。
    check(decide(0, 2, 4u) == Decision::Proceed, "ok=2 不是成功码，不得拒绝", __LINE__);
    check(decide(0, 2, 8u) == Decision::Proceed, "ok=2 不是成功码，不得拒绝", __LINE__);
}

// ---- 决策表穷举：proxy ∈ {-2..2} × ok ∈ {0,1,2} × raw ∈ {0..63} ----
//   raw 取满 0..63：0 / 1 / 2 / 4 / 8 / 16 / 32 与六位以内**全部**组合位（12 / 5 / 9 / 3 都在其中）。
static void test_decide_exhaustive()
{
    static const int kProxies[] = {-2, -1, 0, 1, 2};
    static const int kOks[]     = {0, 1, 2};
    for (int proxy : kProxies)
        for (int ok : kOks)
            for (unsigned raw = 0; raw <= 63u; ++raw)
                check_cell(proxy, ok, raw);
}

// ---- 大值 / 未知位：除精确的 4 / 8 外一律放行 ----
static void test_decide_large_values()
{
    static const unsigned kRaws[] = {16u, 32u, 64u, 0x100u, 0x80000000u, 0xFFFFFFFFu};
    for (unsigned raw : kRaws)
    {
        check_cell(0, 1, raw);
        check_cell(0, 0, raw);
        check_cell(1, 1, raw);
        check_cell(-1, 1, raw);
    }
}

// ---- refusal_reason：永不返回 nullptr / 空串，且只有 4 / 8 两个专名 ----
static void test_refusal_reason()
{
    CHECK(std::strcmp(refusal_reason(4u), "adapter_unsupported") == 0);
    CHECK(std::strcmp(refusal_reason(8u), "os_below_min") == 0);
    // 穷举 0..63 + 大值：每个取值都必须「非空 + 等于手抄表」。
    //   ★ 非空这条不是形式主义：调用点 `reason=%hs` 直接格式化，nullptr 在 MSVC 的可变参数里
    //     不是「安全地打 (null)」。而**非** {4,8} 的取值确实会被调用 —— 合成触发测 fail-open
    //     时 raw==0 也会走到 refusal_reason。
    for (unsigned raw = 0; raw <= 63u; ++raw)
        check_reason(raw);
    static const unsigned kRaws[] = {16u, 32u, 0x100u, 0xFFFFFFFFu};
    for (unsigned raw : kRaws)
        check_reason(raw);
}

int main()
{
    test_constants();
    test_decision_table_rows();
    test_boundaries();
    test_decide_exhaustive();
    test_decide_large_values();
    test_refusal_reason();

    std::printf("NgxFeatureGateTest: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
