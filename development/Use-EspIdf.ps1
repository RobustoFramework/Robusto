$ErrorActionPreference = "Stop"

$IdfPath = "C:\esp\v6.0.2\esp-idf"
$IdfPythonEnvPath = "C:\Espressif\python_env\idf6.0_py3.11_env"

if (-not (Test-Path -Path $IdfPath -PathType Container)) {
    throw "ESP-IDF path not found: $IdfPath"
}

if (-not (Test-Path -Path $IdfPythonEnvPath -PathType Container)) {
    throw "ESP-IDF Python environment not found: $IdfPythonEnvPath"
}

$ExportScript = Join-Path -Path $IdfPath -ChildPath "export.ps1"
if (-not (Test-Path -Path $ExportScript -PathType Leaf)) {
    throw "ESP-IDF export script not found: $ExportScript"
}

$env:IDF_PATH = $IdfPath
$env:IDF_PYTHON_ENV_PATH = $IdfPythonEnvPath

if (Test-Path Env:IDF_TARGET) {
    Remove-Item Env:IDF_TARGET
}

. $ExportScript

Write-Host "ESP-IDF environment ready for Robusto. Use: idf.py build"