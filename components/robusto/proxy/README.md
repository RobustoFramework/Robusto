# Robusto proxy

The proxy module delegates selected services between complete Robusto
instances. The delegating controller owns remote request and subscription
state. The delegated instance runs the real local services and exposes only
the configured adapters. Both instances may continue running unrelated local
Robusto services. Applications use the public headers in `proxy/include`;
transport and target internals remain private to the component.

PubSub publishes up to 3,824 bytes use one proxy frame. Larger publishes are
sent as sequential 4,080-byte chunks and reassembled once by the delegated
instance before local PubSub dispatch. The wire format retains the public
`uint32_t` message length and does not impose a smaller message cap. A delegate
accepts a chunked publish only when it can allocate one contiguous message
buffer; otherwise the controller receives `ROB_ERR_OUT_OF_MEMORY` before any
chunks are sent. The current ESP32-C6 build has no PSRAM: its build-time SRAM
remainder is about 290.2 KiB, while the actual runtime limit is the largest
available internal 8-bit heap block and is therefore lower under load.

## Layout

- `include`: portable proxy API and public transport facades.
- `src`: transport-independent client, server, lifecycle, framing, and service
  adapters.
- `transports/rsd1`: portable RSD1 packet and C6 update/recovery wire contracts.
- `transports/esp_sdio`: ESP32-P4 host and ESP32-C6 delegate runtime.
  - `src/p4`: SDIO host binding, proxy client runtime, and C6 provisioning.
  - `src/c6`: SDIO slave binding and proxy service runtime.
  - `src/c6/update`: C6 updater and hash-bound recovery state.
  - `src/common`: private message identifiers shared by both targets.

Board-specific applications and configuration defaults belong under
`examples/proxy`; reusable proxy behavior does not.

## Configuration

Enable `CONFIG_ROBUSTO_PROXY` and select exactly one proxy role. ESP32-P4 SDIO
GPIO and clock settings are under **Robusto Configuration > Robusto Proxy > ESP
SDIO transport**. ESP32-C6 SDIO slave pins are fixed by the peripheral. The
hardware-qualified ESP32-P4/ESP32-C6 runtime supports only the low-memory
profile. The portable standard profile remains native-tested but is not
selectable for these targets until matching runtime capacities are implemented
and measured.

## Support status

| Surface | Status |
|---|---|
| Portable proxy and RSD1 | Implemented and native-tested |
| ESP32-P4/ESP32-C6 one-bit SDIO | Hardware-qualified v1 binding |
| Generation-6 P4-driven update | Hardware-qualified |
| Stock/factory C6 bootstrap | Integrated and build-validated; hardware qualification pending |
| Other P4/C6 SDIO boards | Requires board-specific qualification |
| SPI or UART proxy transport | Not implemented |
| Non-C6 delegated runtime | Not implemented |

SDIO supersedes the earlier SPI physical-binding proposal for the qualified v1
hardware. SPI may be implemented as a separate future transport; it is not a
current parity requirement.

## Native contract gate

Run all portable proxy, lifecycle, RSD1, update, and recovery contracts from
the repository root:

```console
python development/proxy/test/run_proxy_contracts.py
```

This command is the provider-independent proxy CI gate. It requires Python and
`cc`, `gcc`, or `clang`; set `CC` to select another compiler executable.

See the proxy PubSub guide on GitHub for the complete build, provisioning,
update, and recovery procedure:

https://github.com/RobustoFramework/Robusto/blob/main/docs/ESP32-P4-C6-Proxy-PubSub.md

See the qualified ESP32-P4/ESP32-C6 SDIO example on GitHub:

https://github.com/RobustoFramework/Robusto/tree/main/examples/proxy/esp32_p4_c6_sdio