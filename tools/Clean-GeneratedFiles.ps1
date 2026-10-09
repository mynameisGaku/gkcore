[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# この一覧にある退役済みの生成物だけを整理する。
$archiveNames = @(
    'alignment-green',
    'alignment-negative-check',
    'alignment-red',
    'ci-windows-fixed',
    'ci-windows-repro',
    'emissive-fixture-check',
    'emissive-mask-fixture-recheck',
    'generated-normal-registration-fixtures',
    'model-shader-reflection',
    'normal-negative-shaders',
    'occlusion-registration-fixtures',
    'runtime-debug-native',
    'runtime-debug-windows',
    'runtime-guard-newer',
    'runtime-sdk22621',
    'runtime-windows-sdk22621',
    'tangent-registration-fixtures'
)

# 調査用に作ったprobeの既知ファイル名だけを対象にする。
$probeNames = @(
    'ufbx-root-probe.obj',
    'ufbx.obj',
    'yumeka_node_mesh_probe.c',
    'yumeka_node_mesh_probe.exe',
    'yumeka_node_mesh_probe.obj',
    'yumeka_fbx_cluster_probe.c',
    'yumeka_fbx_cluster_probe.exe',
    'yumeka_fbx_cluster_probe.obj',
    'yumeka_fbx_order_probe.c',
    'yumeka_fbx_order_probe.exe',
    'yumeka_fbx_order_probe.obj',
    'yumeka_material_probe.c',
    'yumeka_material_probe.exe',
    'yumeka_material_probe.obj'
)

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$targets = [Collections.Generic.List[IO.FileSystemInfo]]::new()

function Test-WithinPath([string] $Path, [string] $Parent) {
    $prefix = [IO.Path]::GetFullPath($Parent).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($Path).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-SafeTarget([string] $Path, [string] $RequiredParent, [bool] $IsDirectory) {
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $fullPath = [IO.Path]::GetFullPath($resolved)
    $parentPath = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RequiredParent).Path)
    if (-not (Test-WithinPath $fullPath $parentPath) -or -not (Test-WithinPath $fullPath $buildRoot)) {
        throw "対象が許可範囲の外にあります: $fullPath"
    }

    $targetItem = Get-Item -LiteralPath $fullPath -Force
    if ($IsDirectory -ne $targetItem.PSIsContainer) {
        throw "対象の種類が想定と異なります: $fullPath"
    }
    $current = $targetItem
    while ($null -ne $current) {
        if (($current.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "再解析ポイントを含むため中止します: $($current.FullName)"
        }
        if ($current.Name -eq '.git') {
            throw "Git管理データを含むため中止します: $($current.FullName)"
        }
        if ([IO.Path]::GetFullPath($current.FullName).Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parentName = Split-Path -Parent $current.FullName
        if (-not $parentName) {
            break
        }
        $current = Get-Item -LiteralPath $parentName -Force
    }

    if ($IsDirectory) {
        foreach ($entry in Get-ChildItem -LiteralPath $fullPath -Force -Recurse) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "再解析ポイントを含むため中止します: $($entry.FullName)"
            }
            if ($entry.Name -eq '.git') {
                throw "Git管理データを含むため中止します: $($entry.FullName)"
            }
        }
    }

    return $targetItem
}

# 書き出しが終わった検証用コピーだけを対象にし、元のDownloadsは保持する。
$retiredUnrealProject = Join-Path $buildRoot 'unreal-model-validation-20261009'
if (Test-Path -LiteralPath $retiredUnrealProject)
{
    $item = Assert-SafeTarget $retiredUnrealProject $buildRoot $true
    $targets.Add($item)
}

$archiveRoot = Join-Path $buildRoot 'archive'
$probeRoot = Join-Path $buildRoot 'native-validation'
foreach ($name in $archiveNames) {
    $path = Join-Path $archiveRoot $name
    if (Test-Path -LiteralPath $path) {
        $item = Assert-SafeTarget $path $archiveRoot $true
        $targets.Add($item)
    }
}
foreach ($name in $probeNames) {
    $path = Join-Path $probeRoot $name
    if (Test-Path -LiteralPath $path) {
        $item = Assert-SafeTarget $path $probeRoot $false
        $targets.Add($item)
    }
}

foreach ($target in $targets) {
    if ($PSCmdlet.ShouldProcess($target.FullName, '生成済みscratchファイルを削除')) {
        Remove-Item -LiteralPath $target.FullName -Recurse -Force
    }
}
