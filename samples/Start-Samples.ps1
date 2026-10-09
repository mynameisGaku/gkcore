param(
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$viewerPath = Join-Path $repositoryRoot 'build\runtime-windows\Release\gkcore_model_viewer.exe'
$solutionPath = Join-Path $repositoryRoot 'build\runtime-windows\gkcore.slnx'
if (-not (Test-Path -LiteralPath $solutionPath -PathType Leaf))
{
    $solutionPath = Join-Path $repositoryRoot 'build\runtime-windows\gkcore.sln'
}
$yumekaPath = Join-Path $repositoryRoot 'build\local-assets\Yumeka\FBX\Yumeka_v1.0.4.fbx'
$yumekaMaterialsPath = Join-Path $repositoryRoot 'build\local-assets\Yumeka\Prepared\material-config.txt'
$monkeyPath = Join-Path $env:USERPROFILE 'Downloads\monkey.obj'
$cesiumPath = Join-Path $repositoryRoot 'build\local-assets\CesiumMan\CesiumMan.png.glb'
$unrealDefinitionsPath = Join-Path $repositoryRoot 'build\local-assets\UnrealModels\viewer-models.json'
$unrealDefinitionsLoaded = $false
$unrealDefinitionsMissing = $false
$unrealDefinitionsError = $null
$unrealDefinitions = @()

function Get-MotionPath([string]$fileName)
{
    $preferredPath = Join-Path $env:USERPROFILE ("Downloads\{0}" -f $fileName)
    if (Test-Path -LiteralPath $preferredPath -PathType Leaf)
    {
        return (Resolve-Path -LiteralPath $preferredPath).Path
    }
    if ($CheckOnly)
    {
        Write-Host ("確認できません: Downloads内に {0} がありません。" -f $fileName)
        return $null
    }
    while ($true)
    {
        $enteredPath = Read-Host ("Downloads内に {0} がありません。ファイルのフルパスを入力してください（空欄で中止）" -f $fileName)
        if ([string]::IsNullOrWhiteSpace($enteredPath))
        {
            return $null
        }
        if (Test-Path -LiteralPath $enteredPath -PathType Leaf)
        {
            return (Resolve-Path -LiteralPath $enteredPath).Path
        }
        Write-Host 'そのファイルは見つかりません。パスを確認してください。'
    }
}

function Test-RequiredFile([string]$label, [string]$path)
{
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        Write-Host ("不足: {0} を確認できません。{1}" -f $label, $path)
        return $false
    }
    Write-Host ("確認: {0} -> {1}" -f $label, (Resolve-Path -LiteralPath $path).Path)
    return $true
}

function Test-MaterialConfig([string]$path, [string]$label = 'YUMEKA')
{
    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        Write-Host ("不足: {0}の材質設定がありません。{1}" -f $label, $path)
        return $false
    }
    $valid = $true
    foreach ($line in Get-Content -LiteralPath $path)
    {
        if ($line -match '^\s*(#|$)')
        {
            continue
        }
        # 材質設定行から数値7項目の後ろに続く画像pathを取り出す。
        if ($line -notmatch '^\s*\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$')
        {
            Write-Host ("不正な材質設定行: {0}" -f $line)
            $valid = $false
            continue
        }
        $texturePath = $Matches[1]
        if (-not (Test-Path -LiteralPath $texturePath -PathType Leaf))
        {
            Write-Host ("不足: {0}の材質画像がありません。{1}" -f $label, $texturePath)
            $valid = $false
        }
    }
    if ($valid)
    {
        Write-Host ("確認: {0}の材質設定と画像 -> {1}" -f $label, (Resolve-Path -LiteralPath $path).Path)
    }
    return $valid
}

function Get-UnrealModelDefinition([string]$label)
{
    if (-not $script:unrealDefinitionsLoaded)
    {
        $script:unrealDefinitionsLoaded = $true
        if (-not (Test-Path -LiteralPath $script:unrealDefinitionsPath -PathType Leaf))
        {
            $script:unrealDefinitionsMissing = $true
        }
        else
        {
            try
            {
                $parsedDefinitions = ConvertFrom-Json -InputObject (Get-Content -LiteralPath $script:unrealDefinitionsPath -Raw)
                $script:unrealDefinitions = @($parsedDefinitions | ForEach-Object { $_ })
            }
            catch
            {
                $script:unrealDefinitionsError = $_.Exception.Message
            }
        }
    }
    if ($script:unrealDefinitionsMissing)
    {
        Write-Host ("未準備: {0}のモデル定義がありません。{1}" -f $label, $script:unrealDefinitionsPath)
        return $null
    }
    if ($script:unrealDefinitionsError)
    {
        Write-Host ("未準備: モデル定義を読み込めません。{0} ({1})" -f $script:unrealDefinitionsPath, $script:unrealDefinitionsError)
        return $null
    }
    $definition = $script:unrealDefinitions | Where-Object { $_.label -eq $label } | Select-Object -First 1
    if (-not $definition)
    {
        Write-Host ("未準備: {0}のモデル定義がありません。{1}" -f $label, $script:unrealDefinitionsPath)
        return $null
    }
    return $definition
}

function Get-UnrealViewerArguments($definition, [string]$firstMotionPath, [string]$secondMotionPath)
{
    $center = @($definition.center)
    if ([string]::IsNullOrWhiteSpace([string]$definition.model) -or [string]::IsNullOrWhiteSpace([string]$definition.materials) -or [string]::IsNullOrWhiteSpace($firstMotionPath) -or [string]::IsNullOrWhiteSpace($secondMotionPath) -or $center.Length -ne 3)
    {
        throw 'モデル定義のmodel、materials、center、外部motionの指定が不正です。'
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $scale = ([double]$definition.scale).ToString('R', $culture)
    $centerX = ([double]$center[0]).ToString('R', $culture)
    $centerY = ([double]$center[1]).ToString('R', $culture)
    $centerZ = ([double]$center[2]).ToString('R', $culture)
    return @([string]$definition.model, $scale, $centerX, $centerY, $centerZ, 'external-blend', $firstMotionPath, $secondMotionPath, '--materials', [string]$definition.materials)
}

function Format-ViewerCommand([string[]]$arguments)
{
    $quotedArguments = foreach ($argument in $arguments)
    {
        '"' + $argument.Replace('"', '\"') + '"'
    }
    return '"' + $viewerPath + '" ' + ($quotedArguments -join ' ')
}

function Invoke-Viewer([string]$label, [string[]]$arguments)
{
    if (-not (Test-RequiredFile 'モデルviewer' $viewerPath))
    {
        return $false
    }
    $pathsToCheck = @($arguments[0])
    if ($arguments.Count -ge 7 -and ($arguments[5] -eq 'external' -or $arguments[5] -eq 'external-blend'))
    {
        $motionCount = if ($arguments[5] -eq 'external') { 1 } else { 2 }
        $pathsToCheck += $arguments[6..(5 + $motionCount)]
    }
    $materialsOptionIndex = [Array]::IndexOf($arguments, '--materials')
    if ($materialsOptionIndex -ge 0)
    {
        $pathsToCheck += $arguments[$materialsOptionIndex + 1]
    }
    foreach ($path in $pathsToCheck)
    {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf))
        {
            Write-Host ("不足: {0}" -f $path)
            return $false
        }
    }
    if ($materialsOptionIndex -ge 0 -and -not (Test-MaterialConfig $arguments[$materialsOptionIndex + 1] $label))
    {
        return $false
    }
    Write-Host ("選択: {0}" -f $label)
    Write-Host (Format-ViewerCommand $arguments)
    if ($CheckOnly)
    {
        return $true
    }
    $nativeArguments = ($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
    $process = Start-Process -FilePath $viewerPath -ArgumentList $nativeArguments -WorkingDirectory (Split-Path -Parent $viewerPath) -PassThru -Wait
    return ($process.ExitCode -eq 0)
}

Write-Host 'gkcore サンプルを1つ選んでください。'
Write-Host ("Visual Studio solution: {0} (Release / x64 / v142)" -f $solutionPath)
Write-Host '  1. YUMEKAを静止表示（用意済み基本色画像を使用）'
Write-Host '  2. YUMEKAでSilly Dancing + Capoeiraを外部blend'
Write-Host '  3. monkey.objを正面表示'
Write-Host '  4. Cesium Manを静止表示（ローカル検証素材）'
Write-Host '  5. Sci-Fi TrooperでMixamo motionをblend（ローカル検証素材）'
Write-Host '  6. Clown MonsterでMixamo motionをblend（ローカル検証素材）'
Write-Host '  0. 終了'
if (-not (Test-RequiredFile 'Visual Studio solution' $solutionPath))
{
    exit 1
}
if ($CheckOnly)
{
    $allChecksPassed = $true
    $allChecksPassed = (Invoke-Viewer 'YUMEKA 静止表示' (@($yumekaPath, '1.1166145684', '0', '0.671678712', '-0.2517482195', '--materials', $yumekaMaterialsPath))) -and $allChecksPassed
    $sillyPath = Get-MotionPath 'Silly Dancing.fbx'
    $capoeiraPath = Get-MotionPath 'Capoeira.fbx'
    if ($sillyPath -and $capoeiraPath)
    {
        $allChecksPassed = (Invoke-Viewer 'YUMEKA 外部motion blend' (@($yumekaPath, '1.1166145684', '0', '0.671678712', '-0.2517482195', 'external-blend', $sillyPath, $capoeiraPath, '--materials', $yumekaMaterialsPath))) -and $allChecksPassed
    }
    else
    {
        $allChecksPassed = $false
    }
    $allChecksPassed = (Invoke-Viewer 'monkey.obj 正面表示' (@($monkeyPath, '0.5609934521', '-0.014946', '0.0079755', '-0.0313325', 'front'))) -and $allChecksPassed
    $allChecksPassed = (Invoke-Viewer 'Cesium Man 静止表示' (@($cesiumPath, '0.9956520475', '0', '0.753275105', '0.024976999', 'static'))) -and $allChecksPassed
    if (Test-Path -LiteralPath $unrealDefinitionsPath -PathType Leaf)
    {
        foreach ($definitionLabel in @('SciFITrooper', 'ClownMonster'))
        {
            $displayLabel = if ($definitionLabel -eq 'SciFITrooper') { 'Sci-Fi Trooper' } else { 'Clown Monster' }
            $definition = Get-UnrealModelDefinition $definitionLabel
            if ($definition)
            {
                if ($sillyPath -and $capoeiraPath)
                {
                    try
                    {
                        $allChecksPassed = (Invoke-Viewer ("{0} Mixamo blend" -f $displayLabel) (Get-UnrealViewerArguments $definition $sillyPath $capoeiraPath)) -and $allChecksPassed
                    }
                    catch
                    {
                        Write-Host ("不正なモデル定義: {0} ({1})" -f $definitionLabel, $_.Exception.Message)
                        $allChecksPassed = $false
                    }
                }
                else
                {
                    $allChecksPassed = $false
                }
            }
            else
            {
                $allChecksPassed = $false
            }
        }
    }
    else
    {
        Write-Host ("未準備: Unrealモデル定義がないため、新しい2項目は確認を省略します。{0}" -f $unrealDefinitionsPath)
    }
    if ($allChecksPassed)
    {
        Write-Host 'すべてのファイルと起動引数を確認しました。viewerは起動していません。'
        exit 0
    }
    exit 1
}
$selection = Read-Host '番号'
if ($selection -eq '0')
{
    exit 0
}

$yumekaArguments = @($yumekaPath, '1.1166145684', '0', '0.671678712', '-0.2517482195')
$result = $false
switch ($selection)
{
    '1'
    {
        $result = Invoke-Viewer 'YUMEKA 静止表示' ($yumekaArguments + @('--materials', $yumekaMaterialsPath))
    }
    '2'
    {
        $sillyPath = Get-MotionPath 'Silly Dancing.fbx'
        $capoeiraPath = Get-MotionPath 'Capoeira.fbx'
        if ($sillyPath -and $capoeiraPath)
        {
            $blendArguments = $yumekaArguments + @('external-blend', $sillyPath, $capoeiraPath, '--materials', $yumekaMaterialsPath)
            $result = Invoke-Viewer 'YUMEKA 外部motion blend' $blendArguments
        }
    }
    '3'
    {
        $monkeyArguments = @($monkeyPath, '0.5609934521', '-0.014946', '0.0079755', '-0.0313325', 'front')
        $result = Invoke-Viewer 'monkey.obj 正面表示' $monkeyArguments
    }
    '4'
    {
        $cesiumArguments = @($cesiumPath, '0.9956520475', '0', '0.753275105', '0.024976999', 'static')
        $result = Invoke-Viewer 'Cesium Man 静止表示' $cesiumArguments
    }
    '5'
    {
        $definition = Get-UnrealModelDefinition 'SciFITrooper'
        if ($definition)
        {
            $sillyPath = Get-MotionPath 'Silly Dancing.fbx'
            $capoeiraPath = Get-MotionPath 'Capoeira.fbx'
            if ($sillyPath -and $capoeiraPath)
            {
                try
                {
                    $result = Invoke-Viewer 'Sci-Fi Trooper Mixamo blend' (Get-UnrealViewerArguments $definition $sillyPath $capoeiraPath)
                }
                catch
                {
                    Write-Host ("不正なモデル定義です: {0}" -f $_.Exception.Message)
                }
            }
        }
    }
    '6'
    {
        $definition = Get-UnrealModelDefinition 'ClownMonster'
        if ($definition)
        {
            $sillyPath = Get-MotionPath 'Silly Dancing.fbx'
            $capoeiraPath = Get-MotionPath 'Capoeira.fbx'
            if ($sillyPath -and $capoeiraPath)
            {
                try
                {
                    $result = Invoke-Viewer 'Clown Monster Mixamo blend' (Get-UnrealViewerArguments $definition $sillyPath $capoeiraPath)
                }
                catch
                {
                    Write-Host ("不正なモデル定義です: {0}" -f $_.Exception.Message)
                }
            }
        }
    }
    default
    {
        Write-Host '選択番号が正しくありません。START.batをもう一度実行してください。'
        exit 2
    }
}
if (-not $result)
{
    exit 1
}
exit 0
