param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$VendorSource,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '.test-build')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$component = Join-Path $repo 'components\robusto\network\src\media\halow\spi'
$output = $OutputDirectory
New-Item -ItemType Directory -Force $output | Out-Null
$executable = Join-Path $output 'wire-test.exe'
$arguments = @()
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') {
    $arguments += 'cc'
    # Keep compiler scratch/cache files with the test outputs.
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

$checksExecutable = Join-Path $output 'sdspi-check-test.exe'
$checksArguments = @()
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { $checksArguments += 'cc' }
$checksArguments += @(
    '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-I', $component, '-I', $VendorSource,
    (Join-Path $VendorSource 'hgic_sdspi.c'),
    (Join-Path $PSScriptRoot 'sdspi_check_test.c'),
    '-o', $checksExecutable
)
& $Compiler @checksArguments
if ($LASTEXITCODE -ne 0) { throw "SDSPI check compilation failed ($LASTEXITCODE)" }
& $checksExecutable
if ($LASTEXITCODE -ne 0) { throw "SDSPI check assertions failed ($LASTEXITCODE)" }

$rxExecutable = Join-Path $output 'rx-integration-test.exe'
$rxArguments = @()
if ((Split-Path $Compiler -Leaf) -eq 'zig.exe') { $rxArguments += 'cc' }
$rxArguments += @(
    '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter',
    '-DCONFIG_ROB_LOG_MAXIMUM_LEVEL=3',
    '-DCONFIG_ROBUSTO_HALOW_RX_TRACE=1',
    '-I', (Join-Path $PSScriptRoot '..\support'),
    '-I', (Join-Path $repo 'components\robusto\include'),
    '-I', (Join-Path $repo 'components\robusto\base\src\system'),
    '-I', $component, '-I', $VendorSource,
    (Join-Path $component 'halow_spi_wire.c'),
    (Join-Path $component '..\halow.c'),
    (Join-Path $repo 'components\robusto\network\src\message\robusto_message_fragment.c'),
    (Join-Path $repo 'components\robusto\base\src\system\compat_crc32.c'),
    (Join-Path $PSScriptRoot 'rx_integration_test.c'),
    '-o', $rxExecutable
)
& $Compiler @rxArguments
if ($LASTEXITCODE -ne 0) {
    throw "RX integration compilation failed ($LASTEXITCODE)"
}
& $rxExecutable
if ($LASTEXITCODE -ne 0) { throw "RX integration assertions failed ($LASTEXITCODE)" }

$stubs = Join-Path $output 'idf-stubs'
$stubHeaders = @(
    'sdkconfig.h', 'esp_err.h', 'driver\gpio.h', 'driver\spi_master.h', 'driver\uart.h',
    'esp_attr.h', 'esp_log.h', 'esp_log_buffer.h', 'esp_memory_utils.h',
    'esp_private\spi_common_internal.h', 'esp_timer.h', 'esp_random.h', 'hal\gpio_ll.h', 'hal\spi_ll.h',
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
    '-std=c11', '-Draw_send=hgic_spi_raw_send', '-DCONFIG_ROBUSTO_HALOW_RX_TRACE=1',
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

foreach ($role in @('STA', 'AP')) {
    foreach ($network in @('stored', 'configured')) {
        $minimumExecutable = Join-Path $output "backend-minimum-mcs-$role-$network-test.exe"
        $minimumArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
            '-DCONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN=2',
            '-DTEST_MINIMUM_MCS=1'
        )
        if ($role -eq 'AP') { $minimumArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
        if ($network -eq 'configured') {
            $minimumArguments += "-DCONFIG_ROBUSTO_HALOW_SPI_${role}_CONFIGURE=1"
            if ($role -eq 'AP') { $minimumArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK=1' }
        }
        $minimumArguments += @('-o', $minimumExecutable)
        & $Compiler @minimumArguments
        if ($LASTEXITCODE -ne 0) { throw "$role $network minimum-MCS compilation failed ($LASTEXITCODE)" }
        & $minimumExecutable
        if ($LASTEXITCODE -ne 0) { throw "$role $network minimum-MCS assertions failed ($LASTEXITCODE)" }
    }
}

foreach ($role in @('STA', 'AP')) {
    foreach ($direction in @('RX', 'TX', 'TX-checked-echo', 'BYTE-TX', 'BYTE-TX-checked-echo',
                            'TIGHT-TX', 'TIGHT-TX-checked-echo')) {
        $sweepExecutable = Join-Path $output "backend-frm2-sweep-$role-$direction-test.exe"
        $sweepArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
            '-DCONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX=1'
        )
        if ($direction -ne 'RX') {
            $sweepArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX=1'
        }
        if ($direction.StartsWith('BYTE-')) {
            $sweepArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_FRM2_BYTE_SCAN_TX=1'
        }
        if ($direction.StartsWith('TIGHT-')) {
            $sweepArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_FRM2_TIGHT_SCAN_TX=1'
        }
        if ($direction.EndsWith('checked-echo')) {
            $sweepArguments += @(
                '-DCONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS=1',
                '-DCONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP=1',
                '-DCONFIG_ROBUSTO_HALOW_SPI_TX_TRACE=1',
                '-I', (Join-Path $PSScriptRoot '..\support')
            )
        }
        if ($role -eq 'AP') { $sweepArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
        $sweepArguments += @('-o', $sweepExecutable)
        & $Compiler @sweepArguments
        if ($LASTEXITCODE -ne 0) { throw "$role $direction FRM2 sweep compilation failed ($LASTEXITCODE)" }
        & $sweepExecutable
        if ($LASTEXITCODE -ne 0) { throw "$role $direction FRM2 sweep assertions failed ($LASTEXITCODE)" }
    }
}

foreach ($role in @('STA', 'AP')) {
    foreach ($variant in @('plain', 'configured-radio-off')) {
        $echoExecutable = Join-Path $output "backend-echo-$role-$variant-test.exe"
        $echoArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
            '-DCONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP=1',
            '-DCONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS=1'
        )
        if ($role -eq 'AP') { $echoArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
        if ($variant -eq 'configured-radio-off') {
            $echoArguments += @(
                '-DCONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START=1',
                "-DCONFIG_ROBUSTO_HALOW_SPI_${role}_CONFIGURE=1"
            )
        }
        $echoArguments += @('-o', $echoExecutable)
        & $Compiler @echoArguments
        if ($LASTEXITCODE -ne 0) { throw "$role $variant echo compilation failed ($LASTEXITCODE)" }
        & $echoExecutable
        if ($LASTEXITCODE -ne 0) { throw "$role $variant echo assertions failed ($LASTEXITCODE)" }
    }
}

foreach ($role in @('STA', 'AP')) {
    $checksBackendExecutable = Join-Path $output "backend-write-checks-$role-test.exe"
    $checksBackendArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
        '-DCONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS=1',
        '-DCONFIG_ROBUSTO_HALOW_SPI_TX_TRACE=1',
        '-I', (Join-Path $PSScriptRoot '..\support')
    )
    if ($role -eq 'AP') { $checksBackendArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
    $checksBackendArguments += @('-o', $checksBackendExecutable)
    & $Compiler @checksBackendArguments
    if ($LASTEXITCODE -ne 0) { throw "$role write-check backend compilation failed ($LASTEXITCODE)" }
    & $checksBackendExecutable
    if ($LASTEXITCODE -ne 0) { throw "$role write-check backend assertions failed ($LASTEXITCODE)" }
}

foreach ($role in @('STA', 'AP')) {
    $radioOffExecutable = Join-Path $output "backend-radio-off-$role-plain-test.exe"
    $radioOffArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
        '-DCONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START=1'
    )
    if ($role -eq 'AP') { $radioOffArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
    $radioOffArguments += @('-o', $radioOffExecutable)
    & $Compiler @radioOffArguments
    if ($LASTEXITCODE -ne 0) { throw "Startup radio-off $role compilation failed ($LASTEXITCODE)" }
    & $radioOffExecutable
    if ($LASTEXITCODE -ne 0) { throw "Startup radio-off $role assertions failed ($LASTEXITCODE)" }
}

foreach ($role in @('STA', 'AP')) {
    $traceExecutable = Join-Path $output "backend-tx-trace-$role-test.exe"
    $traceArguments = $backendArguments[0..($backendArguments.Count - 3)] + @(
        '-DCONFIG_ROBUSTO_HALOW_SPI_TX_TRACE=1',
        '-I', (Join-Path $PSScriptRoot '..\support')
    )
    if ($role -eq 'AP') { $traceArguments += '-DCONFIG_ROBUSTO_HALOW_SPI_ROLE_AP=1' }
    $traceArguments += @('-o', $traceExecutable)
    & $Compiler @traceArguments
    if ($LASTEXITCODE -ne 0) { throw "$role TX trace compilation failed ($LASTEXITCODE)" }
    & $traceExecutable
    if ($LASTEXITCODE -ne 0) { throw "$role TX trace assertions failed ($LASTEXITCODE)" }
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
