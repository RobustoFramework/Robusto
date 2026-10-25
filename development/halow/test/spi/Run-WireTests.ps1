param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$VendorSource
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$component = Join-Path $repo 'components\robusto\network\src\media\halow\spi'
$output = Join-Path $PSScriptRoot '.test-build'
New-Item -ItemType Directory -Force $output | Out-Null
$executable = Join-Path $output 'wire-test.exe'
$arguments = @()
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') {
    $arguments += 'cc'
    # Keep compiler scratch/cache files inside this component.
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $output 'zig-local'
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $output 'zig-global'
}
$arguments += @(
    '-std=c11', '-fno-builtin', '-DROBUSTO_HALOW_SPI_HOST_TEST',
    '-Draw_send=hgic_spi_raw_send',
    '-I', $component, '-I', $VendorSource,
    (Join-Path $component 'halow_spi_wire.c'),
    (Join-Path $VendorSource 'hgic_raw.c'),
    (Join-Path $VendorSource 'hgic_cmd.c'),
    (Join-Path $VendorSource 'hgic_sdspi.c'),
    (Join-Path $PSScriptRoot 'wire_test.c'),
    '-o', $executable
)
& $Compiler @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Host test compilation failed ($LASTEXITCODE)"
}
& $executable
if ($LASTEXITCODE -ne 0) {
    throw "Host assertion failed at wire_test.c line $LASTEXITCODE"
}
Write-Output 'HaLow parser, full-size TX and firmware-info/STA/connection-state/reset SDSPI block assertions passed.'

$stubs = Join-Path $output 'idf-stubs'
$stubHeaders = @(
    'sdkconfig.h', 'esp_err.h', 'driver\gpio.h', 'driver\spi_master.h', 'driver\uart.h',
    'esp_attr.h', 'esp_log.h', 'esp_log_buffer.h', 'esp_memory_utils.h',
    'esp_private\spi_common_internal.h', 'esp_timer.h', 'hal\gpio_ll.h', 'hal\spi_ll.h',
    'freertos\FreeRTOS.h', 'freertos\semphr.h', 'freertos\task.h'
)
foreach ($header in $stubHeaders) {
    $path = Join-Path $stubs $header
    New-Item -ItemType Directory -Force (Split-Path $path -Parent) | Out-Null
    Set-Content -Path $path -Value '/* Generated host-test header; declarations are force-included. */'
}
$backendExecutable = Join-Path $output 'backend-test.exe'
$backendArguments = @()
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') {
    $backendArguments += 'cc'
}
$backendArguments += @(
    '-std=c11', '-Draw_send=hgic_spi_raw_send',
    '-include', (Join-Path $PSScriptRoot 'backend_test_stubs.h'),
    '-I', $stubs, '-I', $component,
    '-I', $VendorSource,
    '-I', (Join-Path $repo 'components\robusto\include'),
    (Join-Path $component 'halow_spi_wire.c'),
    (Join-Path $VendorSource 'hgic_raw.c'),
    (Join-Path $VendorSource 'hgic_cmd.c'),
    (Join-Path $PSScriptRoot 'backend_test.c'),
    '-o', $backendExecutable
)
& $Compiler @backendArguments
if ($LASTEXITCODE -ne 0) {
    throw "Backend host test compilation failed ($LASTEXITCODE)"
}
& $backendExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Backend host assertions failed ($LASTEXITCODE)"
}

$apExecutable = Join-Path $output 'backend-ap-test.exe'
$apArguments = $backendArguments[0..($backendArguments.Count - 3)] +
    @('-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1', '-o', $apExecutable)
& $Compiler @apArguments
if ($LASTEXITCODE -ne 0) {
    throw "AP backend host test compilation failed ($LASTEXITCODE)"
}
& $apExecutable
if ($LASTEXITCODE -ne 0) {
    throw "AP backend host assertions failed ($LASTEXITCODE)"
}
