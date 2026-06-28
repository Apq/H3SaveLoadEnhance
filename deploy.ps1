$dst = 'D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\人性化读档'
$src = "$PSScriptRoot\Release"

if (-not (Test-Path $dst)) {
    New-Item -ItemType Directory -Path $dst -Force | Out-Null
}

$dll = Join-Path $src 'SaveLoadEnhance.dll'
if (-not (Test-Path $dll)) {
    throw "未找到编译输出：$dll"
}

Copy-Item $dll $dst -Force
# INI 仅首次部署，避免覆盖游戏目录中已修改的配置
#if (-not (Test-Path "$dst\SaveLoadEnhance.ini")) {
    Copy-Item "$PSScriptRoot\SaveLoadEnhance.ini" $dst
#}

Write-Host "已部署到 $dst"
Write-Host "来源 DLL: $dll"
