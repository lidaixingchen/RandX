#!/usr/bin/env bash
# 下载并构建 PractRand 到 ./PractRand_build/
# 用法：bash download_practrand.sh
#
# 产物：
#   PractRand_build/RNG_test   PractRand 测试驱动
#
# 环境要求：g++、make、git
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/PractRand_build"
SRC_DIR="${SCRIPT_DIR}/PractRand_src"

# 检查命令行依赖
for cmd in git g++; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "错误：缺少依赖 $cmd，请先安装" >&2; exit 1; }
done

# PractRand 上游（镜像仓库）与锁定 commit
PRACTRAND_REPO="https://github.com/csc-lab/PractRand.git"
PRACTRAND_COMMIT="82f4facf15fba871c3820d3c3b394804b7c4c870"

echo "[1/4] 清理旧构建 ..."
case "${BUILD_DIR}" in
  "${SCRIPT_DIR}"/*)
    rm -rf "${BUILD_DIR}" "${SRC_DIR}"
    ;;
  *)
    echo "错误：BUILD_DIR 路径异常 (${BUILD_DIR})，拒绝清理" >&2
    exit 1
    ;;
esac

echo "[2/4] 克隆 PractRand (${PRACTRAND_COMMIT}) ..."
git clone "${PRACTRAND_REPO}" "${SRC_DIR}"
pushd "${SRC_DIR}" >/dev/null
git checkout "${PRACTRAND_COMMIT}"

echo "[3/4] 构建 RNG_test ..."
mkdir -p bin
shopt -s nullglob
SOURCES=(src/*.cpp src/*/*.cpp src/*/*/*.cpp)
shopt -u nullglob
if [ ${#SOURCES[@]} -eq 0 ]; then
    echo "错误：未找到 PractRand 源文件" >&2
    exit 1
fi
g++ -O3 -Iinclude -I. "${SOURCES[@]}" tools/RNG_test.cpp -o bin/RNG_test -lpthread
popd >/dev/null

echo "[4/4] 拷贝产物到 ${BUILD_DIR}/ ..."
mkdir -p "${BUILD_DIR}"
cp "${SRC_DIR}/bin/RNG_test" "${BUILD_DIR}/RNG_test"

# 清理源码（保留构建目录）
rm -rf "${SRC_DIR}"

echo "完成：${BUILD_DIR}/RNG_test"
