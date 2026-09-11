# 2026-09-11 录制画质反馈：已验证的范围与待定位项

## 状态

尚未复现用户报告的严重模糊/文字扭曲，不能宣称产品缺陷已修复。
本轮不修改用户配置、不改变编码质量默认值、不替换安装文件，也不发布未经证实的“画质修复版”。
新增的是能够检查画面细节的测试，而不是把文件可解码当成画质合格。

## 本地证据

- 用户新回放样本：2560×1440、60 fps、HEVC、8-bit 4:4:4、Full range、约 19.02 Mbps 视频码率。
- 旧样本：2560×1440、59.94 fps、H.264、4:2:0、Limited range、约 116.59 Mbps 视频码率。
- 两个样本的录制内容和编码参数不同，因此体积差异不能单独证明代码造成了画质退化。
- 新样本的 HEVC 位流抽样得到 `init_qp_minus26=0`、`slice_qp_delta=-12`，即 QP 14，未发现该样本的 CQP 14 被忽略。
- 原生大小截图、私人样本及配置只保存在本地，未提交或上传。

## 新旧 CUDA 上传实现的控制变量实验

使用 `New-DetailChart.ps1` 生成原生 2560×1440 参考图，其中包含不同字号的中英文文字、
单像素黑白条纹、红青色度条纹和细棋盘图，不再将原先 20×20 随机色块放大后当成细节验证。

共同参数为 2560×1440/60、HEVC/I444、Full range、CQP 14/P5、关闭多遍/前瞻/AQ、无 B 帧、
不缩放参考图，同时录制并进行两次磁盘回放导出。

比较三个目标：

1. 将 CUDA 上传文件恢复到原始定制提交 `ba28278e` 的实现，其余代码和依赖不变。
2. 当前定制的独立锁页暂存缓冲上传实现。
3. 实际 CI 发布包 `87c9da6d8` 的 `obs-nvenc.dll` 副本。

结果：

- 原上传实现和新上传实现的前 120 个解码帧，逐帧哈希完全一致。
- 三个目标在一秒处的解码 PNG 完全一致，SHA-256 为
  `ec4505fa8851ca77df184db6b2febe4167193ad184cfa632a80d52ae5703affb`。
- 解码帧对参考图的 RGB SSIM 为 `0.999654`，PSNR 为 `52.585515 dB`。
- 9 个合成录制/回放文件全程解码通过，测试结束无 `cache.tmp` 残留。
- 本机实际安装的 NVENC DLL 与相应 CI ZIP 中的 DLL 哈希一致，未发现这一组件混装。

这些结果只覆盖固定合成画面和本机硬件，**不排除动态内容、其他硬件、采集来源、
特定时刻或播放器显示链路的问题**。继续定位需要用户指出具体文件、模糊时间点和播放器。

## 可复用的细节回归方法

先以 `ENABLE_REPLAY_TESTS=ON` 构建完整运行时和 `test-replay-encoding`，
并确保 `ffmpeg` / `ffprobe` 在 PATH 中。

```powershell
# 所有输出目录必须是新目录；不抓取桌面或麦克风。
$Runtime = 'X:\obs-build\build_x64\rundir\Release'
$Output = 'X:\obs-detail-test'
New-Item -ItemType Directory -Path $Output
& .\test\replay-buffer\New-DetailChart.ps1 -Path "$Output\Reference.png"
Push-Location "$Runtime\bin\64bit"
try {
    & .\test-replay-encoding.exe $Runtime "$Output\encode" `
        obs_nvenc_hevc_tex I444 1 "detail:$Output\Reference.png"
    if ($LASTEXITCODE) { throw 'Synthetic encoding failed' }
} finally {
    Pop-Location
}
& .\test\replay-buffer\Verify-DetailQuality.ps1 `
    -Reference "$Output\Reference.png" `
    -Recording "$Output\encode\Recording.mkv" `
    -OutputDirectory "$Output\quality"
```

`Verify-DetailQuality.ps1` 检查分辨率/帧率、抽取原生解码帧、计算与输入图的 SSIM/PSNR，
保存机器可读结果和哈希；默认静态细节阈值为 SSIM ≥ 0.995、PSNR ≥ 45 dB。
本轮还用先降至 640×360 再放大的故意模糊视频验证失败路径：
SSIM 为 `0.455791`、PSNR 为 `9.239994 dB`，测试正确拒绝。
此阈值专用于上述静态参考图，不应套用到用户任意视频、HDR 或 4:2:0 色度保真评价。
