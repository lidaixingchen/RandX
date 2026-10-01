#ifndef RANDX_TESTS_COMMON_SERIALIZATION_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_SERIALIZATION_CONTRACTS_HPP

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

TEST_SUITE("公共/基础/序列化")
{
    TEST_CASE("SFC64 文本反序列化成功接受全零状态")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::istringstream iss("0 0 0 0");
        RandX::SFC64 rng(999);
        iss >> rng;
        CHECK(!iss.fail());
        CHECK(rng.serialize() == (RandX::SFC64::state_type{ 0, 0, 0, 0 }));

    }
    TEST_CASE("SFC64 经确定性状态转移演化为全零快照后可精确恢复")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::SFC64 rng(RandX::SFC64::state_type{ 1, 0, 0, UINT64_MAX });
        (void)rng();
        auto snapshot = rng.serialize();

        // 验证演化后的快照确实全为 0
        CHECK(snapshot[0] == 0);
        CHECK(snapshot[1] == 0);
        CHECK(snapshot[2] == 0);
        CHECK(snapshot[3] == 0);

        // 状态构造精确恢复全零，不发生静默改写
        RandX::SFC64 restored(snapshot);
        CHECK(restored.serialize() == snapshot);
        CHECK(restored == rng);

        // deserialize 精确恢复全零
        RandX::SFC64 deser_rng(12345);
        deser_rng.deserialize(snapshot);
        CHECK(deser_rng.serialize() == snapshot);
        CHECK(deser_rng == rng);

        // 后续生成序列一致
        for (int i = 0; i < 10; ++i)
        {
            auto v1 = rng();
            auto v2 = restored();
            auto v3 = deser_rng();
            CHECK(v1 == v2);
            CHECK(v2 == v3);
        }

    }
    TEST_CASE("operator<< / operator>> 流式序列化")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng1{ 12345 };
        // 推进若干步
        for (int i = 0; i < 10; ++i) (void)rng1();

        std::stringstream ss;
        ss << rng1;
        CHECK((ss.good() || ss.eof()));

        RandX::Xoshiro256StarStar rng2{ 99999 };
        ss >> rng2;
        CHECK((ss.good() || ss.eof()));

        // 恢复后两个引擎状态一致，后续输出相同
        CHECK(rng1 == rng2);
        CHECK(rng1() == rng2());

    }
    TEST_CASE("operator>> 失败时设置 failbit 且状态不变")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng1{ 12345 };
        RandX::Xoshiro256StarStar rng2{ 12345 };
        // 推进若干步，使状态非默认
        for (int i = 0; i < 5; ++i) (void)rng1(), (void)rng2();

        std::stringstream ss("not a number");  // 故意错误输入
        ss >> rng1;
        CHECK((ss.failbit & ss.rdstate()) != 0);
        // 状态未改变
        CHECK(rng1 == rng2);

    }
    TEST_CASE("serialize / deserialize 状态恢复")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 99999 };
        rng.discard(10);
        auto state = rng.serialize();
        RandX::Xoshiro256StarStar rng2{ state };
        for (int i = 0; i < 10; ++i)
            CHECK(rng() == rng2());

    }
    TEST_CASE("setfill 与 setw 不产生不可解析状态且格式恢复")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::stringstream ss;
        ss.fill('x');
        ss.width(5);
        ss.setf(std::ios_base::hex, std::ios_base::basefield);
        ss.setf(std::ios_base::showbase);

        RandX::Xoshiro256StarStar rng(12345);
        ss << rng;

        // 检查调用方的 flags 和 fill 恢复
        CHECK(ss.fill() == 'x');
        CHECK((ss.flags() & std::ios_base::basefield) == std::ios_base::hex);
        CHECK((ss.flags() & std::ios_base::showbase));

        // 反序列化成功往返
        RandX::Xoshiro256StarStar restored(1);
        ss >> restored;
        CHECK(!ss.fail());
        CHECK(rng == restored);

        // wstringstream 正常往返
        std::wstringstream wss;
        wss << rng;
        RandX::Xoshiro256StarStar w_restored(1);
        wss >> w_restored;
        CHECK(!wss.fail());
        CHECK(rng == w_restored);

    }
    TEST_CASE("单独字段输入溢出设置 failbit 并保持原状态")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        std::istringstream iss("999999999999999999999999999999999999 0 0 0");
        RandX::Xoshiro256StarStar rng(12345);
        auto orig_state = rng.serialize();

        iss >> rng;
        CHECK(iss.fail());
        CHECK(rng.serialize() == orig_state);

    }
    TEST_CASE("反序列化全零非法状态设置 failbit 并保持原状态，隔离流格式标志")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar rng{ 123456789ULL };
        const auto orig_state = rng.serialize();

        // 1. 全零状态反序列化测试
        std::istringstream bad_is("0 0 0 0");
        bad_is >> rng;
        CHECK(bad_is.fail());
        CHECK(rng.serialize() == orig_state);

        // 2. 带 std::hex 和 std::noskipws 的格式标志隔离测试
        std::ostringstream oss;
        oss << std::hex << rng;
        std::string serialized_str = oss.str();

        std::istringstream iss(serialized_str);
        iss >> std::hex >> std::noskipws >> rng;
        CHECK_FALSE(iss.fail());
        CHECK(rng.serialize() == orig_state);

    }
    TEST_CASE("受控写入失败设置 badbit 并恢复格式")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandXTest::StreamFormatFixtures::FailingBuffer fb;
        std::ostream os(&fb);
        os.fill('#');
        os.setf(std::ios_base::hex, std::ios_base::basefield);

        RandX::Xoshiro256StarStar rng(12345);
        os << rng;

        CHECK(os.bad());
        CHECK(os.fill() == '#');
        CHECK((os.flags() & std::ios_base::basefield) == std::ios_base::hex);

    }
    TEST_CASE("吸收态引擎文本反序列化拒绝全零且旧状态不变")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // Xoshiro256StarStar
        std::istringstream iss_x256("0 0 0 0");
        RandX::Xoshiro256StarStar xrng(12345);
        auto old_x256 = xrng.serialize();
        iss_x256 >> xrng;
        CHECK(iss_x256.fail());
        CHECK(xrng.serialize() == old_x256);

        // RomuDuoJr
        std::istringstream iss_romu("0 0");
        RandX::RomuDuoJr rrng(12345);
        auto old_romu = rrng.serialize();
        iss_romu >> rrng;
        CHECK(iss_romu.fail());
        CHECK(rrng.serialize() == old_romu);

    }
    TEST_CASE("吸收态引擎裸状态入口保留全零修正语义")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar zero_rng(RandX::Xoshiro256StarStar::state_type{ 0, 0, 0, 0 });
        CHECK(zero_rng.serialize()[0] == 1);

        RandX::RomuDuoJr zero_romu(RandX::RomuDuoJr::state_type{ 0, 0 });
        CHECK(zero_romu.serialize()[0] == 1);

    }
    TEST_CASE("外部 setw 掩码不截断反序列化并在退出后恢复原 width")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar orig(98765);
        std::stringstream ss;
        ss << orig;

        RandX::Xoshiro256StarStar restored(1);
        ss.width(2);
        ss >> restored;

        CHECK(!ss.fail());
        CHECK(orig == restored);
        CHECK(ss.width() == 2);

    }
    TEST_CASE("输入失败触发异常掩码且恢复格式保持原状态")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        // 不足字段触发 failbit 异常
        std::istringstream iss("123 456");
        iss.setf(std::ios_base::hex, std::ios_base::basefield);
        iss.unsetf(std::ios_base::skipws);
        iss.fill('*');
        iss.exceptions(std::ios_base::failbit);

        RandX::Xoshiro256StarStar rng(12345);
        auto orig_state = rng.serialize();

        CHECK_THROWS_AS(iss >> rng, std::ios_base::failure);
        CHECK(rng.serialize() == orig_state);
        CHECK(iss.fill() == '*');
        CHECK((iss.flags() & std::ios_base::basefield) == std::ios_base::hex);
        CHECK((iss.rdstate() & std::ios_base::failbit));

    }
    TEST_CASE("连续写入与读取两个引擎")
    {
        RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);
        RandX::Xoshiro256StarStar e1(111);
        RandX::RomuDuoJr e2(222);

        std::stringstream ss;
        ss << e1 << ' ' << e2;

        RandX::Xoshiro256StarStar r1(1);
        RandX::RomuDuoJr r2(1);

        ss >> r1 >> r2;
        CHECK(!ss.fail());
        CHECK(e1 == r1);
        CHECK(e2 == r2);

    }
}

#endif
