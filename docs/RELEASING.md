# 发布流程

本指南面向 RandX 维护者，描述如何将新版本发布到三个发布渠道。

## 共同源码维护与发布检查

维护入口为 `src/header_sources/targets.json` 及其引用的 `.inc` 文件。修改公共定义时编辑 `common/` 来源；修改标准约束、专属能力或适配时编辑对应的 `cpp23/`、`cpp17/` 来源。生产头文件由来源拼装生成，两个版本的来源和受影响产物必须在同一提交中更新。归属与声明上下文见 [共同源码迁移清单](共同源码迁移清单.md)。

在 Miniconda 环境中使用 Python 3.12 或以上版本执行维护检查：

```powershell
python -X utf8 tools/generate_headers.py --write
python -X utf8 -m unittest discover -s tools -p 'test_*header*.py' -v
python -X utf8 tools/generate_headers.py --check
```

单元测试由 `tools/test_generate_headers.py` 和 `tools/test_header_sources.py` 提供，覆盖生成规则、命令行为、逐行映射、两版声明与完整正文的双向定位，以及来源可达性和共同归属。配置中的目标名和输出路径必须唯一；生成器在写入前拒绝重复映射名、规范化后重合或 Windows 大小写冲突的输出路径。`targets.json` 与 `@randx-include` 使用 POSIX 相对路径和 `/` 分隔符，反斜杠会被拒绝；来源映射也使用 POSIX 相对路径，因此在 Windows 和 Linux 上格式一致。

编译诊断继续指向实际消费的 `RandX.hpp` 或 `RandX_Cpp17.hpp` 行号。需要定位维护来源时，执行 `python -X utf8 tools/generate_headers.py --map-dir build/header-maps`；每个目标的 JSON 中，`mappings` 数组记录目标行区间、来源路径和来源行区间。目标行 `L` 对应来源行 `source_start + L - target_start`；按来源路径和来源行筛选全部映射区间，可找到该来源在两个目标中的每次展开位置。临时生成产物可写到 `python -X utf8 tools/generate_headers.py --output-dir build/generated-headers` 指定的目录。

需要验证编译诊断映射时，运行维护演练命令：

```powershell
python -X utf8 tools/check_header_maintenance.py `
  --compiler g++ `
  --artifact-dir build/header-maintenance
```

`--compiler` 指向支持 `-std=c++17`、`-std=c++23`、`-fsyntax-only` 和 `-I` 参数的 GCC/Clang 兼容编译器。该命令在隔离副本中演练共享几何分布正文，分别检查 C++17 与 C++23 的实际错误行能否映射回来源，再恢复副本并比较两个产物字节。每次使用新的产物父目录；其 `header-maintenance-drill` 子目录必须尚不存在，命令不会覆盖已有演练目录。

显式 CMake 维护目标通过 `-DRANDX_ENABLE_HEADER_TOOLS=ON` 启用：`randx_generate_headers` 写入产物，`randx_check_headers` 运行上述两份头文件维护单元测试及一致性检查。普通配置、构建和安装直接使用已提交产物，生成工具默认关闭，消费者无需 Python。

发布前必须执行生成检查、现有完整测试和发布渠道验收；tag 同时保留维护来源、生成器和正确产物。源码归档保留 `src/header_sources/`，安装包继续交付两份独立头文件与 CMake 配置。生成检查 job 名为 `Header generation checks (Ubuntu)` 和 `Header generation checks (Windows)`；当前 GitHub 分支规则尚未将这些状态配置为合并必需检查。

## 渠道概览

| 渠道 | 适用用户 | 提交方式 | 状态 |
|------|---------|---------|------|
| **vcpkg 官方 ports** | CMake / MSBuild 用户（最广） | PR 到 microsoft/vcpkg | 待首发 |
| **xmake-repo** | xmake 用户 | PR 到 xmake-io/xmake-repo | 待首发 |
| **CMake FetchContent** | 满足项目 CMake 最低版本的用户 | 无需提交，靠 Git tag 自动可用 | 已可用 |

> 上游 registry 通过 PR 审核接收包定义。CI 工作流 [`packaging-validation.yml`](../.github/workflows/packaging-validation.yml) 做打包预演，验证 port 能否构建。

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
- [`vcpkg.json`](../ports/randx/vcpkg.json) — 包清单
- [`portfile.cmake`](../ports/randx/portfile.cmake) — 发布包安装脚本

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

参考模板：[`packaging/vcpkg/versions/`](../packaging/vcpkg/versions/)

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
git checkout -b add-randx-1.4.2
git add ports/randx versions/r-/randx.json versions/baseline.json
git commit -m "New port: randx/1.4.2"
git push origin add-randx-1.4.2
```

在 GitHub 上向 `microsoft/vcpkg:master` 发起 PR。等待维护者 review（通常 1~5 天）。

---

## 首次发布到 xmake-repo

### 1. 准备 port 文件

本仓库的 `packaging/xmake-repo/packages/r/randx/xmake.lua` 已就绪：
- 含跨平台 OS 熵源链接（Windows `bcrypt` / macOS `Security`）
- 含 `on_test` snippet（C++23 编译验证）
- 含 v1.4.2 tarball 的 SHA256

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

# 期望输出：=> install randx 1.4.2 .. ok
```

### 4. 提交 PR

```powershell
git checkout -b add-randx-1.4.2
git add packages/r/randx/xmake.lua
git commit -m "Add package randx 1.4.2"
git push origin add-randx-1.4.2
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

在 GitHub Actions 页面用 `workflow_dispatch` 触发[打包验证](../.github/workflows/packaging-validation.yml)工作流（可指定 version 参数）：

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

在[变更记录](../CHANGELOG.md)顶部追加新版本小节，记录本次发布的变更。

---

## CI 工作流说明

文件：[打包验证工作流](../.github/workflows/packaging-validation.yml)

**触发条件**：
- 手动：在 GitHub Actions 页面用 `workflow_dispatch` 触发，可指定 version 参数

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
