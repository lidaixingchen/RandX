#ifndef RANDX_TESTS_COMMON_DEFAULT_ENGINE_CONTRACTS_HPP
#define RANDX_TESTS_COMMON_DEFAULT_ENGINE_CONTRACTS_HPP

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <limits>
#include <mutex>
#include <thread>

#include "doctest.h"
#include "fixtures.hpp"
#include "../fixtures/default_engine.hpp"

namespace RandXTest::DefaultEngineFixtures
{
template <class Integer>
inline void CheckDefaultIntegerRange(Integer minimum, Integer maximum)
{
    RandX::Xoshiro256StarStar expected{RandXTest::TestConstants::kDefaultEngineTestSeed};
    RandX::Reseed(RandXTest::TestConstants::kDefaultEngineTestSeed);

    for (std::size_t index = 0; index < kRangeSampleCount; ++index)
    {
        const Integer actualValue = RandX::RandInt<Integer>(minimum, maximum);
        const Integer expectedValue = RandX::RandInt<Integer>(expected, minimum, maximum);
        CHECK(actualValue == expectedValue);
    }

    CHECK(RandX::DefaultEngine().serialize() == expected.serialize());
}

class WorkerReleaseGuard
{
public:
    WorkerReleaseGuard(
        std::thread& worker,
        std::mutex& mutex,
        std::condition_variable& condition,
        bool& released) noexcept
        : worker_(worker), mutex_(mutex), condition_(condition), released_(released)
    {
    }

    ~WorkerReleaseGuard() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }

    WorkerReleaseGuard(const WorkerReleaseGuard&) = delete;
    WorkerReleaseGuard& operator=(const WorkerReleaseGuard&) = delete;

private:
    std::thread& worker_;
    std::mutex& mutex_;
    std::condition_variable& condition_;
    bool& released_;
};

struct WorkerObservation
{
    const void* entryAddress{nullptr};
    const void* bridgeAddress{nullptr};
    DefaultEngineState stateAfterReseed{};
    DefaultEngineState stateAfterOutput{};
    DefaultEngineState stateAfterPostReset{};
    DefaultEngineState stateAfterMainProgress{};
    std::uint64_t output{0};
    std::uint64_t expectedOutput{0};
    std::exception_ptr failure{};
};
}

TEST_SUITE("公共/基础/默认引擎")
{
    TEST_CASE("固定种子原始输出与状态保持一致")
    {
        using namespace RandXTest::DefaultEngineFixtures;

        RandX::Reseed(kRawOutputSeed);
        RandX::Xoshiro256StarStar expected{kRawOutputSeed};

        for (std::size_t index = 0; index < kExpectedRawOutputs.size(); ++index)
        {
            const std::uint64_t actualOutput = RandX::DefaultEngine()();
            const std::uint64_t expectedOutput = expected();
            CHECK(actualOutput == kExpectedRawOutputs[index]);
            CHECK(actualOutput == expectedOutput);
        }

        CHECK(RandX::DefaultEngine().serialize() == expected.serialize());
    }

    TEST_CASE("固定种子整数入口覆盖闭区间与完整类型范围")
    {
        using namespace RandXTest::DefaultEngineFixtures;

        CheckDefaultIntegerRange(kDiceMinimum, kDiceMaximum);
        CheckDefaultIntegerRange(0U, kPowerOfTwoRangeSize - 1U);
        CheckDefaultIntegerRange(kSingleValue, kSingleValue);
        CheckDefaultIntegerRange(kCrossZeroMinimum, kCrossZeroMaximum);
        CheckDefaultIntegerRange(
            (std::numeric_limits<std::int64_t>::min)(),
            (std::numeric_limits<std::int64_t>::max)());
        CheckDefaultIntegerRange(
            (std::numeric_limits<std::uint64_t>::min)(),
            (std::numeric_limits<std::uint64_t>::max)());
    }

    TEST_CASE("重播种与随机重置后跨翻译单元共享状态")
    {
        using namespace RandXTest::DefaultEngineFixtures;

        RandX::Reseed(kCrossTranslationUnitSeed);
        RandX::Xoshiro256StarStar expected{kCrossTranslationUnitSeed};
        const void* const entryAddress = &RandX::DefaultEngine();

        CHECK(DefaultEngineAddressFromBridge() == entryAddress);
        CHECK(DefaultEngineStateFromBridge() == expected.serialize());
        CHECK(DefaultEngineOutputFromBridge() == expected());
        CHECK(RandX::DefaultEngine().serialize() == expected.serialize());

        DefaultEngineReseedFromBridge(kSecondReseedSeed);
        expected = RandX::Xoshiro256StarStar{kSecondReseedSeed};
        CHECK(&RandX::DefaultEngine() == entryAddress);
        CHECK(RandX::DefaultEngine().serialize() == expected.serialize());

        DefaultEngineResetFromBridge();
        expected = RandX::DefaultEngine();
        CHECK(&RandX::DefaultEngine() == entryAddress);
        CHECK(DefaultEngineAddressFromBridge() == entryAddress);
        CHECK(DefaultEngineStateFromBridge() == expected.serialize());
        CHECK(RandX::DefaultEngine()() == expected());
        CHECK(DefaultEngineStateFromBridge() == expected.serialize());

        DefaultEngineReseedRandomFromBridge();
        expected = RandX::DefaultEngine();
        CHECK(&RandX::DefaultEngine() == entryAddress);
        CHECK(DefaultEngineAddressFromBridge() == entryAddress);
        CHECK(DefaultEngineStateFromBridge() == expected.serialize());
        CHECK(DefaultEngineOutputFromBridge() == expected());
        CHECK(RandX::DefaultEngine().serialize() == expected.serialize());
    }

    TEST_CASE("新线程首次访问与存活线程之间保持确定性隔离")
    {
        using namespace RandXTest::DefaultEngineFixtures;

        RandX::Reseed(kMainThreadSeed);
        RandX::Xoshiro256StarStar expectedMain{kMainThreadSeed};
        const void* const mainAddress = &RandX::DefaultEngine();

        WorkerObservation workerObservation{};
        std::promise<void> workerReadyPromise;
        std::future<void> workerReady = workerReadyPromise.get_future();
        std::promise<void> workerInspectedPromise;
        std::future<void> workerInspected = workerInspectedPromise.get_future();
        std::mutex releaseMutex;
        std::condition_variable releaseCondition;
        bool inspectWorkerRequested = false;
        bool workerReleased = false;

        std::thread worker([&]() {
            try
            {
                workerObservation.entryAddress = &RandX::DefaultEngine();
                workerObservation.bridgeAddress = DefaultEngineAddressFromBridge();
                RandX::Reseed(kWorkerThreadSeed);
                RandX::Xoshiro256StarStar expectedWorker{kWorkerThreadSeed};
                workerObservation.stateAfterReseed = DefaultEngineStateFromBridge();
                workerObservation.output = DefaultEngineOutputFromBridge();
                workerObservation.expectedOutput = expectedWorker();
                workerObservation.stateAfterOutput = RandX::DefaultEngine().serialize();
                RandX::Reseed(kPostResetWorkerSeed);
                workerObservation.stateAfterPostReset = DefaultEngineStateFromBridge();
            }
            catch (...)
            {
                workerObservation.failure = std::current_exception();
            }

            workerReadyPromise.set_value();
            std::unique_lock<std::mutex> lock(releaseMutex);
            releaseCondition.wait(lock, [&]() { return inspectWorkerRequested || workerReleased; });
            if (inspectWorkerRequested)
            {
                workerObservation.stateAfterMainProgress = DefaultEngineStateFromBridge();
                workerInspectedPromise.set_value();
            }
            releaseCondition.wait(lock, [&]() { return workerReleased; });
        });
        WorkerReleaseGuard releaseWorker(worker, releaseMutex, releaseCondition, workerReleased);

        workerReady.wait();
        CHECK(workerObservation.failure == nullptr);
        CHECK(workerObservation.entryAddress == workerObservation.bridgeAddress);
        CHECK(workerObservation.entryAddress != mainAddress);

        RandX::Reseed(kMainThreadSeed);
        expectedMain = RandX::Xoshiro256StarStar{kMainThreadSeed};
        for (std::size_t index = 0; index < kRangeSampleCount; ++index)
        {
            CHECK(RandX::RandInt<int>(kDiceMinimum, kDiceMaximum)
                == RandX::RandInt<int>(expectedMain, kDiceMinimum, kDiceMaximum));
        }
        CHECK(RandX::DefaultEngine().serialize() == expectedMain.serialize());

        RandX::Xoshiro256StarStar expectedWorker{kWorkerThreadSeed};
        CHECK(workerObservation.stateAfterReseed == expectedWorker.serialize());
        CHECK(workerObservation.output == workerObservation.expectedOutput);
        (void)expectedWorker();
        CHECK(workerObservation.stateAfterOutput == expectedWorker.serialize());
        expectedWorker = RandX::Xoshiro256StarStar{kPostResetWorkerSeed};
        CHECK(workerObservation.stateAfterPostReset == expectedWorker.serialize());
        {
            std::lock_guard<std::mutex> lock(releaseMutex);
            inspectWorkerRequested = true;
        }
        releaseCondition.notify_one();
        workerInspected.wait();
        CHECK(workerObservation.stateAfterMainProgress == expectedWorker.serialize());
        CHECK(RandX::DefaultEngine().serialize() == expectedMain.serialize());
    }
}

#endif
