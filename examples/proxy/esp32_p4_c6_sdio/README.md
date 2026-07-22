# ESP32-P4/ESP32-C6 SDIO delegation

This example connects two complete Robusto instances over one-bit SDIO. The
ESP32-P4 is the delegating controller and the ESP32-C6 is the delegate. The
example delegates PubSub, but either processor may also run unrelated local
Robusto services and media.

The reusable implementation is not board-specific. The checked-in GPIO and
clock defaults are the qualified profile for Waveshare ESP32-P4-WIFI6. Change
the P4 settings in **Robusto Configuration > Robusto Proxy > ESP SDIO
transport** for another compatible ESP32-P4/ESP32-C6 design.

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
  hash-bound recovery identity service.
- `p4_controller`: remote PubSub loopback application.
- `provisioning`: P4-USB installer for a built `c6_delegate` application.
- `components/robusto/proxy/transports/esp_sdio`: reusable proxy runtime and
  qualified one-bit raw SDIO drivers.

Use ESP-IDF v6.0.2 and activate its environment using Espressif's instructions
for your operating system. Run the following commands from the Robusto
repository root. `idf.py -C` selects a project without changing the shell's
working directory.

```console
idf.py -C examples/proxy/esp32_p4_c6_sdio/c6_delegate build
idf.py -C examples/proxy/esp32_p4_c6_sdio/p4_controller build
idf.py -C examples/proxy/esp32_p4_c6_sdio/provisioning build
```

The provisioning configure step derives the exact-file and embedded ELF
SHA-256 identities from `c6_delegate/build/robusto_c6_delegate.bin`. Set the
`C6_FIRMWARE` CMake variable to package a different application image.

The user connects only the P4 USB interface. The `provisioning` application
packages the C6 image into P4 flash and transfers it over onboard SDIO. It must
identify the running C6 protocol itself; the user is never expected to know
the installed C6 image or connect to C6 UART.

The current provisioner handles generation-6 revision-2 updates, but the
factory-to-generation-6 bootstrap that was qualified during development has
not yet been integrated into this generalized example. Consequently this
example is not yet a complete stock-board installation path. See
[`docs/ESP32-P4-C6-Proxy-PubSub.md`](../../../docs/ESP32-P4-C6-Proxy-PubSub.md)
for the complete procedure, the optional Waveshare C6 UART pad wiring, the
recovery boundary, and requirements for other P4 Wi-Fi coprocessor boards.

## Native contracts

Run all proxy contracts from the Robusto repository root with Python and a
C compiler available. Set `CC` when the compiler is not named `cc`, `gcc`, or
`clang`:

```console
python development/proxy/test/run_proxy_contracts.py
```

The command compiles and runs the portable proxy, lifecycle, RSD1, C6 update,
and C6 recovery contracts directly against `components/robusto/proxy`.