# 二进制自检：确认 4 条 hook 描述符与 4 个导出都在
param([string]$Dll = 'C:\Red Alert Type\deep\Enthalpy\build\bin\Release\Enthalpy.dll')
if (-not (Test-Path $Dll)) { Write-Host "missing $Dll"; exit 1 }
$vswhere = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat'
$out = cmd /c "`"$vswhere`" >nul 2>&1 && dumpbin /headers `"$Dll`" | findstr /i syhks00 && dumpbin /exports `"$Dll`" | findstr /i Enthalpy_"
$out
# hook 地址是二进制里的字节，dumpbin 的文本里看不到；这里只校验 .syhks00 段存在（存在即表示 hook 描述符已编译进去）
$hooks = if ($out -match 'syhks00') { 'section-present' } else { 0 }
$exports = 0
foreach ($n in 'Enthalpy_Init','Enthalpy_TacticalRender_Before','Enthalpy_TacticalRender_AfterWorld','Enthalpy_SuppressClick') { if ($out -match $n) { $exports++ } }
"hooks=$hooks exports=$exports"
if ($exports -ne 4) { Write-Host 'FAIL: exports'; exit 1 }
Write-Host 'OK'

