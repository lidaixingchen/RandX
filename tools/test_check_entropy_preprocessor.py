"""生产熵源预处理检查的标准库单测。"""

from __future__ import annotations

import unittest

from check_entropy_preprocessor import PreprocessorError, validate_preprocessed_source


NATIVE_READER_BODY: str = (
    "inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept { "
    "if (n == 0) return true; NativeOsEntropyReader reader; "
    "return FillOsEntropy(reader, buf, n); }"
)
SOURCE_NAME: str = "RandX.hpp"


class ProductionPreprocessorTests(unittest.TestCase):
    def test_accepts_native_reader_without_entropy_hook(self) -> None:
        preprocessed_source: str = f"thread_local int default_engine; {NATIVE_READER_BODY}"

        validate_preprocessed_source(preprocessed_source, SOURCE_NAME)

    def test_rejects_hook_symbols(self) -> None:
        preprocessed_source: str = f"EntropyTestHook hook; {NATIVE_READER_BODY}"

        with self.assertRaisesRegex(PreprocessorError, "hook 标识"):
            validate_preprocessed_source(preprocessed_source, SOURCE_NAME)

    def test_rejects_hook_dispatch_in_entropy_reader(self) -> None:
        preprocessed_source: str = (
            "inline bool GetOsEntropyBytes(void* buf, std::size_t n) noexcept { "
            "if (n == 0) return true; NativeOsEntropyReader reader; "
            "return callback(context, buf, n); }"
        )

        with self.assertRaisesRegex(PreprocessorError, "未直接使用 NativeOsEntropyReader"):
            validate_preprocessed_source(preprocessed_source, SOURCE_NAME)

    def test_rejects_missing_entropy_reader_definition(self) -> None:
        with self.assertRaisesRegex(PreprocessorError, "缺少 GetOsEntropyBytes"):
            validate_preprocessed_source("namespace RandX {}", SOURCE_NAME)


if __name__ == "__main__":
    unittest.main()
