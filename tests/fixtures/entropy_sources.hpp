#ifndef RANDX_TESTS_FIXTURES_ENTROPY_SOURCES_HPP
#define RANDX_TESTS_FIXTURES_ENTROPY_SOURCES_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "doctest.h"

#ifndef RANDX_ENABLE_ENTROPY_TEST_HOOKS
#error Entropy failure tests require RANDX_ENABLE_ENTROPY_TEST_HOOKS
#endif

namespace RandXTest::EntropyFixtures
{
constexpr std::size_t kEntropyScriptCapacity = 32;
constexpr std::size_t kEntropyMaterialCapacity = 256;
constexpr std::size_t kEntropyCallCapacity = 64;
constexpr std::size_t kChaCha20BlockBytes = 64;
constexpr std::size_t kChaCha20KeyBytes = 32;
constexpr std::size_t kChaCha20NonceBytes = 12;
constexpr std::size_t kChaCha20SeedBytes = kChaCha20KeyBytes + kChaCha20NonceBytes;
constexpr std::size_t kChaCha20BlockOutputCount =
    kChaCha20BlockBytes / sizeof(RandX::ChaCha20::result_type);
constexpr std::uint8_t kMaterialMultiplier = 37;
constexpr std::uint8_t kMaterialIncrement = 11;
constexpr std::uint8_t kOuterMaterialByte = 0x35;
constexpr std::uint8_t kInnerMaterialByte = 0xA2;
constexpr std::uint8_t kThreadMaterialByte = 0xC7;
constexpr std::uint8_t kUnwrittenByte = 0xE1;
constexpr std::uint8_t kZeroEntropyByte = 0;
constexpr std::uint64_t kZeroSeedValue = 0;
constexpr std::uint64_t kHardwareSeedValue = 101;
constexpr std::uint64_t kOsSeedValue = 202;
constexpr std::uint64_t kRandomDeviceSeedValue = 303;
constexpr std::uint64_t kFallbackSeedValue = 404;
constexpr std::uint32_t kRandomDeviceWordBits = 32;
constexpr std::size_t kNoCalls = 0;
constexpr std::size_t kOneCall = 1;
constexpr std::size_t kOneContractViolation = 1;
constexpr std::size_t kTwoCalls = 2;
constexpr std::size_t kThreeCalls = 3;
constexpr std::size_t kFourCalls = 4;
constexpr std::size_t kFirstCallIndex = 0;
constexpr std::size_t kSecondCallIndex = 1;
constexpr std::size_t kThirdCallIndex = 2;
constexpr std::size_t kFourthCallIndex = 3;
constexpr unsigned long long kOneOutputDiscard = 1;
constexpr unsigned long long kTwoOutputDiscard = 2;
constexpr std::size_t kFirstRandomDeviceRead = 1;

struct ReadStep
{
    RandX::detail::EntropyReadStatus status{RandX::detail::EntropyReadStatus::Progress};
    std::size_t maximumBytes{(std::numeric_limits<std::size_t>::max)()};
};

struct ReadObservation
{
    std::size_t outputOffset{0};
    std::size_t requestedBytes{0};
    std::size_t writtenBytes{0};
    RandX::detail::EntropyReadStatus status{RandX::detail::EntropyReadStatus::Failure};
};

class ScriptedEntropyReader
{
public:
    explicit ScriptedEntropyReader(std::size_t requestLimit = (std::numeric_limits<std::size_t>::max)()) noexcept
        : requestLimit_(requestLimit)
    {
        for (std::size_t index = 0; index < material_.size(); ++index)
        {
            material_[index] = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(index) * kMaterialMultiplier + kMaterialIncrement);
        }
    }

    void SetMaterialByte(std::uint8_t value) noexcept
    {
        material_.fill(value);
    }

    void SetMaterial(const std::uint8_t* bytes, std::size_t length) noexcept
    {
        const std::size_t copied = (std::min)(length, material_.size());
        for (std::size_t index = 0; index < copied; ++index)
            material_[index] = bytes[index];
    }

    void ClearPlan() noexcept
    {
        stepCount_ = 0;
        stepIndex_ = 0;
        materialOffset_ = 0;
        callCount_ = 0;
        unexpectedCalls_ = 0;
        contractViolations_ = 0;
        hookFillCalls_ = 0;
        observations_ = {};
    }

    void AddProgress(std::size_t maximumBytes = (std::numeric_limits<std::size_t>::max)()) noexcept
    {
        if (maximumBytes == kNoCalls)
        {
            ++contractViolations_;
            AddFailure();
            return;
        }
        AddStep({RandX::detail::EntropyReadStatus::Progress, maximumBytes});
    }

    void AddInterrupted() noexcept
    {
        AddStep({RandX::detail::EntropyReadStatus::Interrupted, 0});
    }

    void AddFailure() noexcept
    {
        AddStep({RandX::detail::EntropyReadStatus::Failure, 0});
    }

    std::size_t maxRequestSize() const noexcept
    {
        return requestLimit_;
    }

    RandX::detail::EntropyReadResult read(std::uint8_t* destination, std::size_t requestedBytes) noexcept
    {
        ReadObservation observation{};
        observation.outputOffset = materialOffset_;
        observation.requestedBytes = requestedBytes;

        if (requestedBytes == kNoCalls)
        {
            ++contractViolations_;
            observation.status = RandX::detail::EntropyReadStatus::Failure;
            Record(observation);
            return {observation.status, 0};
        }

        if (stepIndex_ >= stepCount_)
        {
            ++unexpectedCalls_;
            observation.status = RandX::detail::EntropyReadStatus::Failure;
            Record(observation);
            return {observation.status, 0};
        }

        const ReadStep step = steps_[stepIndex_++];
        observation.status = step.status;
        if (step.status != RandX::detail::EntropyReadStatus::Progress)
        {
            Record(observation);
            return {step.status, 0};
        }

        const std::size_t available = material_.size() - materialOffset_;
        const std::size_t written = (std::min)((std::min)(requestedBytes, step.maximumBytes), available);
        if (written == kNoCalls)
        {
            ++contractViolations_;
            observation.status = RandX::detail::EntropyReadStatus::Failure;
            Record(observation);
            return {observation.status, 0};
        }
        for (std::size_t index = 0; index < written; ++index)
            destination[index] = material_[materialOffset_ + index];

        observation.writtenBytes = written;
        materialOffset_ += written;
        Record(observation);
        return {step.status, written};
    }

    bool Fill(void* destination, std::size_t length) noexcept
    {
        ++hookFillCalls_;
        return RandX::detail::FillOsEntropy(*this, destination, length);
    }

    std::size_t stepCount() const noexcept { return stepCount_; }
    std::size_t consumedStepCount() const noexcept { return stepIndex_; }
    std::size_t callCount() const noexcept { return callCount_; }
    std::size_t unexpectedCalls() const noexcept { return unexpectedCalls_; }
    std::size_t contractViolations() const noexcept { return contractViolations_; }
    std::size_t hookFillCalls() const noexcept { return hookFillCalls_; }
    std::size_t materialBytesWritten() const noexcept { return materialOffset_; }
    const ReadObservation& observation(std::size_t index) const noexcept { return observations_[index]; }

private:
    void AddStep(ReadStep step) noexcept
    {
        if (stepCount_ >= steps_.size())
        {
            ++unexpectedCalls_;
            return;
        }
        steps_[stepCount_++] = step;
    }

    void Record(const ReadObservation& observation) noexcept
    {
        if (callCount_ < observations_.size())
            observations_[callCount_] = observation;
        else
            ++unexpectedCalls_;
        ++callCount_;
    }

    std::array<std::uint8_t, kEntropyMaterialCapacity> material_{};
    std::array<ReadStep, kEntropyScriptCapacity> steps_{};
    std::array<ReadObservation, kEntropyCallCapacity> observations_{};
    std::size_t requestLimit_;
    std::size_t stepCount_{0};
    std::size_t stepIndex_{0};
    std::size_t materialOffset_{0};
    std::size_t callCount_{0};
    std::size_t unexpectedCalls_{0};
    std::size_t contractViolations_{0};
    std::size_t hookFillCalls_{0};
};

inline bool FillFromScript(void* context, void* buffer, std::size_t length) noexcept
{
    return static_cast<ScriptedEntropyReader*>(context)->Fill(buffer, length);
}

class EntropyHookGuard
{
public:
    explicit EntropyHookGuard(ScriptedEntropyReader& reader) noexcept
        : previous_(RandX::detail::entropyTestHook)
    {
        RandX::detail::entropyTestHook = {&FillFromScript, &reader};
    }

    ~EntropyHookGuard() noexcept
    {
        RandX::detail::entropyTestHook = previous_;
    }

    EntropyHookGuard(const EntropyHookGuard&) = delete;
    EntropyHookGuard& operator=(const EntropyHookGuard&) = delete;
    EntropyHookGuard(EntropyHookGuard&&) = delete;
    EntropyHookGuard& operator=(EntropyHookGuard&&) = delete;

private:
    RandX::detail::EntropyTestHook previous_;
};

enum class RandomDeviceFailure
{
    None,
    Construction,
    FirstRead,
    SecondRead
};

enum class SeedSourceCall
{
    Hardware,
    Os,
    RandomDevice,
    Fallback
};

class ScriptedSeedSources
{
public:
    bool hardwareSucceeds{false};
    bool osSucceeds{false};
    std::uint64_t hardwareValue{0};
    std::uint64_t osValue{0};
    std::uint64_t randomDeviceValue{0};
    std::uint64_t fallbackValue{0};
    RandomDeviceFailure randomDeviceFailure{RandomDeviceFailure::None};
    std::array<SeedSourceCall, kEntropyScriptCapacity> calls{};
    std::size_t callCount{0};
    std::size_t overflowCalls{0};
    std::size_t randomDeviceConstructions{0};
    std::size_t randomDeviceReads{0};

    bool TryHardware(std::uint64_t& value) noexcept
    {
        Record(SeedSourceCall::Hardware);
        if (hardwareSucceeds)
            value = hardwareValue;
        return hardwareSucceeds;
    }

    bool TryOs(std::uint64_t& value) noexcept
    {
        Record(SeedSourceCall::Os);
        if (osSucceeds)
            value = osValue;
        return osSucceeds;
    }

    std::uint64_t ReadRandomDevice()
    {
        Record(SeedSourceCall::RandomDevice);
        ++randomDeviceConstructions;
        if (randomDeviceFailure == RandomDeviceFailure::Construction)
            throw std::runtime_error("scripted random_device failure");

        const std::uint32_t highWord = ReadRandomDeviceWord(RandomDeviceFailure::FirstRead);
        const std::uint32_t lowWord = ReadRandomDeviceWord(RandomDeviceFailure::SecondRead);
        return (static_cast<std::uint64_t>(highWord) << kRandomDeviceWordBits) | lowWord;
    }

    std::uint64_t Fallback() noexcept
    {
        Record(SeedSourceCall::Fallback);
        return fallbackValue;
    }

private:
    std::uint32_t ReadRandomDeviceWord(RandomDeviceFailure failurePoint)
    {
        ++randomDeviceReads;
        if (randomDeviceFailure == failurePoint)
            throw std::runtime_error("scripted random_device failure");
        if (randomDeviceReads == kFirstRandomDeviceRead)
            return static_cast<std::uint32_t>(randomDeviceValue >> kRandomDeviceWordBits);
        return static_cast<std::uint32_t>(randomDeviceValue);
    }

    void Record(SeedSourceCall call) noexcept
    {
        if (callCount < calls.size())
            calls[callCount] = call;
        else
            ++overflowCalls;
        ++callCount;
    }
};

void EntropyCrossTranslationUnitSecureRandomBytes(void* buffer, std::size_t length);
std::uint64_t EntropyCrossTranslationUnitSecureSeed();

template <std::size_t Size>
void FillMaterial(std::array<std::uint8_t, Size>& destination, std::uint8_t value) noexcept
{
    destination.fill(value);
}

template <std::size_t Size>
std::array<std::uint8_t, Size> MakeMaterial(std::uint8_t salt = 0) noexcept
{
    std::array<std::uint8_t, Size> material{};
    for (std::size_t index = 0; index < material.size(); ++index)
    {
        material[index] = static_cast<std::uint8_t>(
            salt + static_cast<std::uint8_t>(index) * kMaterialMultiplier + kMaterialIncrement);
    }
    return material;
}

template <std::size_t Size>
bool AllBytesEqual(const std::array<std::uint8_t, Size>& bytes, std::uint8_t value) noexcept
{
    for (const std::uint8_t byte : bytes)
    {
        if (byte != value)
            return false;
    }
    return true;
}
}

#endif
