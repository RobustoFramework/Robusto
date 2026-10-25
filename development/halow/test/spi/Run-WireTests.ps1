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
Write-Output 'HaLow event parser, full-size TX and firmware-info/STA/connection-state/radio-on/reset SDSPI block assertions passed.'

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

foreach ($variant in @('configured', 'open', 'max-ssid', 'invalid-ssid', 'invalid-psk', 'invalid-bandwidth', 'invalid-frequencies')) {
    $staExecutable = Join-Path $output "backend-sta-$variant-test.exe"
    $staArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
        '-DCONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE=1'
    )
    if ($variant -eq 'max-ssid') { $staArguments += '-DTEST_STA_MAX_SSID=1' }
    if ($variant -eq 'invalid-ssid') { $staArguments += '-DTEST_STA_INVALID_SSID=1' }
    if ($variant -eq 'invalid-psk') { $staArguments += '-DTEST_STA_INVALID_PSK=1' }
    if ($variant -eq 'open') { $staArguments += '-DTEST_STA_OPEN=1' }
    if ($variant -eq 'invalid-bandwidth') { $staArguments += '-DTEST_STA_INVALID_BANDWIDTH=1' }
    if ($variant -eq 'invalid-frequencies') { $staArguments += '-DTEST_STA_INVALID_FREQUENCIES=1' }
    $staArguments += @('-o', $staExecutable)
    & $Compiler @staArguments
    if ($LASTEXITCODE -ne 0) {
        throw "STA $variant host test compilation failed ($LASTEXITCODE)"
    }
    & $staExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "STA $variant host assertions failed ($LASTEXITCODE)"
    }
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

foreach ($security in @('WPA_PSK', 'OPEN')) {
    $configuredExecutable = Join-Path $output "backend-ap-config-$security-test.exe"
    $configuredArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
        '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1',
        '-DCONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE=1',
        "-DCONFIG_ROBUSTO_HALOW_SPI_AP_$security=1",
        '-o', $configuredExecutable
    )
    & $Compiler @configuredArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Configured AP $security host test compilation failed ($LASTEXITCODE)"
    }
    & $configuredExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "Configured AP $security host assertions failed ($LASTEXITCODE)"
    }
}

$invalidSsidExecutable = Join-Path $output 'backend-ap-invalid-ssid-test.exe'
$invalidSsidArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
    '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1',
    '-DCONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE=1',
    '-DCONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK=1',
    '-DTEST_AP_INVALID_SSID=1',
    '-o', $invalidSsidExecutable
)
& $Compiler @invalidSsidArguments
if ($LASTEXITCODE -ne 0) {
    throw "Invalid AP SSID host test compilation failed ($LASTEXITCODE)"
}
& $invalidSsidExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Invalid AP SSID host assertions failed ($LASTEXITCODE)"
}
