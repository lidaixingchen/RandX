#ifndef RANDX_TESTS_COMMON_SECURITY_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_SECURITY_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "doctest.h"
#include "fixtures.hpp"

TEST_SUITE("公共/基础/安全")
{
    TEST_CASE("ChaCha20 counter 边界 0xFFFFFFFFU 与生命周期契约")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::array<std::uint8_t, 32> key{};
        std::array<std::uint8_t, 12> nonce{};

        RandX::ChaCha20 chacha_max(key.data(), 32, nonce.data(), 12, 0xFFFFFFFFU);
        for (int i = 0; i < 8; ++i)
        {
            CHECK_NOTHROW((void)chacha_max());
        }
        CHECK_THROWS_AS((void)chacha_max(), std::overflow_error);

        RandX::ChaCha20 chacha_max_minus_1(key.data(), 32, nonce.data(), 12, 0xFFFFFFFEU);
        for (int i = 0; i < 16; ++i)
        {
            CHECK_NOTHROW((void)chacha_max_minus_1());
        }
        CHECK_THROWS_AS((void)chacha_max_minus_1(), std::overflow_error);

    }
    TEST_CASE("ChaCha20 moved-from 状态保护")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ChaCha20 a{ 12345 };
        RandX::ChaCha20 b = std::move(a);
        CHECK_THROWS_AS(a(), std::logic_error);
        CHECK_THROWS_AS(a.discard(1), std::logic_error);
        const std::uint64_t val = b();
        CHECK(val != 0ULL);

    }
    TEST_CASE("ChaCha20 移动语义与拷贝禁用")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(!std::is_copy_constructible_v<RandX::ChaCha20>);
        static_assert(!std::is_copy_assignable_v<RandX::ChaCha20>);
        static_assert(std::is_move_constructible_v<RandX::ChaCha20>);
        static_assert(std::is_move_assignable_v<RandX::ChaCha20>);

        RandX::ChaCha20 rng1(12345ULL);
        RandX::ChaCha20 rng2 = std::move(rng1);
        (void)rng2();

    }
    TEST_CASE("ChaCha20 移动语义与拷贝禁用特性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        CHECK(!std::is_copy_constructible_v<RandX::ChaCha20>);
        CHECK(!std::is_copy_assignable_v<RandX::ChaCha20>);
        CHECK(std::is_move_constructible_v<RandX::ChaCha20>);
        CHECK(std::is_move_assignable_v<RandX::ChaCha20>);

    }
    TEST_CASE("ChaCha20 自移动与移出态连续安全操作")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ChaCha20 a{ 12345 };
        // 自移动赋值安全
        auto& ref = a;
        a = std::move(ref);
        CHECK_NOTHROW((void)a());

        RandX::ChaCha20 b = std::move(a);
        // a 处于 moved-from 状态
        CHECK_THROWS_AS((void)a(), std::logic_error);
        CHECK_THROWS_AS((void)a.discard(5), std::logic_error);

        // 重复移动 moved-from 对象安全
        RandX::ChaCha20 c = std::move(a);
        CHECK_THROWS_AS((void)c(), std::logic_error);

        // 重新赋值 moved-from 对象可复活
        RandX::ChaCha20 revived{ 42 };
        a = std::move(revived);
        CHECK_NOTHROW((void)a());

        // 如果 OS 熵源可用，显式 reseed 可复活 moved-from 对象
        if (RandX::IsOsCryptoEntropyAvailable())
        {
            b.reseed();
            CHECK_NOTHROW((void)b());
        }

    }
    TEST_CASE("ChaCha20 默认构造两个实例序列不同")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        if (RandX::IsOsCryptoEntropyAvailable())
        {
            RandX::ChaCha20 a;
            RandX::ChaCha20 b;
            bool allSame = true;
            for (int i = 0; i < 10; ++i)
            {
                if (a() != b()) allSame = false;
            }
            CHECK_FALSE(allSame);
        }

    }
    TEST_CASE("ChaCha20 默认构造成功初始化（OS 熵可用时）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        if (RandX::IsOsCryptoEntropyAvailable())
        {
            RandX::ChaCha20 rng;
            (void)rng();
        }

    }
    TEST_CASE("IsOsCryptoEntropyAvailable 返回 bool")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 仅验证返回类型与可调用性，不假设具体值（跨平台差异）
        const bool available = RandX::IsOsCryptoEntropyAvailable();
        (void)available;

    }
    TEST_CASE("RFC 8439 §2.3.2 KAT（counter=1，前 8 个 uint64 输出）")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // RFC 8439 §2.3.2 官方测试向量
        // key   = 00:01:02:...:1f（32 字节）
        // nonce = 00:00:00:09:00:00:00:4a:00:00:00:00（12 字节）
        // counter = 1
        // 一个 block = 64 字节 = 8 个 uint64（小端序组装）
        const std::uint8_t key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
        };
        const std::uint8_t nonce[12] = {
            0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
        };
        RandX::ChaCha20 rng(key, 32, nonce, 12, 1);

        // RFC 8439 §2.3.2 输出字节流（64 字节，按小端序组装为 uint64）
        // 经 pycryptodome 交叉验证，与 RFC 8439 官方测试向量一致
        const std::uint64_t expected[8] = {
            0x15593bd1e4e7f110ULL,  // 10 f1 e7 e4 d1 3b 59 15
            0xc47120a31fdd0f50ULL,  // 50 0f dd 1f a3 20 71 c4
            0x0368c033c7f4d1c7ULL,  // c7 d1 f4 c7 33 c0 68 03
            0x4e6cd4c39aaa2204ULL,  // 04 22 aa 9a c3 d4 6c 4e
            0x09aa9f07466482d2ULL,  // d2 82 64 46 07 9f aa 09
            0xa2028bd905d7c214ULL,  // 14 c2 d7 05 d9 8b 02 a2
            0xb94e16ded19c12b5ULL,  // b5 12 9c d1 de 16 4e b9
            0x4e3c50a2e883d0cbULL   // cb d0 83 e8 a2 50 3c 4e
        };
        for (auto e : expected)
            CHECK(rng() == e);

    }
    TEST_CASE("SecureRandomBytes 填充缓冲区")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::array<std::uint8_t, 64> buf{};
        RandX::SecureRandomBytes(buf.data(), buf.size());
        // 极低概率全零（2^-512），视为熵源异常
        bool allZero = true;
        for (auto b : buf) { if (b != 0) { allZero = false; break; } }
        CHECK_FALSE(allZero);

    }
    TEST_CASE("SecureRandomBytes 零长度调用安全完成")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::array<std::uint8_t, 1> buffer{};
        RandX::SecureRandomBytes(buffer.data(), 0);
        RandX::SecureRandomBytes(nullptr, 0);

    }
    TEST_CASE("SecureSeed 返回不同值")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const auto a = RandX::SecureSeed();
        const auto b = RandX::SecureSeed();
        // 极低概率相同（2^-64）
        CHECK(a != b);

    }
    TEST_CASE("discard 与连续调用等价")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ChaCha20 a{ 999 };
        RandX::ChaCha20 b{ 999 };
        a.discard(100);
        for (int i = 0; i < 100; ++i) b();
        CHECK(a() == b());

    }
    TEST_CASE("min/max 与 result_type")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        static_assert(std::is_same_v<RandX::ChaCha20::result_type, std::uint64_t>);
        CHECK(RandX::ChaCha20::min() == 0);
        CHECK(RandX::ChaCha20::max() == UINT64_MAX);

    }
    TEST_CASE("reseed() 手动触发后输出序列改变")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        if (!RandX::IsOsCryptoEntropyAvailable()) return;
        RandX::ChaCha20 a{ 42 };
        RandX::ChaCha20 b{ 42 };
        // reseed 前：a 和 b 输出一致（同种子确定性）
        for (int i = 0; i < 8; ++i) CHECK(a() == b());
        // b 手动 reseed（引入 OS 熵）
        b.reseed();
        // reseed 后：b 输出与 a 不同（概率性断言）
        bool different = false;
        for (int i = 0; i < 16; ++i)
        {
            if (a() != b()) { different = true; break; }
        }
        CHECK(different);

    }
    TEST_CASE("不同种子产生不同序列")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ChaCha20 a{ 1 };
        RandX::ChaCha20 b{ 2 };
        bool allSame = true;
        for (int i = 0; i < 10; ++i)
        {
            if (a() != b()) allSame = false;
        }
        CHECK_FALSE(allSame);

    }
    TEST_CASE("原始输出均匀性 df=127")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 该样本量低于 ChaCha20 自动重播种阈值，保持频率检验的确定性。
        constexpr double EXPECTED = static_cast<double>(RandXTest::TestConstants::kChaCha20ChiSquareSampleCount) / RandXTest::TestConstants::kStatisticalBins128;

        RandX::ChaCha20 rng{ 54321 };
        std::array<int, RandXTest::TestConstants::kStatisticalBins128> counts{};
        for (int i = 0; i < RandXTest::TestConstants::kChaCha20ChiSquareSampleCount; ++i)
            ++counts[rng() & 127];

        double chi2 = 0.0;
        for (int i = 0; i < RandXTest::TestConstants::kStatisticalBins128; ++i)
        {
            const double diff = counts[i] - EXPECTED;
            chi2 += diff * diff / EXPECTED;
        }
        CHECK(chi2 < RandXTest::TestConstants::kChiSquareCriticalDof127);

    }
    TEST_CASE("字节缓存：前 8 次同一 block，第 9 次触发新 block")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 一个 block = 64 字节 = 8 个 uint64
        // 实例 A(counter=0) 的第 9 次输出 = 实例 B(counter=1) 的第 1 次输出
        const std::uint8_t key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
        };
        const std::uint8_t nonce[12] = {
            0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
        };
        RandX::ChaCha20 a(key, 32, nonce, 12, 0);
        // 消费前 8 个 uint64（block 0）
        for (int i = 0; i < 8; ++i) (void)a();
        // 第 9 次输出 = block 1 的第 1 个 uint64
        const std::uint64_t ninth = a();
        // 实例 B 从 counter=1 开始，第 1 次输出应等于 A 的第 9 次
        RandX::ChaCha20 b(key, 32, nonce, 12, 1);
        CHECK(ninth == b());

    }
    TEST_CASE("构造方式 2 确定性复现")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::ChaCha20 a{ 42 };
        RandX::ChaCha20 b{ 42 };
        for (int i = 0; i < 20; ++i)
            CHECK(a() == b());

    }
    TEST_CASE("构造方式 3 异常：key 长度非法")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::uint8_t badKey[16] = { 0 };
        const std::uint8_t nonce[12] = { 0 };
        CHECK_THROWS_AS(RandX::ChaCha20(badKey, 16, nonce, 12), std::invalid_argument);

    }
    TEST_CASE("构造方式 3 异常：nonce 长度非法")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        const std::uint8_t key[32] = { 0 };
        const std::uint8_t badNonce[8] = { 0 };
        CHECK_THROWS_AS(RandX::ChaCha20(key, 32, badNonce, 8), std::invalid_argument);

    }
    TEST_CASE("确定性种子 ChaCha20(seed) 在超过 1MB 后依然保持完全确定性")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        constexpr unsigned long long ReseedCalls = (1ULL << 20) / 8;
        RandX::ChaCha20 a{ 42 };
        RandX::ChaCha20 b{ 42 };
        a.discard(ReseedCalls + 16);
        b.discard(ReseedCalls + 16);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(a() == b());
        }

    }
    TEST_CASE("默认构造函数在 2^20 字节阈值后自动 reseed")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        if (!RandX::IsOsCryptoEntropyAvailable()) return;
        // 默认构造函数启用 m_autoReseed = true
        // ChaCha20ReseedThreshold = 2^20 字节，每次 operator() 输出 8 字节
        // 注意：默认构造由 OS 熵播种，无法固定种子做双实例对比。
        // 本用例验证：① 越过阈值不崩溃 ② reseed 后输出非退化
        constexpr unsigned long long ReseedCalls = (1ULL << 20) / 8;
        RandX::ChaCha20 rng;
        // 越过 reseed 阈值
        rng.discard(ReseedCalls + 8);
        // reseed 后继续产生有效输出
        bool allZero = true;
        bool hasRepeat = true;
        std::uint64_t prev = rng();
        if (prev != 0) allZero = false;
        for (int i = 0; i < 16; ++i)
        {
            std::uint64_t cur = rng();
            if (cur != 0) allZero = false;
            if (cur != prev) hasRepeat = false;
            prev = cur;
        }
        CHECK_FALSE(allZero);
        CHECK_FALSE(hasRepeat);

    }
}

#endif
