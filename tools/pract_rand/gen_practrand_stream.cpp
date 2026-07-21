// gen_practrand_stream.cpp — 将指定 RandX 引擎的原始字节流写到 stdout（二进制）
//
// 用途：作为 PractRand 统计检验的输入源。
//   ./gen_practrand_stream <engine> [seed] | RNG_output stdin
//
// 引擎名（不区分大小写）：
//   xoshiro256**       64-bit 输出
//   xoroshiro128**     64-bit
//   xoshiro128**       32-bit
//   xoroshiro64**       32-bit
//   splitmix64         64-bit
//   sfc64              64-bit
//   romuduojr          64-bit
//   chacha20           64-bit（CSPRNG，OS 熵播种，seed 参数被忽略）
//
// 字节序：所有引擎输出按平台原生小端序写入 stdout（PractRand 不关心字节序，
// 只需保证字节流均匀分布即可）。
//
// 编译：
//   g++ -std=c++17 -O2 -Wall -Wextra -I <repo_root>
//       -o gen_practrand_stream gen_practrand_stream.cpp -lbcrypt
//   （Linux 无需 -lbcrypt；macOS 改 -framework Security）

#include "RandX_Cpp17.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#	include <io.h>     // _setmode
#	include <fcntl.h>  // _O_BINARY
#endif

namespace
{
    // 缓冲区大小：64 KiB，平衡 fwrite 系统调用开销与内存占用
    constexpr std::size_t BUFFER_BYTES = 64 * 1024;
    // 默认种子（仅用于统计 PRNG；ChaCha20 走 OS 熵）
    constexpr std::uint64_t DEFAULT_SEED = 0x9E3779B97F4A7C15ULL;

    // 不区分大小写比较引擎名（移除 ** 后缀以便 "xoshiro256**" 与 "xoshiro256" 等价）
    bool engineIs(std::string_view name, const char* candidate)
    {
        const std::size_t n = name.size();
        const std::size_t c = std::strlen(candidate);
        if (n != c && !(n == c + 2 && name.substr(c, 2) == "**"))
            return false;
        for (std::size_t i = 0; i < c; ++i)
        {
            char a = name[i];
            char b = candidate[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    // 将 stdout 切换到二进制模式（Windows 防止 \n → \r\n 污染）
    void setStdoutBinary()
    {
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        {
            std::perror("_setmode");
            std::exit(2);
        }
#endif
    }

    // 通用：把 rng 输出按字节填入缓冲区，写满即 flush
    template <class RNG>
    void runStream(RNG& rng)
    {
        std::uint8_t buf[BUFFER_BYTES];
        std::size_t pos = 0;
        constexpr std::size_t OUT_BYTES = sizeof(typename RNG::result_type);
        // 无限循环：由调用方（管道关闭 / PractRand 测试长度）终止
        while (true)
        {
            auto v = rng();
            std::memcpy(&buf[pos], &v, OUT_BYTES);
            pos += OUT_BYTES;
            if (pos + OUT_BYTES > BUFFER_BYTES)
            {
                if (std::fwrite(buf, 1, pos, stdout) != pos)
                {
                    // 下游（PractRand）已关闭管道：正常退出
                    return;
                }
                pos = 0;
            }
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr,
            "usage: %s <engine> [seed]\n"
            "engines: xoshiro256** xoroshiro128** xoshiro128** xoroshiro64** "
            "splitmix64 sfc64 romuduojr chacha20\n",
            argv[0]);
        return 1;
    }

    const std::string engineArg = argv[1];
    const std::uint64_t seed = (argc >= 3) ? std::strtoull(argv[2], nullptr, 10) : DEFAULT_SEED;

    setStdoutBinary();

    if (engineIs(engineArg, "xoshiro256"))
    {
        RandX::Xoshiro256StarStar rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "xoroshiro128"))
    {
        RandX::Xoroshiro128StarStar rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "xoshiro128"))
    {
        RandX::Xoshiro128StarStar rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "xoroshiro64"))
    {
        RandX::Xoroshiro64StarStar rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "splitmix64"))
    {
        RandX::SplitMix64 rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "sfc64"))
    {
        RandX::SFC64 rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "romuduojr"))
    {
        RandX::RomuDuoJr rng{ seed };
        runStream(rng);
    }
    else if (engineIs(engineArg, "chacha20"))
    {
        // CSPRNG：忽略 seed 参数，OS 熵播种
        RandX::ChaCha20 rng;
        runStream(rng);
    }
    else
    {
        std::fprintf(stderr, "unknown engine: %s\n", engineArg.c_str());
        return 3;
    }
    return 0;
}
