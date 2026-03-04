$pluginName = 'reaper_wingconnector.dll'
$configName = 'config.json'
$targetDir = Join-Path $env:APPDATA 'REAPER\UserPlugins'

$pluginPath = Join-Path $targetDir $pluginName
$configPath = Join-Path $targetDir $configName

if (Test-Path $pluginPath) { Remove-Item $pluginPath -Force }
if (Test-Path $configPath) { Remove-Item $configPath -Force }

Write-Host "Removed COLAB.wing.reaper.virtualsoundcheck from $targetDir"
