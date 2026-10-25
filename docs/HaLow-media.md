# Direct HaLow medium

The HaLow medium carries Robusto packets over MAC-addressed module frames.
It does not require IP, TCP or UDP. Enable `CONFIG_ROBUSTO_SUPPORTS_HALOW`.
The media value is `robusto_mt_halow` (`0x0100`); existing media assignments
remain unchanged.

## Integration boundary

[robusto_halow.h](../components/robusto/include/robusto_halow.h) exposes a
portable driver binding and `robusto_halow_reset_and_reinitialize()`.
Select the bundled SPI backend in Kconfig, or register a custom backend before
Robusto network initialization. Enabling only the medium without a binding
fails explicitly. Selecting SPI and also registering a custom driver is an
explicit configuration error, not an implicit backend override.

The backend provides:

- `start`: start module initialization/association and its receive task.
  Failure must leave no running callbacks or partially started backend.
- `stop`: stop the module and quiesce receive/link callbacks before returning.
- `write`: synchronously consume or copy a complete raw payload addressed to
  the supplied radio MAC. Return the raw payload length, a short count, or a
  negative error. Do not report remote delivery.
- `reset_and_reinitialize` (optional): quiesce traffic, request one module
  reboot and reinitialize once, including after failed startup. Keep Robusto's
  lifecycle synchronized through its stop/start functions where applicable.
  Do not recursively invoke the public reset API or add automatic retries.
  Drivers without this callback return `ROB_ERR_NOT_SUPPORTED` for reset.

For Taixin `hgic_raw_send`, account for its 22-byte vendor/Ethernet overhead:
the driver return must equal raw payload length plus 22. Only then return the
raw payload length to Robusto. A positive short write is an error.

The backend passes the Ethernet source MAC and the payload after the
Ethernet header to `robusto_halow_receive`. Run that callback in an
independent task, serialized against other receive callbacks, not in an ISR
or synchronously inside `write`. The send worker can wait for a receipt or
fragment-transfer result, so receive processing cannot depend on that worker.

Report association availability using `robusto_halow_set_link_ready`.
Starting the driver is not equivalent to association. Disconnection blocks
the send queue without shutting down configuration handling.

Lifecycle/configuration calls must be serialized by the firmware. The
adapter serializes physical writes with a mutex; a receive-side receipt or
fragment response can write while the send worker waits.

## Bundled Huge-IC SPI backend

The reusable [SPI backend](../components/robusto/network/src/media/halow/spi/halow_spi.c),
its parser, traces, vendor-source selection and tests live in Robusto. They
have no Collector-specific registration, board selection or startup caller.
Another application, including Central, can select the same backend and
provide its own board pin settings.

Configure **Robusto Configuration -> Networking -> HaLow**:

| Setting | Purpose |
| --- | --- |
| `CONFIG_ROBUSTO_HALOW_SPI` | Select and automatically register the Huge-IC SPI backend during HaLow initialization |
| `CONFIG_ROBUSTO_HALOW_SPI_ROLE_STA` | Default role: station, with association determining local availability |
| `CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP` | Access-point role, with validated local AP/raw-interface availability independent of station association |
| `CONFIG_ROBUSTO_HALOW_SPI_SCLK_GPIO` | Board SPI clock pin |
| `CONFIG_ROBUSTO_HALOW_SPI_MOSI_GPIO` | Board MOSI pin |
| `CONFIG_ROBUSTO_HALOW_SPI_MISO_GPIO` | Board MISO pin |
| `CONFIG_ROBUSTO_HALOW_SPI_CS_GPIO` | Board peripheral chip-select pin |
| `CONFIG_ROBUSTO_HALOW_SPI_IRQ_GPIO` | Module interrupt pin |
| `CONFIG_ROBUSTO_HALOW_SPI_DEBUG_RX_GPIO` | Module debug TX connection, captured only with diagnostics enabled |
| `CONFIG_ROBUSTO_HALOW_SPI_DIAGNOSTICS` | Opt into detailed startup diagnostics; default off |
| `CONFIG_ROBUSTO_HALOW_RESET_AFTER_START` | Explicitly request one reset/reinitialization after the initial HaLow start, whether that start succeeded or failed; default off |

SPI support is currently limited to ESP32-P4, the platform on which this
backend has been built and exercised. It uses SPI2, 40 MHz, mode 0 and hardware
CS per individual transfer with one-clock setup/hold and no keep-active flag.
The configuration preserves the
[pinned LilyGO wrapper](https://github.com/Xinyuan-LilyGO/T-Halow-P4/blob/2bb0d61a91ef288ac237afa481d6dabf11087031/examples/halow_spi_single/components/cpp_bus_driver/src/bus/spi/hardware_spi.cpp).
It is not a claim of support for other ESP-IDF targets. The generic driver
binding remains portable and independent of the bundled backend.

Required GPIO settings default to `-1`, not a guessed board mapping. Invalid,
duplicate or non-output-capable output pins fail explicitly before transport
allocation. The diagnostic UART pin must also be valid and distinct when
diagnostics are enabled. Collector's LilyGO configuration supplies
SCLK43/MOSI44/MISO39/CS42/IRQ40 and diagnostic RX12; these are application
settings, not hard-coded library defaults.

The build retrieves the three unmodified vendor sources into
`build/_deps/robusto_halow_vendor`, pinned to LilyGO commit
`2bb0d61a91ef288ac237afa481d6dabf11087031`. Nothing is edited in
`managed_components`. Read the [upstream notice](../components/robusto/network/src/media/halow/spi/NOTICE.txt)
before distributing firmware; selecting this backend does not resolve the
external driver's redistribution permissions.

### Initialization and reset

Normal initialization keeps the existing 1-second pre-SDIO delay, validates
firmware-info command 43, then sets the configured role with command 41.
Each response gates the next step. Firmware version and radio MAC are reported
only after a validated 43 reply.

- **STA (default):** sets `"sta"` and queries association with command 40.
  Initialized and associated remain separate states; `associated=0` does not
  fail initialization.
- **AP:** sets `"ap"`, verifies command 145 (`GET_MODE`) reads back `"ap"`,
  then requires an exact `TEST2` local raw-interface echo. It publishes local
  availability without requiring STA association. Individual station
  connect/disconnect events do not clear AP availability. The existing status
  checks query AP mode rather than association; role loss, stale/rejected
  status or transport failure clears availability without restarting.

Mode replies are compared within their declared payload length: `"ap"` may
occupy two bytes and `"sta"` three, without a terminating NUL. NUL-terminated
forms remain accepted; transport padding is not part of the mode string.
Central reported a command-145 reply with a 14-byte declared packet and a
16-byte padded read. Requiring three bytes for `"ap\0"` rejected this reply
before its payload could be evaluated, leading to a command timeout. The host
regression reproduces that header/length with a two-byte `"ap"` test payload;
the original log omitted payload bytes and does not establish their contents.

AP success proves responsive mode/readback and a local raw interface, **not**
physical beaconing, station admission, Robusto peer readiness or RF delivery.
Stored credentials, channel and regulatory settings are unchanged. Applications
must ensure compatible, lawful RF/security settings separately. Central selects
AP in its board overlay; other applications retain STA unless explicitly changed.

`robusto_halow_reset_and_reinitialize()` requires initialized Robusto, but not
successful backend startup. For a stopped backend or failed startup, it opens
only SPI/SDIO, without first requiring the normal role-specific startup sequence,
and submits one module reboot
command 132. It releases the transport and attempts normal initialization
once. A running/runtime-faulted backend is quiesced and stopped through
Robusto. Transport setup, submission and cleanup errors prevent the subsequent
initialization attempt. A failed initialization is returned, not retried.
Reinitialization retains the selected STA/AP role and its validation steps.

The reset targets the **HaLow module**, not the host ESP32 and not factory
defaults. Command submission is not an independently confirmed reboot:
`ROB_OK` means normal initialization completed, not that association or a
physical reboot was observed. The existing 1-second delay is reused without
an invented additional reboot delay. The command still depends on a usable
SPI command transport.

Serialize reset with initialization/start/stop and other resets. Invoke it
from an application task, never the receive task or an ISR. An active
backend whose stop did not finish retains its lifecycle state/resources for
a later explicit stop. Stopped reset-transport cleanup failure remains an
error, but a later explicit reset can clean those resources and make a new
attempt.

When `CONFIG_ROBUSTO_HALOW_RESET_AFTER_START=y`, Robusto's media startup logs
and invokes this operation once after the initial HaLow start, even if that
start failed. The backend's subsequent normal start does not repeat the
configured reset. Failure is logged without further attempts or host reboot.
Neither Collector nor Central needs to implement this startup policy.

### Detailed diagnostics

Normal operation does not claim UART2, obtain a private SPI DMA descriptor or
require diagnostic capture to succeed. Lifecycle/error logs remain enabled.
Opt-in diagnostics retain the following behavior:

- SDIO CIS identity is always queried. Diagnostic runs additionally log up to
  32 initialization CMD52 exchanges, read back SDIO control/block-size
  registers and log successful per-command interface health checks.
- Commands 43, 41, 40, 145 and 132 capture CMD53/data-block TX/RX, TX-buffer stability,
  actual DMA descriptor/backing-memory bytes through the P4 uncached alias,
  hardware bit length/order and SPI register snapshots. Missing or mismatching
  DMA capture fails the diagnostic operation explicitly.
- Callback sequences hold 32 entries with up to 69 bytes per transfer.
  Identical complete busy reads are compacted with counts and first/last
  timing; overflow and byte limits are explicit. `+`/`-` are vendor operation
  markers, not GPIO writes. `CS_latch` is the GPIO output latch, not the
  peripheral-controlled CS waveform. Logging happens after submission.
- Up to 2048 module debug-UART bytes are captured on UART2 at 115200, RX only.
  Setup/cleanup errors are explicit and the driver is released after startup.
  UART bytes are not used as a replacement MAC source. UART1 and module RX
  remain unclaimed.
- Unsupported events are distinguished from malformed packets. TX_BITRATE
  is decoded as a raw four-byte status value (units unassumed), at most four
  logs per start. Unhandled events do not fabricate command responses.

These P4/IDF-specific diagnostics were built against ESP-IDF 6.0.3. DMA and
register snapshots are host observations, not a capture of the module's
received bits or physical CS/SCLK/MOSI waveform.

The private `HALOW_DIAGNOSTIC_SKIP_FW_INFO=0` flag in the backend retains the
earlier query-skip experiment. Setting it to 1 tests 41/40 but explicitly stops
before enabling raw operation because no radio MAC was obtained.

## Peer addressing

The logical identity remains the base MAC. `halow_mac_address` stores the
module's distinct unicast MAC and `halow_address_valid` records whether it is
known. Bind an existing peer using `robusto_halow_bind_peer` before presenting
or sending to it. Do not assume the base MAC is also its module address.
Bindings for established peers are saved in retained relations and restored
with them. Changed bindings update the retained relation. The retained-layout
marker changes with the new mask and address fields; old layouts are rejected.

The conductor's HaLow selection has separate base-MAC and HaLow radio-MAC
settings. Its peer is bound before the first presentation.

Media masks and exclusions are 16 bits throughout the framework. Legacy
presentation v0 keeps its byte-for-byte layout for peers not known to support
HaLow. Presentation v1 adds the high mask byte after the base MAC for
HaLow-capable peers. See the [presentation contract](../components/robusto/network/presentation.md).

Every envelope carries the sender's logical base MAC. Unknown incoming peers
are admitted only for presentation packets. Peer discovery or provisioning
of the initial remote radio MAC is application work, not an IP requirement.
MAC identities and CRCs are not authentication; association security and
management authorization need their own implementation.

## Envelope version 1

All multibyte integers are little-endian. The backend supplies framing:
one complete raw module payload is one envelope.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 2 | ASCII `RH` |
| 2 | 1 | Envelope version, `1` |
| 3 | 1 | `0`: data, `1`: data requesting receipt, `2`: receipt |
| 4 | 4 | Correlation sequence |
| 8 | 6 | Sender's logical base MAC |
| 14 | 2 | Payload length, zero for receipts |
| 16 | 4 | Robusto CRC32 over bytes 0-15 followed by payload |
| 20 | variable | Robusto packet without its internal addressing reserve |

A receipt echoes the sequence, carries the receiving peer's base MAC and
comes from its radio MAC. All three must match the pending send. It confirms
a CRC-valid envelope was admitted to the receive path, not application
processing. Invalid or unallocatable frames are not positively acknowledged.

There is one pending synchronous receipt request per medium. Data fragments
use `receipt=false`; fragmentation initiation can request a receipt. The
existing Robusto fragmentation protocol supplies missing-fragment recovery
and final transfer results. The envelope does not independently retry data
or suppress duplicates; existing Robusto message semantics remain in effect.

## Size limits

The bundled LilyGO driver raw API allows 1778 bytes, but its SPI writer
limits a write to 1664 bytes. Subtracting 22 vendor bytes gives a source-level
raw frame ceiling of 1642 bytes.

- Raw frame: **1642** bytes maximum.
- Envelope: **20** bytes.
- Robusto packet: **1622** bytes maximum before fragmentation.
- Fragment data: **1612** bytes, plus the existing 10-byte fragment header.

The backend must receive complete frames at this size and return errors for
truncation. These bounds do not constitute hardware validation.

## Source selection for local development

An ESP-IDF project can add the sibling checkout's `components` directory to
`EXTRA_COMPONENT_DIRS` before `project.cmake` and declare `REQUIRES robusto`.
Remove registry Robusto requirements from project component manifests so
they cannot select a second managed copy. Do not edit `managed_components`.
Reconfigure from a fresh build directory and inspect the resolved component
paths before claiming a firmware build used these changes.

## Validation and remaining work

Run the focused host contract:

```powershell
$env:CC = 'python -m ziglang cc'
python .\development\halow\test\run_halow_contract.py
python .\development\halow\test\run_presentation_contract.py
```

`CC` may instead name an existing native C compiler. Tests compile against
real Robusto public headers and simulate the driver, queue execution, peer
lookup, and receipt timing. They cover frame limits, short writes,
fragmentation dispatch, correlated receipts, timeout, malformed/CRC-invalid
frames, truncated heartbeats and fragment headers, receive dispatch, retained
binding update calls, and link/stop/restart behavior.

The presentation contract runs the production serializer/parser with HaLow
enabled and disabled. It verifies legacy output, the v1 high mask byte,
round trips, malformed inputs, name bounds and duplicate-peer mapping merges.

A second contract runs the production fragmentation implementation with the
HaLow medium value and chunk size. It verifies a complete 6485-byte transfer
and recovery after dropping one fragment, including the final result and
exactly one requested fragment retransmission. Existing native-format and
signedness compiler warnings in that legacy source are reported separately;
the new adapter compiles with warnings treated as errors.

The shared SPI [wire/lifecycle runner](../development/halow/test/spi/Run-WireTests.ps1)
uses the same native compiler and pinned vendor sources:

```powershell
$zig = python -c "import pathlib,ziglang; print(pathlib.Path(ziglang.__file__).parent / 'zig.exe')"
.\development\halow\test\spi\Run-WireTests.ps1 -Compiler $zig -VendorSource 'C:\path\to\firmware\build\_deps\robusto_halow_vendor\examples\halow_spi_single\components\huge-ic-nonos-driver-v2.0.0-31919'
```

It checks exact 64-byte command blocks (43/41/40/132), vendor callback ordering,
full-size raw TX, the production parser and trace compaction. Header-only
commands retain prior STA bytes outside their declared length; the tests
preserve this vendor behavior. The actual backend is also compiled against
host ESP-IDF/SDIO/Robusto mocks to test startup, reset after a command-43
timeout, invalid GPIOs, runtime faults, credit/lock/quiesce/cleanup failures
and completion-token preservation. Default operation is tested with an
unavailable diagnostic DMA descriptor and occupied UART. The portable
contracts cover optional reset dispatch, unsupported reset and configured
SPI registration, as well as the existing media/fragment/presentation paths.

These are not on-device concurrency, GPIO/DMA or RF tests. Before this source
migration, Collector logs showed firmware `1.6.4.5`/SVN `38054`, successful
43/41/40 after power cycle, and recovery from a 43 timeout using explicit
132 plus reinitialization. The migrated shared build still needs on-device
validation. Association, raw bidirectional traffic, Central integration and
TWAI management remain separate work. Existing hardware profiles are not
silently switched or deleted by enabling source support.
