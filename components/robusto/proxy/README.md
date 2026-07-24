# Robusto proxy

The proxy module delegates selected services between complete Robusto
instances. The delegating controller owns remote request and subscription
state. The delegated instance runs the real local services and exposes only
the configured adapters. Both instances may continue running unrelated local
Robusto services. Applications use the public headers in `proxy/include`;
transport and target internals remain private to the component.

PubSub data up to 3,824 bytes uses one proxy frame in either direction. Larger
controller-to-delegate publishes use sequential 4,080-byte chunks and are
reassembled once by the delegate before local PubSub dispatch. Larger
delegate-to-controller deliveries use negotiated begin, chunk, and commit
events; the controller invokes the application callback only after complete
reassembly. The wire format retains the public `uint32_t` message length and
does not impose a smaller directional message cap. Practical payload size in
either direction is determined at runtime by the contiguous allocations that
succeed while the application and transport are active.

The generic delivery codec permits up to 4,080 data bytes per chunk. The
ESP32-C6 SDIO sender uses 4,028 data bytes so the 16-byte delivery header,
24-byte proxy frame overhead, and 24-byte RSD1 overhead fit the C6 driver's
4,092-byte packet limit. It retains the current FIFO delivery descriptor until
each event is confirmed sent, then serializes that descriptor through
`DELIVERY_COMMIT` before selecting the next queued delivery. The P4 parser also
accepts a valid inline delivery for another subscription while chunk
reassembly is active without discarding the partial large delivery.

Each sender must own one contiguous payload while a transfer is active. The
current ESP32-C6 build has no PSRAM, so both inbound publish reassembly and an
outbound queued delivery are limited by available 8-bit heap under load. The
ESP32-P4 client prefers PSRAM for delivery reassembly and falls back to internal
8-bit heap. When a chunked publish is synchronously delivered to a delegated
subscription, the C6 transfers ownership of the completed reassembly buffer to
the outbound queue instead of allocating a second full copy. C6 delivery
allocation or queue pressure drops the complete newest delivery and increments
`delivery_drops`; query PubSub status and monitor P4 sequence-gap counters
rather than interpreting a missing large topic as a subscription failure.

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