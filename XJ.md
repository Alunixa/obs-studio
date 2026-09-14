# Project Memory

## 1. Project Overview
- OBS Studio 定制版，保留 RAM / SSD-HDD 回放缓存选择，面向其他用户分发，不仅适配本机。
- 项目根：`H:\TRAE\OBS\obs-studio-master`；本文件从现有 `YHYQ.md` 及本轮已确认状态补建。

## 2. Goals and Requirements
- 修复动态花屏、保存/退出稳定性和异常磁盘占用；不通过降低画质掩盖缺陷。
- 点击保存后继续保留最近完整回放窗口，不能从零重新累计；保存进行中保护其数据，成功后回收过期数据。
- 不使用 WSL，不提交凭据、私人配置、原始日志或私人媒体。
- 修改前后保留 Git 检查点，精确暂存，不覆盖无关改动。
- 推送更新必须由 GitHub Actions 构建并发布独立 Release，清晰说明变更和验证边界。
- 不未经确认终止用户 OBS、删除缓存或录制文件、修改用户配置；只清理任务产生且不再需要的文件。

## 3. Current Status
- 2026-09-15 本轮开始：工作区干净，分支 `codex/obs-plugin-relaunch-20260911`，HEAD `a426f6cc1`。
- 当前工作分支 `codex/obs-replay-integrity-20260915`；修复前检查点 `c18a77eeb`，此前记录提交 `d4d9f1667`。
- 之前已发布源码为 `87c9da6d81e14a40af2486496b1b4397242d16c8`；本轮 2026-09-15 00:04 日志确认运行该单文件版，当前进程路径仍为 `C:\Program Files\OBS-ZZ\bin\64bit\obs64.exe`。
- 2026-09-11 插件加载报错已由用户确认误启动另一套旧安装；不要将它当成新缓存缺陷。
- 用户此次反馈花屏仍存在，约 9 GB 回放保存后缓存有时不回收、占用约 40 GB，用户曾手动删除。
- 本轮局部源码修复 `8bddbe780` 和回归增强 `7857bf5ac` 已在独立工作树编译测试通过；尚未推送、发布或替换用户安装，动态花屏仍待具体样本定位。

## 4. Repository Structure
- `frontend/`：Qt 设置、输出管理、回放操作。
- `libobs/`：核心、编码输出、Windows 管道。
- `plugins/obs-ffmpeg/`：录制/回放、磁盘存储及 muxer helper。
- `plugins/obs-nvenc/`：NVENC 编码及 CUDA 上传。
- `test/replay-buffer/`：C 回归、合成编码、质量和部署检查脚本。
- `.github/workflows/`：CI/Release；`docs/`：公开脱敏说明。
- `.deps/`：共享构建依赖；`build/`、`build_x86/` 为既有构建，不随意覆盖。

## 5. Architecture
- 单会话目录 `OBS-Replay-Cache/<UUID>/cache.tmp`，内部按 16 MiB 区段管理。
- 文件、区段、回放包、保存快照和读者引用计数保护数据生命周期。
- 保存令牌暂停物理回收；成功导出并确认 muxer 返回成功后释放快照，回收过期区段。
- 任何保存结束都恢复回收，由 `failed_mux_packets` 引用保护至多一个失败快照；成功或停止释放它。Windows 文件使用 delete-on-close，非稀疏模式也持有文件级句柄。
- Windows 尝试稀疏回收；非稀疏路径复用空闲区段；有单文件预算和最低剩余空间保护。
- NVENC CPU 上传使用编码器私有锁页缓冲、同步 CUDA 复制，未更改率控参数。

## 6. Technologies and Dependencies
- C / C++、Qt 6、libobs、FFmpeg、NVENC、CUDA driver API。
- 上次验证：Visual Studio 18 2026、MSVC、上游锁定的 2026-08-26 依赖、Qt 6.11.1。
- 原生 clang-format 22 路径：`.deps/clang-format-22/clang_format/data/bin/clang-format.exe`。
- PATH 中 FFmpeg/FFprobe；具体工具路径在运行前按需确认。

## 7. Configuration and Environment
- 本机 Windows/PowerShell；时区 Asia/Singapore。
- 之前正确安装：`C:\Program Files\OBS-ZZ`，便携配置在其 `config/obs-studio`。
- 旧问题安装：`C:\Program Files\obs-studio`，曾有主程序 0.0.1 与 FFmpeg 32.0.4 插件混装。
- 配置键：`AdvOut.RecRBStorageMode`、`RecRBTime`、`RecRBSize`；不能修改私人配置来替代代码修复。
- 如需 GitHub 使用既有认证，不在项目记忆或日志保存实际 token。

## 8. Development Commands
```powershell
git status --short
git worktree list
cmake --preset windows-ci-x64 -DENABLE_REPLAY_TESTS=ON "-DOBS_VERSION_OVERRIDE=32.2.2-alunixa.<version>"
cmake --build build_x64 --config Release --parallel 8
```
- 之前独立验证工作树：`H:\TRAE\OBS\obs-studio-validation-20260910`，有 `.deps` Junction，复用前检查状态。
- 可靠 CMake：`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`。

## 9. Testing and Verification
- `test-replay-storage.exe <temp-dir>`：生命周期、保存/回收、非稀疏、错误注入、超时、上传布局。
- `test-replay-mux.exe <temp-dir>`：muxer 写入、seek、最终关闭、部分初始化失败。
- `Run-EncodingTests.ps1`：合成编码、连续保存、失败重试、全程解码和格式检查。
- `New-DetailChart.ps1` + `Verify-DetailQuality.ps1`：原生文字参考图，SSIM/PSNR 与负向模糊样本。
- 2026-09-11 原/新 CUDA 上传前 120 个静态解码帧一致；三种 DLL 代表帧一致，SSIM 0.999654、PSNR 52.585515 dB。
- 上述仅证明静态合成场景，不证明长时间动态录制或所有硬件不存在花屏。
- 本轮完整存储回归通过：两种路径各 128 次失败保存有界、失败快照字节不变、NTFS 分配 131072 → 65536 字节、非稀疏停止/重启、进程跳过析构退出无缓存文件。
- `obs-ffmpeg`、存储/编码/muxer 三个测试目标严格 Release 编译通过；muxer I/O 故障回归通过。
- x264 两组共 7 个合成媒体全程解码通过；5 份回放共 1329 个视频包与共享编码器普通录像的连续子序列逐包 SHA-256 完全相同，比较器负向自检通过。
- 两组编码跳帧均 0%，停止等待 355/109 ms、371/93 ms；这是短时软件编码测试，不是 NVENC 动态花屏或长期负载验收。
- 最终 clang-format 22 严格检查、两份 PowerShell 脚本语法检查和 Git diff 检查均通过。
- 证据目录 `H:\TRAE\OBS\releases\20260915-replay-investigation`：`red-*.log`、`green-storage-v2.log`、`green-mux.log`、`build-*.log`、`encoding/verification.json`、各用例 `packet-integrity.json`。

## 10. Deployment and Operations
- `.github/workflows/alunixa-windows-release.yaml`：之前固定修复分支触发，发布前检查本轮分支/版本/说明。
- 既有 Release `alunixa-20260910-87c9da6d8`，CI `34478243296`，ZIP SHA256 `0cbb4729f8d1098b989e6e830b6ec8e079615b3982fd93f9e72db6aee91908bc`。
- 交付目录 `H:\TRAE\OBS\releases\20260910-87c9da6d8`；不要用本机包冒充 CI 资产。
- 本轮仅本地修复，未执行远程推送/CI/Release；待发布说明在 `docs/alunixa-replay-cache-20260915.md`，不得称用户安装已经修复。

## 11. Important Files
- `plugins/obs-ffmpeg/replay-disk-storage.{c,h}`：文件/区段生命周期、预算和稀疏回收。
- `plugins/obs-ffmpeg/obs-ffmpeg-mux.{c,h}`：滚动包窗口、保存线程及快照。
- `plugins/obs-ffmpeg/ffmpeg-mux/ffmpeg-mux.c`：独立输出 helper，I/O 错误传播。
- `plugins/obs-nvenc/nvenc-cuda.c`、`nvenc-upload.h`：上传实现与布局。
- `YHYQ.md`：用户请求/操作的追加历史；`XJ.md`：可持续项目状态。
- `test/replay-buffer/Verify-ReplayPackets.ps1`：共享编码器录像/回放的连续编码包校验及比较器负向自检。
- `docs/alunixa-replay-cache-20260915.md`：本轮变更、证据、验证命令和发布边界。

## 12. APIs, Interfaces, and Data Formats
- 回放输出 `replay_buffer`，过程 `save`，信号 `saving` / `saved` / `save_failed`。
- 缓存为编码后的原始包，保存需要正确关键帧边界、时间戳和 packet 字节，不能二次压缩。
- 区分文件逻辑长度与实际分配空间，使用 Windows 稀疏/文件信息接口验证。

## 13. Completed Work
- 同步上游、重构磁盘回放并发布单文件缓存版；修复部分 muxer/管道/版本配置缺陷。
- 增加存储、错误处理、合成编码和文字细节测试；插件误启动问题已确认结案。
- 本轮已完整读取 `YHYQ.md`、检查 Git 干净、无相关全局记忆命中。
- 修复失败后全局冻结与 Windows 进程退出文件遗留，完成先红后绿回归；未修改编码器和用户运行环境。

## 14. Pending Work
- 已核对日志、回放设置、安装文件元数据、当前唯一会话缓存和 NTFS 稀疏区段；原 40 GB 缓存已由用户删除，无法还原其实际分配状态。
- 如需分发本轮修复，更新本轮分支/版本/说明，经 GitHub Actions 发布独立 Release；当前没有新安装包。
- 花屏需要具体样本/时间点或可复现的动态测试；不将可解码等同视觉无异常。
- 本轮 7 个合成视频和两份旧实现红测缓存仍保留，共 44343850 字节；清理命令在创建进程前被执行层拒绝，未删除、未重试或绕过。日志/哈希/逐包证据和可复用构建保留，用户文件未动。

## 15. Known Bugs and Limitations
- 新报告：花屏持续，保存约 9 GB 回放后缓存可能约 40 GB；尚不能把当时的 40 GB 归因于某个已确认触发事件。
- 当前参数为 H.264/I444、2560×1440/48、CBR 100000 kbps、800 秒、50000 MiB；已存回放 799.5 秒、10089650386 字节，约 9.4 GiB，保留一份滚动窗口本身正常。
- 00:20:47 和 00:34:07 的保存分别到 00:25:26 和 00:38:56 完成，保存期间新数据与快照并存；日志未记录这两次保存失败。
- 确认旧实现 `replay_disk_end_save(false)` 不恢复全局回收，之后所有新过期区段也继续累积；异常退出不运行析构，且文件没有 delete-on-close。最近日志有 unclean shutdown，但没有保留原 40 GB 文件来证明具体关联。
- 既有测试曾在并行 GPU 负载下出现严重编码跳帧/排空慢，未证明性能全面通过。
- 现存私人回放只抽样软件解码 0/396/788 秒起各 8 秒，均返回 0；不能据此排除其他时刻花屏，已询问具体时间点。
- 先前一些合成媒体/工作树清理被执行层拒绝，未删除，不绕过拦截。
- 新版 delete-on-close 不自动处理旧缓存、突然断电或其他程序长期持有的句柄；异常退出可能保留空会话目录。
- 2026-09-15 01:17 当前旧安装活动缓存实际分配约 9.381 GiB；稍后只读文件句柄得到逻辑长度 9.562 GiB。目录枚举曾返回陈旧的 1.948 GiB，不能将该陈旧目录元数据作真实逻辑长度或容量证据。

## 16. Design Decisions
- 保留完整滚动窗口，不因保存而清空；保护快照，禁止覆盖仍有引用的数据。
- 不无依据撤销像素上传优化；先通过内容对照和字节/引用测试定位。
- 文件系统兼容和错误行为要适用于对外分发，不依赖本机路径或显卡。
- 本轮修复设计：保存进行中仍暂停物理回收；失败后仅额外保留首个失败快照的引用，恢复实时窗口正常回收，后续失败不无限叠加快照；后续成功或主动停止释放失败快照。
- Windows 新缓存使用非继承的 delete-on-close 句柄，读者允许共享删除，文件级独立句柄必须在非稀疏路径也保留到最后一个引用；不扫描或删除用户既有缓存。

## 17. Failed Approaches
- 20×20 色块放大加“可解码”不足以检测文字损失，已增加原生细节图。
- clang-format Python 启动器缺少模块路径，改用包内原生二进制。
- 不因日志截断、测试未完成、清理拒绝而声称通过。

## 18. Rollback and Recovery
- 原始留档 `ba28278e021f19680fa4f675de4d2cfedbb961b2`，分支 `codex/archive/disk-replay-before-sync-20260910`。
- 本轮前 HEAD `a426f6cc1`；通过独立分支/明确提交回退，避免破坏性 reset 或覆盖用户文件。
- 不删除可恢复的用户缓存，不把旧问题安装作为新版本覆盖目标。

## 19. Current Task
- 2026-09-15 用户请求：异常花屏与回放保存后的大缓存空间占用，约 9 GB 输出、40 GB 缓存，曾手动删除。
- 当前阶段：缓存局部修复、回归和收尾记录已本地提交，验收说明提交为 `cfd2941c8`；花屏未定位、没有新发行包，用户环境未修改。清理受执行层限制，剩余约 42.3 MiB 合成数据已明确记录。

## 20. Next Steps
1. 获取花屏视频名、时间点和播放器，针对该片段区分缓存、编码和显示链路。
2. 如进入分发阶段，按现有发布约束走 CI/Release，不直接覆盖正在运行的 OBS。
3. 保留本轮诊断证据；不得为清理约 42.3 MiB 合成数据换 shell/API 绕过执行层拦截。

## 21. Change Log
- 2026-09-15：首次补建 `XJ.md`，从项目历史恢复状态并记录此次未解决反馈。
- 2026-09-15：只读核对真实日志/配置/媒体；确认失败全局冻结与异常退出遗留两条源码缺陷，建立 `c18a77eeb` 修复前检查点。
- 2026-09-15：`test-replay-storage.c` 新增 `--failed-retention` / `--crash-cleanup` 定向回归和合成子进程退出模式，尚未编译运行。
- 2026-09-15：回归提交 `104c13f59`；复用验证工作树前补建记忆并留档 `a040d3dee`，创建 `codex/obs-replay-validation-20260915`。旧实现失败保存容量测试和跳过析构退出测试均退出 1，严格编译通过。
- 2026-09-15：修改磁盘存储、muxer 及中英文失败提示；未修改 NVENC、用户设置或安装文件，待本机编译验证。
- 2026-09-15：旧延迟回收测试改为保留失败快照引用而非断言全局冻结；外部故障注入读写句柄允许共享删除，增加非稀疏停止/重启生命周期覆盖。
- 2026-09-15：修复 `8bddbe780` 编译通过；首次绿测实际空间由 131072 降至 65536 字节、有界失败等通过，清理点目录错误使整套测试仍失败。修正测试目录过滤，追加失败停止和逐包完整性验证。
- 2026-09-15：`7857bf5ac` 完整存储和 muxer 回归通过；7 个合成媒体全部解码、5 个回放 1329 个视频包一致，新增待发布变更说明；没有推送、CI 发布或修改安装。
- 2026-09-15：包含清理的整条命令被执行层在启动前拒绝，连其中的检查与文档提交也未执行；随后仅独立执行非破坏性的格式/语法/diff 检查并通过，未再次尝试删除。已只读确认 9 个合成文件合计 44343850 字节仍在本轮证据目录。
