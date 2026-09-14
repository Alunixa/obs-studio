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
- 本轮待验证修改：任何保存结束都恢复回收，由 `failed_mux_packets` 引用保护至多一个失败快照；成功或停止释放它。Windows 文件使用 delete-on-close，非稀疏模式也持有文件级句柄。
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

## 10. Deployment and Operations
- `.github/workflows/alunixa-windows-release.yaml`：之前固定修复分支触发，发布前检查本轮分支/版本/说明。
- 既有 Release `alunixa-20260910-87c9da6d8`，CI `34478243296`，ZIP SHA256 `0cbb4729f8d1098b989e6e830b6ec8e079615b3982fd93f9e72db6aee91908bc`。
- 交付目录 `H:\TRAE\OBS\releases\20260910-87c9da6d8`；不要用本机包冒充 CI 资产。

## 11. Important Files
- `plugins/obs-ffmpeg/replay-disk-storage.{c,h}`：文件/区段生命周期、预算和稀疏回收。
- `plugins/obs-ffmpeg/obs-ffmpeg-mux.{c,h}`：滚动包窗口、保存线程及快照。
- `plugins/obs-ffmpeg/ffmpeg-mux/ffmpeg-mux.c`：独立输出 helper，I/O 错误传播。
- `plugins/obs-nvenc/nvenc-cuda.c`、`nvenc-upload.h`：上传实现与布局。
- `YHYQ.md`：用户请求/操作的追加历史；`XJ.md`：可持续项目状态。

## 12. APIs, Interfaces, and Data Formats
- 回放输出 `replay_buffer`，过程 `save`，信号 `saving` / `saved` / `save_failed`。
- 缓存为编码后的原始包，保存需要正确关键帧边界、时间戳和 packet 字节，不能二次压缩。
- 区分文件逻辑长度与实际分配空间，使用 Windows 稀疏/文件信息接口验证。

## 13. Completed Work
- 同步上游、重构磁盘回放并发布单文件缓存版；修复部分 muxer/管道/版本配置缺陷。
- 增加存储、错误处理、合成编码和文字细节测试；插件误启动问题已确认结案。
- 本轮已完整读取 `YHYQ.md`、检查 Git 干净、无相关全局记忆命中。

## 14. Pending Work
- 已核对日志、回放设置、安装文件元数据、当前唯一会话缓存和 NTFS 稀疏区段；原 40 GB 缓存已由用户删除，无法还原其实际分配状态。
- 为两个源码缺陷建立回归：失败后全局回收永久暂停、异常退出跳过 unlink 留下文件。
- 为证实缺陷增加先失败后通过的回归，独立编译验证，再决定发布。
- 花屏需要具体样本/时间点或可复现的动态测试；不将可解码等同视觉无异常。

## 15. Known Bugs and Limitations
- 新报告：花屏持续，保存约 9 GB 回放后缓存可能约 40 GB；尚不能把当时的 40 GB 归因于某个已确认触发事件。
- 当前参数为 H.264/I444、2560×1440/48、CBR 100000 kbps、800 秒、50000 MiB；已存回放 799.5 秒、10089650386 字节，约 9.4 GiB，保留一份滚动窗口本身正常。
- 00:20:47 和 00:34:07 的保存分别到 00:25:26 和 00:38:56 完成，保存期间新数据与快照并存；日志未记录这两次保存失败。
- 确认旧实现 `replay_disk_end_save(false)` 不恢复全局回收，之后所有新过期区段也继续累积；异常退出不运行析构，且文件没有 delete-on-close。最近日志有 unclean shutdown，但没有保留原 40 GB 文件来证明具体关联。
- 既有测试曾在并行 GPU 负载下出现严重编码跳帧/排空慢，未证明性能全面通过。
- 现存私人回放只抽样软件解码 0/396/788 秒起各 8 秒，均返回 0；不能据此排除其他时刻花屏，已询问具体时间点。
- 先前一些合成媒体/工作树清理被执行层拒绝，未删除，不绕过拦截。

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
- 当前阶段：修复提交 `8bddbe780` 的 FFmpeg 插件及三个测试目标严格编译通过；缓存回归已通过有界失败、稀疏实际回收、非稀疏生命周期、并发及故障注入，但退出测试末尾清理误包含 os_glob 的点目录导致总体失败，已修正测试筛选，待重跑。
- 新增软件编码连续失败后直接停止的集成用例及 `Verify-ReplayPackets.ps1`，对照共享编码器普通录像逐包 SHA-256 验证回放未改变数据；未运行前不宣称通过。

## 20. Next Steps
1. 加入失败后持续滚动、重复失败、非稀疏和进程异常退出的回归，先在旧实现上证实失败。
2. 修复回收范围和 Windows 文件生命周期，不改变 NVENC/CBR/画质设置。
3. 在既有独立验证工作树进行严格编译和合成编码/包字节一致性验证。
4. 记录验证边界；花屏继续等待具体时间点，未部署前不声称用户安装已修复。

## 21. Change Log
- 2026-09-15：首次补建 `XJ.md`，从项目历史恢复状态并记录此次未解决反馈。
- 2026-09-15：只读核对真实日志/配置/媒体；确认失败全局冻结与异常退出遗留两条源码缺陷，建立 `c18a77eeb` 修复前检查点。
- 2026-09-15：`test-replay-storage.c` 新增 `--failed-retention` / `--crash-cleanup` 定向回归和合成子进程退出模式，尚未编译运行。
- 2026-09-15：回归提交 `104c13f59`；复用验证工作树前补建记忆并留档 `a040d3dee`，创建 `codex/obs-replay-validation-20260915`。旧实现失败保存容量测试和跳过析构退出测试均退出 1，严格编译通过。
- 2026-09-15：修改磁盘存储、muxer 及中英文失败提示；未修改 NVENC、用户设置或安装文件，待本机编译验证。
- 2026-09-15：旧延迟回收测试改为保留失败快照引用而非断言全局冻结；外部故障注入读写句柄允许共享删除，增加非稀疏停止/重启生命周期覆盖。
- 2026-09-15：修复 `8bddbe780` 编译通过；首次绿测实际空间由 131072 降至 65536 字节、有界失败等通过，清理点目录错误使整套测试仍失败。修正测试目录过滤，追加失败停止和逐包完整性验证。
