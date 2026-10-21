#Requires -Version 5.1
<#
.SYNOPSIS
    抓取 PDFium 预编译产物（资料库 PDF 文本层解析，模块 M3-c）。

.DESCRIPTION
    从官方预编译仓库 bblanchon/pdfium-binaries 下载**固定 tag** 的 Windows x64 产物，
    解压到 <仓库根>/third_party/pdfium/。

    该目录已在 .gitignore 中忽略：二进制不进仓库、不进 PR diff。
    本脚本只做下载与解压，不改 CMake 缓存；重新运行后需重新配置 CMake
    （CMakeLists.txt 检测到目录即启用 PDF 文本层，缺失则降级为「PDF 未启用」）。

.PARAMETER Tag
    产物 tag，默认 chromium/8111（已锁定；升级时需同步更新 CMakeLists.txt 注释）。

.PARAMETER Dest
    目标目录，默认 <仓库根>/third_party/pdfium。

.EXAMPLE
    pwsh scripts/fetch-pdfium.ps1
#>
[CmdletBinding()]
param(
    [string]$Tag = 'chromium/8111',
    [string]$Dest
)

$ErrorActionPreference = 'Stop'

if (-not $Dest) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $Dest = Join-Path $repoRoot 'third_party/pdfium'
}

$repo = 'bblanchon/pdfium-binaries'
$assetName = 'pdfium-win-x64.tgz'
$headers = @{ 'User-Agent' = 'meowai-fetch-pdfium' }

# 先按 tag + 资产名解析出资产（下载 url 由 API 给出，含正确签名）
$releaseTag = $Tag -replace '/', '%2F'
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/tags/$releaseTag" -Headers $headers
$asset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1
if (-not $asset) {
    throw "tag $Tag 下未找到资产 $assetName"
}

$archive = Join-Path ([System.IO.Path]::GetTempPath()) $assetName
Write-Host "下载 $repo@$Tag / $assetName（$($asset.size) 字节）"
# 走 API 资产端点而不是浏览器直链：部分网络环境无法直连 github.com 的 302 跳转
Invoke-WebRequest -Uri $asset.url -Headers ($headers + @{ 'Accept' = 'application/octet-stream' }) `
    -OutFile $archive -UseBasicParsing

if (Test-Path $Dest) {
    Remove-Item $Dest -Recurse -Force
}
New-Item -ItemType Directory -Path $Dest -Force | Out-Null

# 压缩包内即为根层级（bin/ include/ lib/ LICENSE VERSION），无需剥离目录
tar -xzf $archive -C $Dest
Remove-Item $archive -Force

foreach ($needed in @('include/fpdfview.h', 'include/fpdf_text.h', 'lib/pdfium.dll.lib', 'bin/pdfium.dll')) {
    if (-not (Test-Path (Join-Path $Dest $needed))) {
        throw "PDFium 产物缺少必要文件：$needed"
    }
}

Write-Host "PDFium 已就位：$Dest"
Get-Content (Join-Path $Dest 'VERSION') | ForEach-Object { Write-Host "上游版本：$_" }
