#!/usr/bin/env bash
# 下载并构建 PractRand 到 ./PractRand_build/
# 用法：bash download_practrand.sh
#
# 产物：
#   PractRand_build/RNG_output   PractRand 测试驱动
#
# 环境要求：g++、make、git
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/PractRand_build"
SRC_DIR="${SCRIPT_DIR}/PractRand_src"

# PractRand 上游（官方仓库）
PRACTRAND_REPO="https://github.com/PractRand/PractRand.git"
# 锁定版本 tag（避免上游破坏性变更）
PRACTRAND_TAG="v0.95"

echo "[1/4] 清理旧构建 ..."
rm -rf "${BUILD_DIR}" "${SRC_DIR}"

echo "[2/4] 克隆 PractRand ${PRACTRAND_TAG} ..."
git clone --depth 1 --branch "${PRACTRAND_TAG}" "${PRACTRAND_REPO}" "${SRC_DIR}"

echo "[3/4] 构建 RNG_output ..."
pushd "${SRC_DIR}" >/dev/null
# PractRand 用 makefile 直接构建，无 configure 步骤
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)" build
popd >/dev/null

echo "[4/4] 拷贝产物到 ${BUILD_DIR}/ ..."
mkdir -p "${BUILD_DIR}"
cp "${SRC_DIR}/bin/RNG_output" "${BUILD_DIR}/RNG_output"

# 清理源码（保留构建目录）
rm -rf "${SRC_DIR}"

echo "完成：${BUILD_DIR}/RNG_output"
