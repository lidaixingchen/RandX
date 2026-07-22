# 发布流程

本指南面向 RandX 维护者，描述如何将新版本发布到三个发布渠道。

## 渠道概览

| 渠道 | 适用用户 | 提交方式 | 状态 |
|------|---------|---------|------|
| **vcpkg 官方 ports** | CMake / MSBuild 用户（最广） | PR 到 microsoft/vcpkg | 待首发 |
| **xmake-repo** | xmake 用户 | PR 到 xmake-io/xmake-repo | 待首发 |
| **CMake FetchContent** | 所有 CMake 3.11+ 用户 | 无需提交，靠 Git tag 自动可用 | 已可用 |

> **官方 registry 不接受 CI 自动推送**——必须通过 PR review 流程。CI 工作流 [`packaging-validation.yml`](.github/workflows/packaging-validation.yml) 仅做本地预演，验证 port 能否构建。

## 前置准备

### 本地环境

```powershell
# 必备
git --version           # >= 2.30
xmake --version         # >= 2.7（用于 xrepo 验证）

# 可选（用于本地 port 验证，CI 已覆盖）
vcpkg version           # 任意受支持版本
```

### Fork 目标仓库

发布前需一次性 fork 两个目标 registry（首次发布时）：

- vcpkg：fork [microsoft/vcpkg](https://github.com/microsoft/vcpkg)
- xmake-repo：fork [xmake-io/xmake-repo](https://github.com/xmake-io/xmake-repo)

---

## 首次发布到 vcpkg 官方 ports

### 1. 准备 port 文件

本仓库的 `ports/randx/` 目录已就绪：
- [`vcpkg.json`](ports/randx/vcpkg.json) — port manifest
- [`portfile.cmake`](ports/randx/portfile.cmake) — 安装脚本（含 v1.3.1 tarball 的 SHA512）

### 2. 复制到 vcpkg fork

```powershell
git clone https://github.com/<your-gh-user>/vcpkg.git
cd vcpkg

# 复制本仓库的 port 到 fork
Copy-Item -Recurse <randx-repo>/ports/randx ports/randx

# Bootstrap vcpkg（首次）
.\bootstrap-vcpkg.bat -disableMetrics
```

### 3. 生成版本记录

vcpkg 要求每个 port 在 `versions/` 下登记版本信息：

```powershell
# 自动生成 versions/r-/randx.json 并更新 versions/baseline.json
.\vcpkg.exe x-add-version randx
```

参考模板：[`packaging/vcpkg/versions/`](packaging/vcpkg/versions/)

### 4. 本地验证

```powershell
.\vcpkg.exe install randx --classic --overlay-ports=ports --triplet x64-windows

# 期望输出：
# -- Installing: include/RandX.hpp
# -- Installing: include/RandX_Cpp17.hpp
# All requested installations completed successfully
```

> `--classic` 必须加：本仓库根目录的 `vcpkg.json` 会让 vcpkg 误入 manifest mode。

### 5. 提交 PR

```powershell
git checkout -b add-randx-1.3.1
git add ports/randx versions/r-/randx.json versions/baseline.json
git commit -m "New port: randx/1.3.1"
git push origin add-randx-1.3.1
```

在 GitHub 上向 `microsoft/vcpkg:master` 发起 PR。等待维护者 review（通常 1~5 天）。

---

## 首次发布到 xmake-repo

### 1. 准备 port 文件

本仓库的 `packaging/xmake-repo/packages/r/randx/xmake.lua` 已就绪：
- 含跨平台 OS 熵源链接（Windows `bcrypt` / macOS `Security`）
- 含 `on_test` snippet（C++23 编译验证）
- 含 v1.3.1 tarball 的 SHA256

### 2. 复制到 xmake-repo fork

```powershell
git clone https://github.com/<your-gh-user>/xmake-repo.git
cd xmake-repo

# 复制本仓库的 port 到 fork（标准目录结构 packages/r/randx/）
New-Item -ItemType Directory -Force -Path packages/r/randx
Copy-Item <randx-repo>/packaging/xmake-repo/packages/r/randx/xmake.lua packages/r/randx/xmake.lua
```

### 3. 本地验证

```powershell
# 注册本地 fork 作为仓库
xrepo add-repo local-randx "$PWD"

# 安装并触发 on_test
xrepo install -y randx

# 期望输出：=> install randx 1.3.1 .. ok
```

### 4. 提交 PR

```powershell
git checkout -b add-randx-1.3.1
git add packages/r/randx/xmake.lua
git commit -m "Add package randx 1.3.1"
git push origin add-randx-1.3.1
```

在 GitHub 上向 `xmake-io/xmake-repo:master` 发起 PR。

---

## 后续版本发布

每次发布新版本（如 v1.4.0）的完整流程：

### 步骤 1：本地准备

```powershell
# 1.1 在 master 上更新版本号（4 处）
# - CMakeLists.txt        : project(RandX VERSION 1.4.0 LANGUAGES CXX)
# - vcpkg.json            : "version": "1.4.0"
# - ports/randx/vcpkg.json: "version": "1.4.0"
# - packaging/xmake-repo/packages/r/randx/xmake.lua: add_versions("1.4.0", "<sha256>")

# 1.2 提交并打 tag
git add CMakeLists.txt vcpkg.json ports/randx/ packaging/
git commit -m "release: v1.4.0"
git tag v1.4.0
git push origin master
git push origin v1.4.0
```

### 步骤 2：等待 CI 验证

推送 `v*` tag 会自动触发 [Packaging Validation](.github/workflows/packaging-validation.yml) 工作流：

- **vcpkg-validate**（windows-latest）：用 overlay port 安装 + 验证头文件
- **xrepo-validate**（ubuntu-24.04）：注册本地 xmake-repo + 安装 + 触发 on_test

两个 job 都通过后才进入下一步。失败时修复后再推送新 tag（如 `v1.4.1`）。

### 步骤 3：计算新版本的 SHA 哈希

vcpkg 需要 SHA512，xmake-repo 需要 SHA256：

```powershell
# 等待 GitHub codeload 传播（推 tag 后 1~2 分钟）
$tmp = "$env:TEMP\randx-v1.4.0.tar.gz"
Invoke-WebRequest "https://codeload.github.com/lidaixingchen/RandX/tar.gz/refs/tags/v1.4.0" `
    -OutFile $tmp -UseBasicParsing

# 计算 SHA512（写入 ports/randx/portfile.cmake）
(Get-FileHash $tmp -Algorithm SHA512).Hash.ToLower()

# 计算 SHA256（写入 packaging/xmake-repo/packages/r/randx/xmake.lua）
(Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
```

### 步骤 4：向 vcpkg fork 提交版本更新 PR

```powershell
cd <vcpkg-fork>
git pull origin master  # 同步上游

# 更新 port
Copy-Item -Recurse <randx-repo>/ports/randx/* ports/randx/

# 生成新版本记录（追加到 versions/r-/randx.json + 更新 baseline.json）
.\vcpkg.exe x-add-version randx

git checkout -b bump-randx-1.4.0
git add ports/randx versions/r-/randx.json versions/baseline.json
git commit -m "[randx] Update to 1.4.0"
git push origin bump-randx-1.4.0
```

### 步骤 5：向 xmake-repo fork 提交版本更新 PR

```powershell
cd <xmake-repo-fork>
git pull origin master

# 仅更新 xmake.lua（新增 add_versions 行）
Copy-Item <randx-repo>/packaging/xmake-repo/packages/r/randx/xmake.lua `
         packages/r/randx/xmake.lua

git checkout -b bump-randx-1.4.0
git add packages/r/randx/xmake.lua
git commit -m "Update randx to 1.4.0"
git push origin bump-randx-1.4.0
```

### 步骤 6：更新 CHANGELOG

在 [`CHANGELOG.md`](CHANGELOG.md) 顶部追加新版本小节，记录本次发布的变更。

---

## CI 工作流说明

文件：[`.github/workflows/packaging-validation.yml`](.github/workflows/packaging-validation.yml)

**触发条件**：
- 自动：推送 `v*` 格式的 tag
- 手动：在 GitHub Actions 页面用 `workflow_dispatch` 触发，可指定 version

**Jobs**：

| Job | Runner | 验证内容 |
|-----|--------|---------|
| `vcpkg-validate` | windows-latest | port 版本号匹配 tag + `vcpkg install` overlay 成功 + 头文件已安装 |
| `xrepo-validate` | ubuntu-24.04 + GCC 14 | port 版本号匹配 tag + `xrepo install` 成功（含 on_test C++23 编译）|
| `summary` | ubuntu-24.04 | 汇总两个 job 的结果到 GitHub Actions Summary |

**失败常见原因**：
- SHA 不匹配 → tarball 与 tag 不对应，重新计算
- `on_test` 编译失败 → 检查 RandX.hpp 是否引入了新依赖或要求更高的 C++ 标准
- GitHub codeload 502 → 重试或等待几分钟

---

## Checklist

发布新版本时，逐项核对：

- [ ] 4 处版本号已更新（CMakeLists / 根 vcpkg.json / ports/randx/vcpkg.json / xmake.lua）
- [ ] ports/randx/portfile.cmake 的 SHA512 已更新
- [ ] packaging/xmake-repo/packages/r/randx/xmake.lua 的 add_versions 已新增
- [ ] CHANGELOG.md 已追加新版本小节
- [ ] 本地 `vcpkg install randx --classic --overlay-ports=ports` 通过
- [ ] 本地 `xrepo install randx` 通过
- [ ] commit + tag 已推送
- [ ] CI 工作流 Packaging Validation 全绿
- [ ] vcpkg fork PR 已提交
- [ ] xmake-repo fork PR 已提交
