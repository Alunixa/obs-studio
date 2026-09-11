param(
    [Parameter(Mandatory)][string]$Path
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Path) { throw "Refusing to replace an existing image: $Path" }
Add-Type -AssemblyName System.Drawing
$Image = [Drawing.Bitmap]::new(2560, 1440)
$Graphics = [Drawing.Graphics]::FromImage($Image)
$Fonts = @()
try {
    $Graphics.Clear([Drawing.Color]::FromArgb(32, 32, 32))
    $Graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $Graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::None
    $Text = 'OBS 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 录制文字清晰度测试'
    $Y = 16
    foreach ($Size in @(8, 10, 12, 14, 16, 20, 24, 32, 48)) {
        $Font = [Drawing.Font]::new('Microsoft YaHei UI', $Size, [Drawing.FontStyle]::Regular,
            [Drawing.GraphicsUnit]::Pixel)
        $Fonts += $Font
        $Graphics.DrawString("${Size}px $Text", $Font, [Drawing.Brushes]::White, 16, $Y)
        $Graphics.DrawString("${Size}px $Text", $Font, [Drawing.Brushes]::Cyan, 16, $Y + $Size + 5)
        $Y += 2 * ($Size + 5) + 8
    }
    $Graphics.FillRectangle([Drawing.Brushes]::White, 0, 650, 1280, 180)
    for ($X = 0; $X -lt 1280; $X += 2) {
        $Graphics.DrawLine([Drawing.Pens]::Black, $X, 650, $X, 829)
        $Graphics.DrawLine([Drawing.Pens]::Red, 1280 + $X, 650, 1280 + $X, 829)
        $Graphics.DrawLine([Drawing.Pens]::Cyan, 1281 + $X, 650, 1281 + $X, 829)
    }
    for ($Y = 840; $Y -lt 1010; $Y++) {
        $Pen = if ($Y % 2) { [Drawing.Pens]::White } else { [Drawing.Pens]::Black }
        $Graphics.DrawLine($Pen, 0, $Y, 2559, $Y)
    }
    for ($Y = 1020; $Y -lt 1440; $Y += 4) {
        for ($X = 0; $X -lt 2560; $X += 4) {
            $Brush = if ((($X / 4) + ($Y / 4)) % 2) { [Drawing.Brushes]::White } else {
                [Drawing.Brushes]::Black
            }
            $Graphics.FillRectangle($Brush, $X, $Y, 4, 4)
        }
    }
    $Image.Save([IO.Path]::GetFullPath($Path), [Drawing.Imaging.ImageFormat]::Png)
} finally {
    foreach ($Font in $Fonts) { $Font.Dispose() }
    $Graphics.Dispose()
    $Image.Dispose()
}
Write-Output "Created native 2560x1440 text, one-pixel lines and chroma detail chart: $Path"
