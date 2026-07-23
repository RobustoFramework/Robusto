# ESP32-P4/ESP32-C6 SDIO delegation

This example connects two complete Robusto instances over one-bit SDIO. The
ESP32-P4 is the delegating controller and the ESP32-C6 is the delegate. The
example delegates PubSub, but either processor may also run unrelated local
Robusto services and media.

The reusable implementation is not board-specific. The checked-in GPIO and
clock defaults are the qualified profile for Waveshare ESP32-P4-WIFI6. Change
the P4 settings in **Robusto Configuration > Robusto Proxy > ESP SDIO
transport** for another compatible ESP32-P4/ESP32-C6 design.

The qualified P4 projects enable the ESP32-P4NRW32 package's 32 MiB PSRAM in
16-line HEX mode at 200 MHz. PSRAM is registered with ESP-IDF's capability
allocator, while standard `malloc()` remains internal. Buffers intended for
PSRAM request `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` explicitly.

## Roles

Both applications initialize Robusto normally. Their proxy configuration is
the meaningful difference:

- The controller selects `CONFIG_ROBUSTO_PROXY_CLIENT`, registers the SDIO
  client, and calls remote service APIs such as `robusto_proxy_pubsub_*`.
- The delegate selects `CONFIG_ROBUSTO_PROXY_SERVER`, enables each local
  service being delegated, and starts the SDIO delegate facade.
- RSD1 framing and the ESP SDIO transport are shared infrastructure.
- A transport failure remains an explicit error; it never redirects an
  operation to the controller's local service.

## Projects

- `c6_delegate`: C6 delegate, local PubSub backend, revision-2 updater, and
  hash-bound recovery identity service. The same project also builds the
  factory-migration bootstrap identity.
- `p4_controller`: remote PubSub loopback application.
- `provisioning`: P4-USB factory migration and final delegate installer.
- `components/robusto/proxy/transports/esp_sdio`: reusable proxy runtime and
  qualified one-bit raw SDIO drivers.

Use ESP-IDF v6.0.2 and activate its environment using Espressif's instructions
for your operating system. `idf.py -C` selects a project, but a relative `-B`
path is still resolved from the shell's working directory. Define the absolute
example path so every artifact remains under its selected project regardless
of the caller's working directory. ESP Hosted and ESP Wi-Fi Remote select
their ESP-IDF 6.0 Kconfig files through `ESP_IDF_VERSION`; set that
compatibility-series value to `6.0`, not the full installed release `6.0.2`,
before configuring the P4 projects.

```powershell
$env:ESP_IDF_VERSION = "6.0"
$robustoRoot = (Resolve-Path "<path-to-Robusto>").Path
$example = Join-Path $robustoRoot "examples/proxy/esp32_p4_c6_sdio"
$c6Delegate = Join-Path $example "c6_delegate"
$p4Controller = Join-Path $example "p4_controller"
$provisioning = Join-Path $example "provisioning"
```

Build the final delegate, the factory-migration bootstrap, and then the P4
projects:

```powershell
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build") build
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build-bootstrap") -D "ROBUSTO_C6_BOOTSTRAP=ON" build
idf.py -C $p4Controller -B (Join-Path $p4Controller "build") build
idf.py -C $provisioning -B (Join-Path $provisioning "build") build
```

The provisioning configure step derives exact-file and embedded ELF SHA-256
identities for both C6 images. It packages 64-byte-header containers at bundle
offsets `0` and `0x60000`, so the C6 payload offsets are `64` and `393280`.
Set `C6_BOOTSTRAP_FIRMWARE` and `C6_FINAL_FIRMWARE` during configure to package
different built images.

The user connects only the P4 USB interface. The `provisioning` application
packages both C6 images into the P4 `slave_fw` partition and identifies the
running C6 protocol itself. A factory C6 receives the bootstrap through the
factory-compatible transport; the provisioner then uses raw SDIO to confirm
the bootstrap, install the final delegate, confirm its exact ELF identity, and
verify that identity after another P4 restart. The six-phase migration state is
stored in P4 NVS so an interrupted run resumes at its recorded boundary. The
user is never expected to identify the installed C6 image or connect to C6
UART.

The complete dual-image build and P4 application build were validated with
ESP-IDF v6.0.2 on 2026-07-23. Factory migration on hardware remains a separate
qualification gate. See
[`docs/ESP32-P4-C6-Proxy-PubSub.md`](../../../docs/ESP32-P4-C6-Proxy-PubSub.md)
for the complete procedure, the optional Waveshare C6 UART pad wiring, the
recovery boundary, and requirements for other P4 Wi-Fi coprocessor boards.

Canonical GitHub links:

- Guide: https://github.com/RobustoFramework/Robusto/blob/main/docs/ESP32-P4-C6-Proxy-PubSub.md
- Example: https://github.com/RobustoFramework/Robusto/tree/main/examples/proxy/esp32_p4_c6_sdio

## Native contracts

Run all proxy contracts from the Robusto repository root with Python and a
C compiler available. Set `CC` when the compiler is not named `cc`, `gcc`, or
`clang`:

```console
python development/proxy/test/run_proxy_contracts.py
```

The command compiles and runs the portable proxy, lifecycle, RSD1, C6 update,
and C6 recovery contracts directly against `components/robusto/proxy`.