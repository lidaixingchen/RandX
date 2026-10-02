	////////////////////////////////////////////////////////////////
	//
	//	便捷工具函数
	//

	// 生成非确定性的 64 位种子（优先硬件 RNG，用于统计 PRNG 播种）
	// 优先级链：RDRAND (x86_64) → detail::GetOsEntropyBytes → std::random_device → 时间戳回退
// @randx-include ../common/security/random_seed_definition.inc

	/// @brief 获取当前线程专属的默认伪随机数生成引擎
	/// @return 线程局部 Xoshiro256StarStar 引擎的左值引用
	/// @warning 返回引用的生命周期严格绑定于当前线程的线程局部存储（TLS），
	///          严禁跨线程转移引用、捕获引用传递给异地异步任务（如 std::async、线程池）。
// @randx-include ../common/engines/default_engine_definition.inc

	/// @brief 重新播种当前线程的默认引擎（用于 POSIX fork() 产生子进程后重置引擎状态）
// @randx-include ../common/engines/reset_thread_local_engine_definition.inc

	/// @defgroup csprng 密码学安全
	/// @brief ChaCha20 CSPRNG 与 OS 熵源 API

	/// @brief 用 OS 密码学熵源填充 [buf, buf+n) 字节
	/// @param buf 目标缓冲区指针
	/// @param n 需要填充的字节数
	/// @throw std::runtime_error 当 OS 熵源不可用时抛出（CSPRNG 无熵不可用 = 致命错误）
// @randx-include ../common/security/secure_random_bytes_definition.inc

	/// @brief 生成密码学安全的 64 位随机种子
	/// @return 复用 SecureRandomBytes 取前 8 字节的种子值
// @randx-include ../common/security/secure_seed_definition.inc

	/// @brief 编译期特性检测：检测当前平台是否支持 OS 密码学熵源 API
	/// @return true 表示支持真 OS 密码学 API（BCryptGenRandom/getrandom/SecRandomCopyBytes）；
	///         false 表示缺少 OS 密码学支持。密码学安全组件在熵源失败时直接抛异常，绝不隐式降级。
// @randx-include ../common/security/is_os_crypto_entropy_available_definition.inc

	////////////////////////////////////////////////////////////////
	//
	//	ChaCha20 (RFC 8439) — CSPRNG 引擎实现
	//
	//  状态矩阵布局（16 × uint32，常数省略存于 m_state[0..11]）：
	//    0  1  2  3      "expa"  "nd 3"  "2-by"  "te k"   ← 常数（generateBlock 时补齐）
	//    4  5  6  7      key[0]  key[1]  key[2]  key[3]   ← m_state[0..3]
	//    8  9 10 11      key[4]  key[5]  key[6]  key[7]   ← m_state[4..7]
	//   12 13 14 15      ctr    nonce[0] nonce[1] nonce[2]← m_state[8..11]
	//
	//  生成流程：operator() → reseedIfNecessary → (缓存耗尽时)generateBlock → 取 8 字节
	//

// @randx-include ../common/security/cha_cha20_cha_cha20_03_definition.inc

// @randx-include ../common/security/cha_cha20_move_assign_definition.inc

// @randx-include ../common/security/cha_cha20_destroy_cha_cha20_definition.inc

	// 构造方式 1：从 OS 熵自动播种（密码学安全，默认）
// @randx-include ../common/security/cha_cha20_cha_cha20_04_definition.inc

	// 构造方式 2：显式种子（仅测试/复现，非密码学安全）
	// 用 SplitMix64 将 64-bit 种子扩展为 32 字节 key + 12 字节 nonce
// @randx-include ../common/security/cha_cha20_cha_cha20_02_definition.inc

	// 构造方式 3：直接指定 key + nonce + counter
// @randx-include ../common/security/cha_cha20_cha_cha20_definition.inc

	// 生成一个 ChaCha20 block（64 字节）填充 m_buffer
// @randx-include ../common/security/cha_cha20_generate_block_definition.inc

	// 自上次 reseed 以来输出字节数达到阈值时自动 reseed（前向安全）
// @randx-include ../common/security/cha_cha20_reseed_if_necessary_definition.inc

	// 从 OS 熵重新播种：32 字节新 key + 12 字节新 nonce，重置 counter=0、缓存标记耗尽
// @randx-include ../common/security/cha_cha20_reseed_definition.inc

	// 生成一个 64-bit 随机数（从缓存取 8 字节，缓存耗尽时生成新 block）
// @randx-include ../common/security/cha_cha20_next_definition.inc

// @randx-include ../common/security/cha_cha20_discard_definition.inc
