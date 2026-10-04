#if defined(_MSC_VER)
#define RANDX_AUDIT_NOINLINE __declspec(noinline)
#define RANDX_AUDIT_USED
#elif defined(__GNUC__) || defined(__clang__)
#define RANDX_AUDIT_NOINLINE __attribute__((noinline))
#define RANDX_AUDIT_USED __attribute__((used, externally_visible))
#else
#define RANDX_AUDIT_NOINLINE
#define RANDX_AUDIT_USED
#endif

#if defined(RANDX_AUDIT_CPP17)
#include "RandX_Cpp17.hpp"
#else
#include "RandX.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace
{
volatile std::uint64_t auditSink = 0;
constexpr std::size_t DirectMaterialSize = 37;
constexpr std::size_t ScopedMaterialSize = 43;
constexpr std::size_t InitializerMaterialSize = 29;
constexpr std::size_t ExceptionMaterialSize = 41;
constexpr std::uint64_t IndependentSeedMask = 0x9E3779B97F4A7C15ULL;

RANDX_AUDIT_NOINLINE void consumeBytes(const std::uint8_t* const bytes,
                                       const std::size_t length) noexcept
{
	std::uint64_t value = static_cast<std::uint64_t>(length);
	for (std::size_t index = 0; index < length; ++index)
	{
		value = (value << 5) ^ (value >> 2) ^ bytes[index];
	}
	auditSink = value;
}

RANDX_AUDIT_NOINLINE void consumeValue(const std::uint64_t value) noexcept
{
	auditSink = (auditSink << 1) ^ value;
}

void fillBytes(std::uint8_t* const bytes,
               const std::size_t length,
               std::uint64_t value) noexcept
{
	for (std::size_t index = 0; index < length; ++index)
	{
		value ^= value << 13;
		value ^= value >> 7;
		value ^= value << 17;
		bytes[index] = static_cast<std::uint8_t>(value >> ((index % sizeof(value)) * 8));
	}
}

[[noreturn]] RANDX_AUDIT_NOINLINE void throwAudit(const std::uint64_t input)
{
	throw input;
}
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_direct(const std::uint64_t input) noexcept
{
	std::array<std::uint8_t, DirectMaterialSize> material{};
	fillBytes(material.data(), material.size(), input | 1U);
	consumeBytes(material.data(), material.size());
#if !defined(RANDX_AUDIT_MUTATE_DIRECT)
	RandX::detail::SecureWipe(material.data(), material.size());
#endif
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_scoped(const std::uint64_t input) noexcept
{
	std::array<std::uint8_t, ScopedMaterialSize> material{};
#if !defined(RANDX_AUDIT_MUTATE_SCOPED)
	RandX::detail::ScopedWiper wiper(material.data(), material.size());
#endif
	fillBytes(material.data(), material.size(), input | 1U);
	consumeBytes(material.data(), material.size());
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_exception(const std::uint64_t input)
{
	std::array<std::uint8_t, ExceptionMaterialSize> material{};
#if !defined(RANDX_AUDIT_MUTATE_EXCEPTION)
	RandX::detail::ScopedWiper wiper(material.data(), material.size());
#endif
	fillBytes(material.data(), material.size(), input | 1U);
	consumeBytes(material.data(), material.size());
	throwAudit(input);
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_generate_block(const std::uint64_t input)
{
	RandX::ChaCha20 generator{input | 1U};
	consumeValue(generator());
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_reseed(const std::uint64_t input)
{
	RandX::ChaCha20 generator{input | 1U};
	consumeValue(generator());
	generator.reseed();
	consumeValue(generator());
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_move_construct(RandX::ChaCha20* destination, RandX::ChaCha20* source)
{
	::new (destination) RandX::ChaCha20(std::move(*source));
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_move_assign(RandX::ChaCha20* destination, RandX::ChaCha20* source)
{
	*destination = std::move(*source);
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_destructor(RandX::ChaCha20* generator)
{
	generator->~ChaCha20();
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_probe_initializer_only(const std::uint64_t input) noexcept
{
	std::array<std::uint8_t, InitializerMaterialSize> material{};
	material[0] = static_cast<std::uint8_t>(input);
	consumeBytes(material.data(), material.size());
}

#if defined(RANDX_AUDIT_TRIANGULAR)
extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
double randx_audit_triangular_invalid()
{
	return RandX::RandTriangular(2.0, 1.0, 3.0);
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
double randx_audit_triangular_degenerate()
{
	return RandX::RandTriangular(7.0, 7.0, 7.0);
}

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
double randx_audit_triangular_valid()
{
	return RandX::RandTriangular(0.0, 0.5, 1.0);
}
#endif

extern "C" RANDX_AUDIT_NOINLINE RANDX_AUDIT_USED
void randx_audit_entry(const std::uint64_t input)
{
	randx_audit_probe_direct(input);
	randx_audit_probe_scoped(input + 1U);
	try { randx_audit_probe_exception(input); }
	catch (const std::uint64_t value) { consumeValue(value); }
	randx_audit_probe_generate_block(input + 2U);
	randx_audit_probe_reseed(input + 3U);
	RandX::ChaCha20 source{input | 1U};
	RandX::ChaCha20 destination{input ^ IndependentSeedMask};
	consumeValue(source());
	consumeValue(destination());
	randx_audit_probe_move_assign(&destination, &source);
	consumeValue(destination());
	alignas(RandX::ChaCha20) std::array<std::byte, sizeof(RandX::ChaCha20)> storage;
	auto* moved = reinterpret_cast<RandX::ChaCha20*>(storage.data());
	randx_audit_probe_move_construct(moved, &destination);
	consumeValue(moved->operator()());
	randx_audit_probe_destructor(moved);
	randx_audit_probe_initializer_only(input + 7U);
}

int main(const int argc, char** const argv)
{
#if defined(RANDX_AUDIT_TRIANGULAR)
	try { (void)randx_audit_triangular_invalid(); return 1; }
	catch (const std::invalid_argument&) {}
	if (randx_audit_triangular_degenerate() != 7.0) return 1;
	if (!(randx_audit_triangular_valid() < 1.0)) return 1;
#endif
	const std::uint64_t input = static_cast<std::uint64_t>(argc)
		^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(argv));
	randx_audit_entry(input);
	return static_cast<int>(auditSink & 0U);
}
