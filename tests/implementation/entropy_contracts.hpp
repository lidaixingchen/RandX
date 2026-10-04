#ifndef RANDX_TESTS_IMPLEMENTATION_ENTROPY_CONTRACTS_HPP
#define RANDX_TESTS_IMPLEMENTATION_ENTROPY_CONTRACTS_HPP

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../fixtures/entropy_sources.hpp"

namespace RandXTest::EntropyFixtures
{
constexpr std::size_t kEntropyFillTestBytes = 10;
constexpr std::size_t kEntropyChunkLimit = 4;
constexpr std::size_t kShortReadFirstBytes = 3;
constexpr std::size_t kShortReadSecondBytes = 2;
constexpr std::size_t kEntropyChunkSecondBytes = 2;
constexpr std::size_t kEntropyChunkFailurePrefix = kEntropyChunkLimit + kEntropyChunkSecondBytes;
#if defined(__linux__) && __has_include(<sys/random.h>)
constexpr ssize_t kNativeInterruptedResult = -1;
constexpr ssize_t kNativeFailureResult = -1;
constexpr int kNativeNonInterruptedError = EIO;
#endif
}

TEST_SUITE("内部/熵源")
{
    TEST_CASE("新线程空容器抽样保持 OS 熵读取计数")
    {
        using namespace RandXTest::EntropyFixtures;
        struct Observation
        {
            bool empty;
            std::size_t readsBeforeSeed;
            std::size_t readsAfterSeed;
            std::size_t unexpectedCalls;
        };
        const auto observe = []
        {
            ScriptedEntropyReader reader;
            reader.AddProgress();
            EntropyHookGuard hook(reader);
            const std::vector<int> population;
            constexpr std::size_t request = 1;
            const bool empty = RandX::RandSample(population, request).empty();
            const auto readsBeforeSeed = reader.hookFillCalls();
            (void)RandX::SecureSeed();
            return Observation{empty, readsBeforeSeed,
                               reader.hookFillCalls(), reader.unexpectedCalls()};
        };
        std::promise<Observation> result;
        auto ready = result.get_future();
        std::thread worker([&]
        {
            try
            {
                result.set_value(observe());
            }
            catch (...)
            {
                result.set_exception(std::current_exception());
            }
        });
        worker.join();
        const auto observation = ready.get();
        CHECK(observation.empty);
        CHECK(observation.readsBeforeSeed == kNoCalls);
        CHECK(observation.readsAfterSeed == kOneCall);
        CHECK(observation.unexpectedCalls == kNoCalls);
    }

    TEST_CASE("FillOsEntropy 处理零长度全零短读中断和失败")
    {
        using namespace RandXTest::EntropyFixtures;
        using Status = RandX::detail::EntropyReadStatus;

        ScriptedEntropyReader zeroLengthReader;
        std::array<std::uint8_t, kEntropyFillTestBytes> untouched{};
        untouched.fill(kUnwrittenByte);
        CHECK(RandX::detail::FillOsEntropy(zeroLengthReader, untouched.data(), kNoCalls));
        CHECK(zeroLengthReader.callCount() == kNoCalls);
        CHECK(zeroLengthReader.unexpectedCalls() == kNoCalls);
        CHECK(zeroLengthReader.contractViolations() == kNoCalls);
        CHECK(std::all_of(untouched.begin(), untouched.end(), [](std::uint8_t byte)
        {
            return byte == kUnwrittenByte;
        }));

        ScriptedEntropyReader zeroMaterialReader;
        zeroMaterialReader.SetMaterialByte(kZeroEntropyByte);
        zeroMaterialReader.AddProgress();
        std::array<std::uint8_t, kEntropyFillTestBytes> zeroMaterial{};
        CHECK(RandX::detail::FillOsEntropy(zeroMaterialReader, zeroMaterial.data(), zeroMaterial.size()));
        CHECK(AllBytesEqual(zeroMaterial, kZeroEntropyByte));
        CHECK(zeroMaterialReader.callCount() == kOneCall);
        CHECK(zeroMaterialReader.consumedStepCount() == zeroMaterialReader.stepCount());
        CHECK(zeroMaterialReader.unexpectedCalls() == kNoCalls);
        CHECK(zeroMaterialReader.contractViolations() == kNoCalls);

        ScriptedEntropyReader shortReadReader;
        shortReadReader.AddProgress(kShortReadFirstBytes);
        shortReadReader.AddInterrupted();
        shortReadReader.AddProgress(kShortReadSecondBytes);
        shortReadReader.AddProgress();
        std::array<std::uint8_t, kEntropyFillTestBytes> shortReadBytes{};
        CHECK(RandX::detail::FillOsEntropy(shortReadReader, shortReadBytes.data(), shortReadBytes.size()));
        CHECK(shortReadBytes == MakeMaterial<kEntropyFillTestBytes>());
        REQUIRE(shortReadReader.callCount() == kFourCalls);
        CHECK(shortReadReader.observation(kFirstCallIndex).outputOffset == kNoCalls);
        CHECK(shortReadReader.observation(kFirstCallIndex).requestedBytes == kEntropyFillTestBytes);
        CHECK(shortReadReader.observation(kFirstCallIndex).writtenBytes == kShortReadFirstBytes);
        CHECK(shortReadReader.observation(kSecondCallIndex).outputOffset == kShortReadFirstBytes);
        CHECK(shortReadReader.observation(kSecondCallIndex).requestedBytes ==
              kEntropyFillTestBytes - kShortReadFirstBytes);
        CHECK(shortReadReader.observation(kSecondCallIndex).status == Status::Interrupted);
        CHECK(shortReadReader.observation(kThirdCallIndex).outputOffset == kShortReadFirstBytes);
        CHECK(shortReadReader.observation(kThirdCallIndex).requestedBytes ==
              kEntropyFillTestBytes - kShortReadFirstBytes);
        CHECK(shortReadReader.observation(kThirdCallIndex).writtenBytes == kShortReadSecondBytes);
        CHECK(shortReadReader.observation(kFourthCallIndex).outputOffset ==
              kShortReadFirstBytes + kShortReadSecondBytes);
        CHECK(shortReadReader.observation(kFourthCallIndex).requestedBytes ==
              kEntropyFillTestBytes - kShortReadFirstBytes - kShortReadSecondBytes);
        CHECK(shortReadReader.observation(kFourthCallIndex).writtenBytes ==
              kEntropyFillTestBytes - kShortReadFirstBytes - kShortReadSecondBytes);
        CHECK(shortReadReader.consumedStepCount() == shortReadReader.stepCount());
        CHECK(shortReadReader.unexpectedCalls() == kNoCalls);
        CHECK(shortReadReader.contractViolations() == kNoCalls);

        ScriptedEntropyReader failedReader(kEntropyChunkLimit);
        failedReader.AddProgress();
        failedReader.AddProgress(kEntropyChunkSecondBytes);
        failedReader.AddFailure();
        std::array<std::uint8_t, kEntropyFillTestBytes> partialBytes{};
        partialBytes.fill(kUnwrittenByte);
        CHECK_FALSE(RandX::detail::FillOsEntropy(failedReader, partialBytes.data(), partialBytes.size()));
        REQUIRE(failedReader.callCount() == kThreeCalls);
        CHECK(failedReader.observation(kFirstCallIndex).requestedBytes == kEntropyChunkLimit);
        CHECK(failedReader.observation(kFirstCallIndex).writtenBytes == kEntropyChunkLimit);
        CHECK(failedReader.observation(kSecondCallIndex).outputOffset == kEntropyChunkLimit);
        CHECK(failedReader.observation(kSecondCallIndex).requestedBytes == kEntropyChunkLimit);
        CHECK(failedReader.observation(kSecondCallIndex).writtenBytes == kEntropyChunkSecondBytes);
        CHECK(failedReader.observation(kThirdCallIndex).outputOffset == kEntropyChunkFailurePrefix);
        CHECK(failedReader.observation(kThirdCallIndex).requestedBytes == kEntropyChunkLimit);
        CHECK(failedReader.observation(kThirdCallIndex).status == Status::Failure);
        CHECK(std::equal(partialBytes.begin(), partialBytes.begin() + kEntropyChunkFailurePrefix,
                         MakeMaterial<kEntropyFillTestBytes>().begin()));
        CHECK(std::all_of(partialBytes.begin() + kEntropyChunkFailurePrefix, partialBytes.end(),
                          [](std::uint8_t byte) { return byte == kUnwrittenByte; }));
        CHECK(failedReader.consumedStepCount() == failedReader.stepCount());
        CHECK(failedReader.unexpectedCalls() == kNoCalls);
        CHECK(failedReader.contractViolations() == kNoCalls);

        ScriptedEntropyReader immediateFailureReader;
        immediateFailureReader.AddFailure();
        std::array<std::uint8_t, kEntropyFillTestBytes> noProgressBytes{};
        CHECK_FALSE(RandX::detail::FillOsEntropy(
            immediateFailureReader, noProgressBytes.data(), noProgressBytes.size()));
        CHECK(immediateFailureReader.callCount() == kOneCall);
        CHECK(immediateFailureReader.observation(kFirstCallIndex).status == Status::Failure);
        CHECK(immediateFailureReader.consumedStepCount() == immediateFailureReader.stepCount());
        CHECK(immediateFailureReader.unexpectedCalls() == kNoCalls);
        CHECK(immediateFailureReader.contractViolations() == kNoCalls);

        ScriptedEntropyReader invalidProgressReader;
        invalidProgressReader.AddProgress(kNoCalls);
        std::array<std::uint8_t, kEntropyFillTestBytes> invalidProgressBytes{};
        CHECK_FALSE(RandX::detail::FillOsEntropy(
            invalidProgressReader, invalidProgressBytes.data(), invalidProgressBytes.size()));
        CHECK(invalidProgressReader.callCount() == kOneCall);
        CHECK(invalidProgressReader.consumedStepCount() == invalidProgressReader.stepCount());
        CHECK(invalidProgressReader.unexpectedCalls() == kNoCalls);
        CHECK(invalidProgressReader.contractViolations() == kOneContractViolation);
    }

    TEST_CASE("原生 reader 上限和当前平台状态转换")
    {
        using namespace RandXTest::EntropyFixtures;
        using Status = RandX::detail::EntropyReadStatus;

        RandX::detail::NativeOsEntropyReader reader;
        CHECK(reader.maxRequestSize() > kNoCalls);
        static_assert(noexcept(reader.maxRequestSize()));
        static_assert(noexcept(reader.read(nullptr, kNoCalls)));
        static_assert(noexcept(std::declval<ScriptedEntropyReader&>().read(nullptr, kNoCalls)));

#if defined(_WIN32) && __has_include(<bcrypt.h>)
        constexpr NTSTATUS kSuccessfulStatus = static_cast<NTSTATUS>(kNoCalls);
        constexpr NTSTATUS kFailedStatus = static_cast<NTSTATUS>(-1);
        const auto success = RandX::detail::ConvertWindowsEntropyResult(kSuccessfulStatus, kEntropyChunkLimit);
        const auto failure = RandX::detail::ConvertWindowsEntropyResult(kFailedStatus, kEntropyChunkLimit);
        CHECK(reader.maxRequestSize() == static_cast<std::size_t>((std::numeric_limits<ULONG>::max)()));
        CHECK(success.status == Status::Progress);
        CHECK(success.bytes == kEntropyChunkLimit);
        CHECK(failure.status == Status::Failure);
        CHECK(failure.bytes == kNoCalls);
#elif defined(__linux__) && __has_include(<sys/random.h>)
        constexpr ssize_t kSuccessfulResult = static_cast<ssize_t>(kEntropyChunkLimit);
        const auto progress = RandX::detail::ConvertLinuxEntropyResult(kSuccessfulResult, kNoCalls);
        const auto interrupted = RandX::detail::ConvertLinuxEntropyResult(
            kNativeInterruptedResult, EINTR);
        const auto failed = RandX::detail::ConvertLinuxEntropyResult(
            kNativeFailureResult, kNativeNonInterruptedError);
        const auto zero = RandX::detail::ConvertLinuxEntropyResult(0, kNoCalls);
        CHECK(reader.maxRequestSize() == static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)()));
        CHECK(progress.status == Status::Progress);
        CHECK(progress.bytes == kEntropyChunkLimit);
        CHECK(interrupted.status == Status::Interrupted);
        CHECK(interrupted.bytes == kNoCalls);
        CHECK(failed.status == Status::Failure);
        CHECK(zero.status == Status::Failure);
#elif defined(__APPLE__) && __has_include(<Security/Security.h>)
        constexpr int kFailedStatus = (std::numeric_limits<int>::min)();
        const auto success = RandX::detail::ConvertAppleEntropyResult(errSecSuccess, kEntropyChunkLimit);
        const auto failure = RandX::detail::ConvertAppleEntropyResult(kFailedStatus, kEntropyChunkLimit);
        CHECK(success.status == Status::Progress);
        CHECK(success.bytes == kEntropyChunkLimit);
        CHECK(failure.status == Status::Failure);
        CHECK(failure.bytes == kNoCalls);
#else
        CHECK(reader.maxRequestSize() == (std::numeric_limits<std::size_t>::max)());
#endif
    }

    TEST_CASE("ScopedWiper 在正常退出和异常展开时擦除存活数组")
    {
        using namespace RandXTest::EntropyFixtures;
        std::array<std::uint8_t, kChaCha20SeedBytes> normalBytes{};
        normalBytes.fill(kInnerMaterialByte);
        {
            RandX::detail::ScopedWiper wiper(normalBytes.data(), normalBytes.size());
        }
        CHECK(AllBytesEqual(normalBytes, kZeroEntropyByte));

        std::array<std::uint8_t, kChaCha20SeedBytes> exceptionalBytes{};
        exceptionalBytes.fill(kOuterMaterialByte);
        try
        {
            RandX::detail::ScopedWiper wiper(exceptionalBytes.data(), exceptionalBytes.size());
            throw std::runtime_error("scope unwinding");
        }
        catch (const std::runtime_error&)
        {
        }
        CHECK(AllBytesEqual(exceptionalBytes, kZeroEntropyByte));
    }

    TEST_CASE("SecureWipe 与兼容擦除只清零指定范围")
    {
        using WipeFunction = void (*)(void*, std::size_t) noexcept;
        const auto checkRange = [](WipeFunction wipe)
        {
            constexpr std::size_t kBufferSize = 17;
            constexpr std::size_t kWipeOffset = 3;
            constexpr std::size_t kWipeLength = 9;
            constexpr std::size_t kZeroLength = 0;
            constexpr std::uint8_t kZeroByte = 0;
            constexpr std::uint8_t kSentinelByte = 0xA5;
            std::array<std::uint8_t, kBufferSize> bytes{};
            bytes.fill(kSentinelByte);
            wipe(nullptr, kZeroLength);
            wipe(bytes.data() + kWipeOffset, kWipeLength);

            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                const bool isWiped = index >= kWipeOffset && index < kWipeOffset + kWipeLength;
                CHECK(bytes[index] == (isWiped ? kZeroByte : kSentinelByte));
            }
        };

        checkRange(&RandX::detail::SecureWipe);
        checkRange(&RandX::detail::SecureWipePortable);
    }

    TEST_CASE("RandomSeedWithSources 按优先级短路并在随机设备异常后回退")
    {
        using namespace RandXTest::EntropyFixtures;

        ScriptedSeedSources hardware;
        hardware.hardwareSucceeds = true;
        hardware.hardwareValue = kHardwareSeedValue;
        CHECK(RandX::detail::RandomSeedWithSources(hardware) == hardware.hardwareValue);
        REQUIRE(hardware.callCount == kOneCall);
        CHECK(hardware.calls[kFirstCallIndex] == SeedSourceCall::Hardware);
        CHECK(hardware.overflowCalls == kNoCalls);

        ScriptedSeedSources os;
        os.osSucceeds = true;
        os.osValue = kOsSeedValue;
        CHECK(RandX::detail::RandomSeedWithSources(os) == os.osValue);
        REQUIRE(os.callCount == kTwoCalls);
        CHECK(os.calls[kFirstCallIndex] == SeedSourceCall::Hardware);
        CHECK(os.calls[kSecondCallIndex] == SeedSourceCall::Os);
        CHECK(os.overflowCalls == kNoCalls);

        ScriptedSeedSources randomDevice;
        randomDevice.randomDeviceValue = kRandomDeviceSeedValue;
        CHECK(RandX::detail::RandomSeedWithSources(randomDevice) == randomDevice.randomDeviceValue);
        REQUIRE(randomDevice.callCount == kThreeCalls);
        CHECK(randomDevice.calls[kFirstCallIndex] == SeedSourceCall::Hardware);
        CHECK(randomDevice.calls[kSecondCallIndex] == SeedSourceCall::Os);
        CHECK(randomDevice.calls[kThirdCallIndex] == SeedSourceCall::RandomDevice);
        CHECK(randomDevice.randomDeviceConstructions == kOneCall);
        CHECK(randomDevice.randomDeviceReads == kTwoCalls);
        CHECK(randomDevice.overflowCalls == kNoCalls);

        for (const RandomDeviceFailure failure : {
                 RandomDeviceFailure::Construction,
                 RandomDeviceFailure::FirstRead,
                 RandomDeviceFailure::SecondRead})
        {
            ScriptedSeedSources fallback;
            fallback.randomDeviceFailure = failure;
            fallback.fallbackValue = kFallbackSeedValue;
            CHECK(RandX::detail::RandomSeedWithSources(fallback) == fallback.fallbackValue);
            REQUIRE(fallback.callCount == kFourCalls);
            CHECK(fallback.calls[kFirstCallIndex] == SeedSourceCall::Hardware);
            CHECK(fallback.calls[kSecondCallIndex] == SeedSourceCall::Os);
            CHECK(fallback.calls[kThirdCallIndex] == SeedSourceCall::RandomDevice);
            CHECK(fallback.calls[kFourthCallIndex] == SeedSourceCall::Fallback);
            CHECK(fallback.randomDeviceConstructions == kOneCall);
            const std::size_t expectedReadCount =
                failure == RandomDeviceFailure::Construction ? kNoCalls :
                failure == RandomDeviceFailure::FirstRead ? kOneCall : kTwoCalls;
            CHECK(fallback.randomDeviceReads == expectedReadCount);
            CHECK(fallback.overflowCalls == kNoCalls);
        }
    }
}

#endif
