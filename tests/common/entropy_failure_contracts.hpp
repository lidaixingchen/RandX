#ifndef RANDX_TESTS_COMMON_ENTROPY_FAILURE_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_ENTROPY_FAILURE_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#include "../fixtures/entropy_sources.hpp"

namespace RandXTest::EntropyFixtures
{
constexpr std::size_t kPublicEntropyTestBytes = sizeof(std::uint64_t);
constexpr std::size_t kPublicPartialPrefixBytes = 5;
constexpr std::uint8_t kOriginalKeyByte = 0x16;
constexpr std::uint8_t kOriginalNonceByte = 0x4B;
constexpr std::uint8_t kInnerSeedMaterialByte = 0xA2;
constexpr std::uint64_t kExplicitChaChaSeed = 0xA1B2C3D4E5F60718ULL;

inline void CheckReaderConsumed(const ScriptedEntropyReader& reader)
{
    CHECK(reader.consumedStepCount() == reader.stepCount());
    CHECK(reader.unexpectedCalls() == kNoCalls);
    CHECK(reader.contractViolations() == kNoCalls);
}

inline RandX::ChaCha20 MakeInitialChaCha20()
{
    std::array<std::uint8_t, kChaCha20KeyBytes> key{};
    std::array<std::uint8_t, kChaCha20NonceBytes> nonce{};
    key.fill(kOriginalKeyByte);
    nonce.fill(kOriginalNonceByte);
    return RandX::ChaCha20(key.data(), key.size(), nonce.data(), nonce.size());
}

inline RandX::ChaCha20 MakeChaCha20FromSeedMaterial(
    const std::array<std::uint8_t, kChaCha20SeedBytes>& seedMaterial)
{
    return RandX::ChaCha20(seedMaterial.data(), kChaCha20KeyBytes,
                           seedMaterial.data() + kChaCha20KeyBytes, kChaCha20NonceBytes);
}

inline std::uint64_t RepeatedByteSeed(std::uint8_t byte) noexcept
{
    std::array<std::uint8_t, sizeof(std::uint64_t)> bytes{};
    bytes.fill(byte);
    std::uint64_t value{};
    std::memcpy(&value, bytes.data(), bytes.size());
    return value;
}
}

TEST_SUITE("公共/基础/安全熵源故障")
{
    TEST_CASE("安全字节和种子只在完整成功后返回结果")
    {
        using namespace RandXTest::EntropyFixtures;
        const bool nativeCapability = RandX::IsOsCryptoEntropyAvailable();
        ScriptedEntropyReader reader;
        reader.SetMaterialByte(kZeroEntropyByte);
        EntropyHookGuard hook(reader);

        RandX::SecureRandomBytes(nullptr, kNoCalls);
        CHECK(reader.hookFillCalls() == kNoCalls);
        CHECK(reader.callCount() == kNoCalls);

        reader.AddProgress();
        std::array<std::uint8_t, kPublicEntropyTestBytes> bytes{};
        bytes.fill(kUnwrittenByte);
        RandX::SecureRandomBytes(bytes.data(), bytes.size());
        CHECK(AllBytesEqual(bytes, kZeroEntropyByte));
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        reader.SetMaterialByte(kZeroEntropyByte);
        reader.AddProgress();
        CHECK(RandX::SecureSeed() == kZeroSeedValue);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        reader.AddFailure();
        bytes.fill(kUnwrittenByte);
        CHECK_THROWS_AS(RandX::SecureRandomBytes(bytes.data(), bytes.size()), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CHECK(reader.observation(kFirstCallIndex).status == RandX::detail::EntropyReadStatus::Failure);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        const auto expectedPartialBytes = MakeMaterial<kPublicEntropyTestBytes>();
        reader.SetMaterial(expectedPartialBytes.data(), expectedPartialBytes.size());
        reader.AddProgress(kPublicPartialPrefixBytes);
        reader.AddFailure();
        bytes.fill(kUnwrittenByte);
        CHECK_THROWS_AS(RandX::SecureRandomBytes(bytes.data(), bytes.size()), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        REQUIRE(reader.callCount() == kTwoCalls);
        CHECK(reader.observation(kFirstCallIndex).writtenBytes == kPublicPartialPrefixBytes);
        CHECK(reader.observation(kSecondCallIndex).outputOffset == kPublicPartialPrefixBytes);
        CHECK(std::equal(bytes.begin(), bytes.begin() + kPublicPartialPrefixBytes,
                         expectedPartialBytes.begin()));
        CHECK(std::all_of(bytes.begin() + kPublicPartialPrefixBytes, bytes.end(),
                          [](std::uint8_t byte) { return byte == kUnwrittenByte; }));
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        reader.AddFailure();
        CHECK_THROWS_AS((void)RandX::SecureSeed(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
        CHECK(RandX::IsOsCryptoEntropyAvailable() == nativeCapability);
    }

    TEST_CASE("默认构造传播原生熵失败而显式种子构造不读取 OS")
    {
        using namespace RandXTest::EntropyFixtures;
        ScriptedEntropyReader reader;
        reader.AddFailure();
        EntropyHookGuard hook(reader);

        CHECK_THROWS_AS((RandX::ChaCha20{}), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        RandX::ChaCha20 seeded(kExplicitChaChaSeed);
        std::array<std::uint8_t, kChaCha20KeyBytes> key{};
        std::array<std::uint8_t, kChaCha20NonceBytes> nonce{};
        key.fill(kOriginalKeyByte);
        nonce.fill(kOriginalNonceByte);
        RandX::ChaCha20 explicitMaterial(key.data(), key.size(), nonce.data(), nonce.size());
        CHECK(seeded() == RandX::ChaCha20(kExplicitChaChaSeed)());
        CHECK(explicitMaterial() == MakeInitialChaCha20()());
        CHECK(reader.hookFillCalls() == kNoCalls);
        CHECK(reader.callCount() == kNoCalls);
        CHECK(reader.unexpectedCalls() == kNoCalls);
    }

    TEST_CASE("正常流的重播种失败保留状态并在恢复后切换完整密钥流")
    {
        using namespace RandXTest::EntropyFixtures;
        ScriptedEntropyReader reader;
        EntropyHookGuard hook(reader);
        RandX::ChaCha20 engine = MakeInitialChaCha20();
        RandX::ChaCha20 control = MakeInitialChaCha20();

        CHECK(engine() == control());
        reader.AddFailure();
        CHECK_THROWS_AS(engine.reseed(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
        CHECK(engine() == control());

        engine.discard(kChaCha20BlockOutputCount - kOneCall);
        control.discard(kChaCha20BlockOutputCount - kOneCall);
        reader.ClearPlan();
        reader.AddProgress(kPublicPartialPrefixBytes);
        reader.AddFailure();
        CHECK_THROWS_AS(engine.reseed(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        REQUIRE(reader.callCount() == kTwoCalls);
        CHECK(reader.observation(kFirstCallIndex).writtenBytes == kPublicPartialPrefixBytes);
        CHECK(reader.observation(kSecondCallIndex).outputOffset == kPublicPartialPrefixBytes);
        CheckReaderConsumed(reader);
        CHECK(engine() == control());

        const auto newSeed = MakeMaterial<kChaCha20SeedBytes>(kInnerMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(newSeed.data(), newSeed.size());
        reader.AddProgress();
        engine.reseed();
        RandX::ChaCha20 recovered = MakeChaCha20FromSeedMaterial(newSeed);
        CHECK(engine() == recovered());
        CHECK(engine() == recovered());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
    }

    TEST_CASE("缓存耗尽时重播种失败保留跨块连续性并支持恢复")
    {
        using namespace RandXTest::EntropyFixtures;
        constexpr std::size_t kOutputsAcrossBlockBoundary = kChaCha20BlockOutputCount + kOneCall;

        ScriptedEntropyReader reader;
        EntropyHookGuard hook(reader);
        RandX::ChaCha20 engine = MakeInitialChaCha20();
        RandX::ChaCha20 control = MakeInitialChaCha20();

        for (std::size_t index = 0; index < kChaCha20BlockOutputCount; ++index)
            CHECK(engine() == control());
        reader.AddFailure();
        CHECK_THROWS_AS(engine.reseed(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        for (std::size_t index = 0; index < kOutputsAcrossBlockBoundary; ++index)
            CHECK(engine() == control());
        for (std::size_t index = 0; index < kChaCha20BlockOutputCount - kOneCall; ++index)
            CHECK(engine() == control());

        reader.ClearPlan();
        reader.AddProgress(kPublicPartialPrefixBytes);
        reader.AddFailure();
        CHECK_THROWS_AS(engine.reseed(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        REQUIRE(reader.callCount() == kTwoCalls);
        CHECK(reader.observation(kFirstCallIndex).writtenBytes == kPublicPartialPrefixBytes);
        CHECK(reader.observation(kSecondCallIndex).outputOffset == kPublicPartialPrefixBytes);
        CheckReaderConsumed(reader);

        for (std::size_t index = 0; index < kOutputsAcrossBlockBoundary; ++index)
            CHECK(engine() == control());

        const auto newSeed = MakeMaterial<kChaCha20SeedBytes>(kInnerSeedMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(newSeed.data(), newSeed.size());
        reader.AddProgress();
        engine.reseed();
        RandX::ChaCha20 recovered = MakeChaCha20FromSeedMaterial(newSeed);
        for (std::size_t index = 0; index < kOutputsAcrossBlockBoundary; ++index)
            CHECK(engine() == recovered());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
    }

    TEST_CASE("计数器耗尽对象在首次失败和部分失败后仍保持耗尽并能恢复")
    {
        using namespace RandXTest::EntropyFixtures;
        std::array<std::uint8_t, kChaCha20KeyBytes> key{};
        std::array<std::uint8_t, kChaCha20NonceBytes> nonce{};
        key.fill(kOriginalKeyByte);
        nonce.fill(kOriginalNonceByte);
        RandX::ChaCha20 exhausted(
            key.data(), key.size(), nonce.data(), nonce.size(),
            (std::numeric_limits<std::uint32_t>::max)());
        for (std::size_t index = 0; index < kChaCha20BlockOutputCount; ++index)
            (void)exhausted();

        ScriptedEntropyReader reader;
        EntropyHookGuard hook(reader);
        reader.AddFailure();
        CHECK_THROWS_AS(exhausted.reseed(), std::runtime_error);
        CHECK_THROWS_AS((void)exhausted(), std::overflow_error);
        CHECK_THROWS_AS(exhausted.discard(kOneOutputDiscard), std::overflow_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        reader.AddProgress(kPublicPartialPrefixBytes);
        reader.AddFailure();
        CHECK_THROWS_AS(exhausted.reseed(), std::runtime_error);
        CHECK_THROWS_AS((void)exhausted(), std::overflow_error);
        CHECK_THROWS_AS(exhausted.discard(kOneOutputDiscard), std::overflow_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        REQUIRE(reader.callCount() == kTwoCalls);
        CHECK(reader.observation(kFirstCallIndex).writtenBytes == kPublicPartialPrefixBytes);
        CHECK(reader.observation(kSecondCallIndex).outputOffset == kPublicPartialPrefixBytes);
        CheckReaderConsumed(reader);

        const auto newSeed = MakeMaterial<kChaCha20SeedBytes>(kThreadMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(newSeed.data(), newSeed.size());
        reader.AddProgress();
        exhausted.reseed();
        RandX::ChaCha20 control = MakeChaCha20FromSeedMaterial(newSeed);
        CHECK(exhausted() == control());
        CHECK(exhausted() == control());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
    }

    TEST_CASE("移出对象在首次失败和部分失败后保持移出状态并能恢复")
    {
        using namespace RandXTest::EntropyFixtures;
        RandX::ChaCha20 source(kExplicitChaChaSeed);
        RandX::ChaCha20 destination(std::move(source));
        (void)destination;

        ScriptedEntropyReader reader;
        EntropyHookGuard hook(reader);
        reader.AddFailure();
        CHECK_THROWS_AS(source.reseed(), std::runtime_error);
        CHECK_THROWS_AS((void)source(), std::logic_error);
        CHECK_THROWS_AS(source.discard(kOneOutputDiscard), std::logic_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        reader.AddProgress(kPublicPartialPrefixBytes);
        reader.AddFailure();
        CHECK_THROWS_AS(source.reseed(), std::runtime_error);
        CHECK_THROWS_AS((void)source(), std::logic_error);
        CHECK_THROWS_AS(source.discard(kOneOutputDiscard), std::logic_error);
        CHECK(reader.hookFillCalls() == kOneCall);
        REQUIRE(reader.callCount() == kTwoCalls);
        CHECK(reader.observation(kFirstCallIndex).writtenBytes == kPublicPartialPrefixBytes);
        CHECK(reader.observation(kSecondCallIndex).outputOffset == kPublicPartialPrefixBytes);
        CheckReaderConsumed(reader);

        const auto newSeed = MakeMaterial<kChaCha20SeedBytes>(kOuterMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(newSeed.data(), newSeed.size());
        reader.AddProgress();
        source.reseed();
        RandX::ChaCha20 control = MakeChaCha20FromSeedMaterial(newSeed);
        CHECK(source() == control());
        source.discard(kTwoOutputDiscard);
        control.discard(kTwoOutputDiscard);
        CHECK(source() == control());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
    }

    TEST_CASE("默认构造自动重播种使用真实阈值并在重复失败后恢复")
    {
        using namespace RandXTest::EntropyFixtures;
        constexpr std::size_t kOutputsToReseed = static_cast<std::size_t>(
            RandX::detail::ChaCha20ReseedThreshold / sizeof(RandX::ChaCha20::result_type));
        static_assert(RandX::detail::ChaCha20ReseedThreshold % sizeof(RandX::ChaCha20::result_type) == 0);

        ScriptedEntropyReader reader;
        const auto initialSeed = MakeMaterial<kChaCha20SeedBytes>(kOuterMaterialByte);
        reader.SetMaterial(initialSeed.data(), initialSeed.size());
        reader.AddProgress();
        reader.AddFailure();
        reader.AddFailure();
        EntropyHookGuard hook(reader);
        RandX::ChaCha20 automatic;
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        for (std::size_t index = 0; index < kOutputsToReseed; ++index)
            (void)automatic();
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CHECK_THROWS_AS((void)automatic(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kTwoCalls);
        CHECK(reader.callCount() == kTwoCalls);
        CHECK_THROWS_AS((void)automatic(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kThreeCalls);
        CHECK(reader.callCount() == kThreeCalls);
        CheckReaderConsumed(reader);

        const auto recoveredSeed = MakeMaterial<kChaCha20SeedBytes>(kThreadMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(recoveredSeed.data(), recoveredSeed.size());
        reader.AddProgress();
        RandX::ChaCha20 recovered = MakeChaCha20FromSeedMaterial(recoveredSeed);
        CHECK(automatic() == recovered());
        CHECK(automatic() == recovered());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);

        reader.ClearPlan();
        RandX::ChaCha20 deterministic(kExplicitChaChaSeed);
        deterministic.discard(static_cast<unsigned long long>(kOutputsToReseed + kOneCall));
        CHECK(reader.hookFillCalls() == kNoCalls);
        CHECK(reader.callCount() == kNoCalls);
        CHECK(reader.unexpectedCalls() == kNoCalls);
    }

    TEST_CASE("跨阈值 discard 传播失败并在熵源恢复后重新开始")
    {
        using namespace RandXTest::EntropyFixtures;
        constexpr std::size_t kOutputsToReseed = static_cast<std::size_t>(
            RandX::detail::ChaCha20ReseedThreshold / sizeof(RandX::ChaCha20::result_type));

        ScriptedEntropyReader reader;
        const auto initialSeed = MakeMaterial<kChaCha20SeedBytes>(kInnerMaterialByte);
        reader.SetMaterial(initialSeed.data(), initialSeed.size());
        reader.AddProgress();
        reader.AddFailure();
        reader.AddFailure();
        EntropyHookGuard hook(reader);
        RandX::ChaCha20 automatic;
        for (std::size_t index = 0; index + kOneCall < kOutputsToReseed; ++index)
            (void)automatic();
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK_THROWS_AS(automatic.discard(kTwoOutputDiscard), std::runtime_error);
        CHECK(reader.hookFillCalls() == kTwoCalls);
        CHECK(reader.callCount() == kTwoCalls);
        CHECK(reader.unexpectedCalls() == kNoCalls);
        CHECK_THROWS_AS((void)automatic(), std::runtime_error);
        CHECK(reader.hookFillCalls() == kThreeCalls);
        CHECK(reader.callCount() == kThreeCalls);
        CheckReaderConsumed(reader);

        const auto recoveredSeed = MakeMaterial<kChaCha20SeedBytes>(kOuterMaterialByte);
        reader.ClearPlan();
        reader.SetMaterial(recoveredSeed.data(), recoveredSeed.size());
        reader.AddProgress();
        RandX::ChaCha20 recovered = MakeChaCha20FromSeedMaterial(recoveredSeed);
        CHECK(automatic() == recovered());
        CHECK(automatic() == recovered());
        CHECK(reader.hookFillCalls() == kOneCall);
        CHECK(reader.callCount() == kOneCall);
        CheckReaderConsumed(reader);
    }

    TEST_CASE("同一线程跨翻译单元共享 hook 并在嵌套和异常后恢复")
    {
        using namespace RandXTest::EntropyFixtures;
        const bool nativeCapability = RandX::IsOsCryptoEntropyAvailable();
        ScriptedEntropyReader outerReader;
        outerReader.SetMaterialByte(kOuterMaterialByte);
        outerReader.AddProgress();
        outerReader.AddProgress();
        outerReader.AddProgress();
        outerReader.AddProgress();
        EntropyHookGuard outerHook(outerReader);

        std::array<std::uint8_t, kPublicEntropyTestBytes> outerBytes{};
        EntropyCrossTranslationUnitSecureRandomBytes(outerBytes.data(), outerBytes.size());
        CHECK(AllBytesEqual(outerBytes, kOuterMaterialByte));

        ScriptedEntropyReader innerReader;
        innerReader.SetMaterialByte(kInnerSeedMaterialByte);
        innerReader.AddProgress();
        {
            EntropyHookGuard innerHook(innerReader);
            CHECK(EntropyCrossTranslationUnitSecureSeed() == RepeatedByteSeed(kInnerSeedMaterialByte));
        }

        EntropyCrossTranslationUnitSecureRandomBytes(outerBytes.data(), outerBytes.size());
        CHECK(AllBytesEqual(outerBytes, kOuterMaterialByte));

        ScriptedEntropyReader failingReader;
        failingReader.AddFailure();
        {
            EntropyHookGuard failingHook(failingReader);
            CHECK_THROWS_AS((void)EntropyCrossTranslationUnitSecureSeed(), std::runtime_error);
        }
        EntropyCrossTranslationUnitSecureRandomBytes(outerBytes.data(), outerBytes.size());
        CHECK(AllBytesEqual(outerBytes, kOuterMaterialByte));

        ScriptedEntropyReader exceptionalReader;
        try
        {
            EntropyHookGuard exceptionalHook(exceptionalReader);
            throw std::runtime_error("nested hook unwind");
        }
        catch (const std::runtime_error&)
        {
        }
        EntropyCrossTranslationUnitSecureRandomBytes(outerBytes.data(), outerBytes.size());
        CHECK(AllBytesEqual(outerBytes, kOuterMaterialByte));

        CHECK(outerReader.hookFillCalls() == kFourCalls);
        CHECK(outerReader.callCount() == kFourCalls);
        CheckReaderConsumed(outerReader);
        CHECK(innerReader.hookFillCalls() == kOneCall);
        CHECK(innerReader.callCount() == kOneCall);
        CheckReaderConsumed(innerReader);
        CHECK(failingReader.hookFillCalls() == kOneCall);
        CHECK(failingReader.callCount() == kOneCall);
        CheckReaderConsumed(failingReader);
        CHECK(exceptionalReader.hookFillCalls() == kNoCalls);
        CHECK(exceptionalReader.callCount() == kNoCalls);
        CHECK(exceptionalReader.unexpectedCalls() == kNoCalls);
        CHECK(exceptionalReader.contractViolations() == kNoCalls);
        CHECK(RandX::IsOsCryptoEntropyAvailable() == nativeCapability);
    }

    TEST_CASE("两个线程通过跨翻译单元调用各自的熵脚本")
    {
        using namespace RandXTest::EntropyFixtures;
        ScriptedEntropyReader mainReader;
        mainReader.SetMaterialByte(kOuterMaterialByte);
        mainReader.AddProgress();
        EntropyHookGuard mainHook(mainReader);

        std::promise<void> workerHookInstalledPromise;
        std::future<void> workerHookInstalled = workerHookInstalledPromise.get_future();
        std::promise<void> mainCallCompletedPromise;
        std::future<void> mainCallCompleted = mainCallCompletedPromise.get_future();

        std::array<std::uint8_t, kPublicEntropyTestBytes> threadBytes{};
        bool threadSucceeded = false;
        std::size_t threadFillCalls = kNoCalls;
        std::size_t threadReadCalls = kNoCalls;
        std::size_t threadConsumedSteps = kNoCalls;
        std::size_t threadStepCount = kNoCalls;
        std::size_t threadMaterialBytesWritten = kNoCalls;
        std::size_t threadUnexpectedCalls = kNoCalls;
        std::size_t threadContractViolations = kNoCalls;
        std::thread worker([&]()
        {
            ScriptedEntropyReader workerReader;
            workerReader.SetMaterialByte(kThreadMaterialByte);
            workerReader.AddProgress();
            EntropyHookGuard workerHook(workerReader);
            workerHookInstalledPromise.set_value();
            mainCallCompleted.wait();
            try
            {
                EntropyCrossTranslationUnitSecureRandomBytes(threadBytes.data(), threadBytes.size());
                threadSucceeded = true;
            }
            catch (...)
            {
                threadSucceeded = false;
            }
            threadFillCalls = workerReader.hookFillCalls();
            threadReadCalls = workerReader.callCount();
            threadConsumedSteps = workerReader.consumedStepCount();
            threadStepCount = workerReader.stepCount();
            threadMaterialBytesWritten = workerReader.materialBytesWritten();
            threadUnexpectedCalls = workerReader.unexpectedCalls();
            threadContractViolations = workerReader.contractViolations();
        });
        workerHookInstalled.wait();

        std::array<std::uint8_t, kPublicEntropyTestBytes> mainBytes{};
        bool mainSucceeded = false;
        try
        {
            EntropyCrossTranslationUnitSecureRandomBytes(mainBytes.data(), mainBytes.size());
            mainSucceeded = true;
        }
        catch (...)
        {
            mainSucceeded = false;
        }
        mainCallCompletedPromise.set_value();
        worker.join();

        CHECK(mainSucceeded);
        CHECK(threadSucceeded);
        CHECK(AllBytesEqual(threadBytes, kThreadMaterialByte));
        CHECK(AllBytesEqual(mainBytes, kOuterMaterialByte));
        CHECK(threadBytes != mainBytes);
        CHECK(threadFillCalls == kOneCall);
        CHECK(threadReadCalls == kOneCall);
        CHECK(threadConsumedSteps == threadStepCount);
        CHECK(threadMaterialBytesWritten == threadBytes.size());
        CHECK(threadUnexpectedCalls == kNoCalls);
        CHECK(threadContractViolations == kNoCalls);
        CHECK(mainReader.hookFillCalls() == kOneCall);
        CHECK(mainReader.callCount() == kOneCall);
        CHECK(mainReader.materialBytesWritten() == mainBytes.size());
        CheckReaderConsumed(mainReader);
    }
}

#endif
