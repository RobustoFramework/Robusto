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
An expired presentation wait does not cancel queued transmission. Duplicate
outbound HI requests for the same peer and reason return `ROB_ERR_NOT_READY`
while the original is queued or sending, without allocating another message.
Replies and other traffic keep their existing admission policy. See
[presentation ownership](../components/robusto/network/presentation.md)
for the mutex-protected reservation and deferred-free contract. This prevents
repeated offline requests from exhausting the queue; it does not fix association.

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
| `CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE` | Opt into applying and verifying the complete STA network configuration on normal startup; default off |
| `CONFIG_ROBUSTO_HALOW_SPI_STA_SSID` | Required 1-31-byte SSID when STA configuration is enabled |
| `CONFIG_ROBUSTO_HALOW_SPI_STA_WPA_PSK` / `_STA_OPEN` | STA security choice; WPA-PSK is the default |
| `CONFIG_ROBUSTO_HALOW_SPI_STA_PSK` | Same 64-hex-digit key as the AP, not a passphrase; no generated/default deployment key |
| `CONFIG_ROBUSTO_HALOW_SPI_STA_BW_1`, `_BW_2`, `_BW_4`, `_BW_8` | STA BSS bandwidth in MHz; match the AP |
| `CONFIG_ROBUSTO_HALOW_SPI_STA_FREQ_LIST` | Required comma-separated center frequencies in 100 kHz units; must include the AP channel |
| `CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP` | Access-point role, with validated local AP/raw-interface availability independent of station association |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE` | Opt into AP network configuration on normal startup; default off |
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
firmware-info command 43, then sets the configured role with command 41 and
requires acknowledgement of radio-on command 65 with value `1`.
Each response gates the next step. Firmware version and radio MAC are reported
only after a validated 43 reply.

- **STA (default):** sets `"sta"`, enables the radio and queries association with command 40.
  Initialized and associated remain separate states; `associated=0` does not
  fail initialization.
- **AP:** sets `"ap"`, enables the radio, verifies command 145 (`GET_MODE`) reads back `"ap"`,
  then requires an exact `TEST2` local raw-interface echo. It publishes local
  availability without requiring STA association. Individual station
  connect/disconnect events do not clear AP availability. The existing status
  checks query AP mode rather than association; role loss, stale/rejected
  status or transport failure clears availability without restarting.

The pinned API documents `hgic_raw_set_radio_onoff(1)` but no radio-state
getter. Startup therefore issues that command once on every normal start,
including reinitialization after reset. Submission failure, rejection or
timeout fails startup before the receive task or link readiness is published;
there is no fallback or additional retry. Successful acknowledgement is logged:

```text
Radio enable command 65 acknowledged; RF operation not independently verified
```

This establishes acceptance of the enable request, not an independent RF-state
readback, beacon capture, station admission or successful frame delivery. The
query-skip diagnostic experiment remains blocked from radio operation and
does not issue radio-on. Stopping still quiesces host traffic/transport; this
change does not add a radio-off command or factory reset.

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
Stored network settings are unchanged unless STA or AP configuration below is enabled.
Applications must ensure compatible, lawful RF/security settings. In the
Central/Collector deployment, Central selects STA in its board overlay and
Collector provides the AP. Other applications retain STA unless explicitly changed.

### STA network configuration

Under **Robusto Configuration -> Networking -> HaLow**, select STA and enable
**Apply STA network settings at startup** (`CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE`).
The menu then exposes SSID, security, PSK, BSS bandwidth and frequency list.
Disabled preserves the module's stored settings. **Migration:** this option
previously applied only SSID; existing enabled configurations must now supply
the complete set, including a PSK when WPA-PSK is selected.

Central's LilyGO overlay selects `onwatch`, WPA-PSK, 1 MHz and the single
frequency `8655` (865.5 MHz). The user must supply Collector's existing
64-hexadecimal-character key in `CONFIG_ROBUSTO_HALOW_SPI_STA_PSK`.
There is no generated key, passphrase derivation, default deployment key,
implicit open-security fallback, or borrowing of an unverified stored key.

Before opening transport or performing an explicit reset, validate SSID
(1-31 bytes), WPA-PSK (exactly 64 hexadecimal digits), bandwidth (1/2/4/8 MHz)
and frequency list (1-16 distinct positive 16-bit integers, no spaces).
After firmware-info (43), set SSID (4), PSK (12, WPA-PSK only), key management
(11), bandwidth (34), and channel list (39). Then set STA mode (41), enable
radio (65), verify SSID/security/bandwidth/list (48/86/62/64), and query
association (40). String readbacks accept exact bounded strings with or
without a terminating NUL; bandwidth and frequency values must match exactly.
The PSK write must be acknowledged, but the key is never queried or logged.

Submission errors, rejections, timeouts and mismatched/truncated readbacks
fail explicitly before host readiness. A failed write may leave partially
changed module settings; there is no rollback, additional retry or fallback.
Reset/reinitialization reapplies and verifies the same complete configuration.
AP and STA use shared validation, setters and readback logic.

The explicit frequency list overrides a stored frequency range. Country/
regulatory configuration, transmit power, automatic channel selection and
pairing are not changed. The existing module auto-save policy may persist
the configured changes. Acknowledged/readback settings do not establish a
lawful deployment, independent radio state, association or RF delivery.
STA link readiness still requires association.

Real Kconfig keys remain plaintext in local inputs and generated build
outputs and are embedded in firmware. Do not commit them or include them
in shared logs. Central's approved `-Menuconfig -BuildOnly` workflow saves
STA settings in an ignored local defaults file for subsequent clean builds;
see [Central's setup instructions](../../onwatch_central/README.md).
Event 16 (`HGIC_EVENT_REQUEST_PARAM`) is recognized and logged; its required
response remains unverified and is not invented here.

### AP network configuration

Under **Networking -> HaLow**, select AP and enable **Apply AP network settings
at startup** (`CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE`). This is optional and
defaults off, preserving existing firmware behavior. Enabling it requires:

| Setting | Required value |
| --- | --- |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_SSID` | 1-31 bytes; empty is invalid |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK` / `CONFIG_ROBUSTO_HALOW_SPI_AP_OPEN` | Explicit security choice; WPA-PSK is the default |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_PSK` | WPA-PSK only: exactly 64 hexadecimal digits, **not** a passphrase |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_BW_1`, `_BW_2`, `_BW_4`, `_BW_8` | BSS bandwidth choice in MHz; derived integer is `CONFIG_ROBUSTO_HALOW_SPI_AP_BSS_BW` |
| `CONFIG_ROBUSTO_HALOW_SPI_AP_FREQ_LIST` | 1-16 distinct comma-separated positive 16-bit integers, no spaces; center MHz multiplied by 10 (100 kHz units) |

Robusto supplies no default SSID, PSK or frequency plan; applications may
provide non-secret defaults in their own Kconfig. The 31-byte SSID limit is
conservative: the non-OS guide says at most 32 characters, while the AT guide
specifies fewer than 32. Both describe a 64-hex-digit PSK, bandwidths 1/2/4/8
and a maximum of 16 explicit frequencies. The pinned SPI source, not the
guide's inconsistent `int *` declaration, defines the frequency-list encoding:
16-bit little-endian count followed by 16-bit little-endian entries.

The complete configuration is validated before transport setup, including on
an explicit reset request. After firmware-info succeeds, startup sets SSID
(4), PSK when encrypted (12), key management (11), bandwidth (34), and channel
list (39), waiting for each acknowledgement. PSK precedes enabling WPA-PSK.
It then sets AP mode (41), enables the radio (65), verifies AP mode (145),
reads back SSID (48), key management
(86), bandwidth (62), and channel list (64), and finally probes the local raw
interface. Every readback must match before AP readiness can be published.
The PSK is never queried, printed or included in host command trace captures;
its successful write acknowledgement is not key readback or proof of STA
authentication.

Reinitialization reapplies the same settings. Submission errors, rejections,
timeouts and mismatches fail startup, with explicit logs and cleanup, not a
fallback to stored settings. Writes are not atomic: a failure can leave
partially changed module settings. No rollback or extra retry is attempted.
The module's existing auto-save policy is unchanged and **may persist changes
to its flash**; the vendor guide describes auto-save as enabled by default.
No additional SAVE, factory reset or reboot is added.

The frequency list overrides a stored frequency range. Automatic channel
selection, transmit power, regulatory settings and pairing state are not
changed. Choose a lawful frequency list/bandwidth supported by the module;
numeric validation is not regional certification. Central's STA must have
compatible SSID, security/key and RF settings. Matching configuration and
local AP readiness still do not prove beaconing, admission or RF delivery.

Kconfig secrets are plaintext in configuration/build outputs and embedded in
the firmware image. Supply real values locally; do not commit a real PSK.
Collector leaves this option disabled until explicitly selected and never
supplies a default PSK. Its application Kconfig supplies SSID `onwatch` and
frequency list `8655` (865.5 MHz); the existing security/bandwidth defaults
select WPA-PSK and 1 MHz for its EU deployment. These are nominal network
settings, not proof of regulatory compliance or measured performance. See
[Collector's configuration notes](../../onwatch_collector/README.md).

### Reset lifecycle

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

### Vendor-event coverage

Every event in the pinned `enum hgic_event` (IDs 1-30) is recognized in both
EVENT and EVENT2 packets. Names retain the upstream spelling:

| IDs | Names |
| --- | --- |
| 1-6 | STATE_CHG, CH_SWICH, DISCONNECT_REASON, ASSOC_STATUS, SCANNING, SCAN_DONE |
| 7-10 | TX_BITRATE, PAIR_START, PAIR_SUCCESS, PAIR_DONE |
| 11-16 | CONECT_START, CONECTED, DISCONECTED, SIGNAL, DISCONNET_LOG, REQUEST_PARAM |
| 17-22 | TESTMODE_STATE, FWDBG_INFO, CUSTOMER_MGMT, SLEEP_FAIL, DHCPC_DONE, CONNECT_FAIL |
| 23-26 | CUST_DRIVER_DATA, UNPAIR_STA, BLENC_DATA, HWSCAN_RESULT |
| 27-30 | EXCEPTION_INFO, DSLEEP_WAKEUP, STA_MIC_ERROR, ACS_DONE |

Event 31 has been observed in device logs but has no definition in the pinned
header. It is reported as `UNDOCUMENTED_31`, without assigning it a firmware
restart, AP-start or readiness meaning. Other unknown IDs remain unsupported
and generate warning/header diagnostics. Malformed known events are reported
as malformed, not unknown.

Layouts established by the pinned source are validated against the declared
payload before access: connection MAC (6 bytes), signal/EVM (2), bitrate (4),
connection-failure detail (at least 1), pairing lists (whole MAC addresses,
at most 8), and exception number/fields. Valid pairing lists replace the bounded
vendor pairing state; oversize or partial entries are rejected, not truncated.
The eight named exception codes are logged as errors, decoding only their
declared fields (CPU, heap/buffer usage, TX blocked/delay, background signal,
temperature and wrong password). Unknown exception numbers remain explicit
errors with their numeric code and payload length, not fabricated diagnoses.

Other known events are logged by name, ID and declared payload length with
`payload not decoded`. No firmware text, credentials or opaque payload bytes
are dumped, and no undocumented state transition or request response is
invented. In particular, CH_SWICH and ACS_DONE are named radio-related events,
not independent evidence of beaconing or admission. TX_BITRATE and SIGNAL
values continue updating vendor state, but their normal logs are limited to
four per transport initialization, followed by an explicit suppression notice.
Important connection/failure/exception events are not subject to that limit.

Normal logs recognize event 11 (`HGIC_EVENT_CONECT_START`) as a connection
attempt and event 22 (`HGIC_EVENT_CONNECT_FAIL`) as a connection failure,
instead of reporting them as unhandled vendor events. Event 22 must contain
at least one byte within its declared payload; missing/truncated data remains
malformed. Its first payload byte is logged in hexadecimal and decimal,
together with the declared payload length, for example:

```text
Connection failed (event 22): detail=0xa5 (165), payload=1 bytes; detail meaning unverified
```

This example is synthetic, not a decoded Central failure. The vendor detail
byte's meaning is not established; it is not labelled as a password, channel
or authentication error. Transport padding and remaining payload bytes are
not logged. Unknown events retain header-only diagnostics, and credential
responses remain excluded.

These logs are enabled without detailed SPI diagnostics and cover events
received during startup commands as well as runtime RX. They do not change
association/lifecycle state or AP readiness, issue configuration commands, or
alter retry behavior or timeouts. Existing association responses and connect/disconnect
events continue to control STA availability.

### Detailed diagnostics

Normal operation does not claim UART2, obtain a private SPI DMA descriptor or
require diagnostic capture to succeed. Lifecycle/error logs remain enabled.
Opt-in diagnostics retain the following behavior:

- SDIO CIS identity is always queried. Diagnostic runs additionally log up to
  32 initialization CMD52 exchanges, read back SDIO control/block-size
  registers and log successful per-command interface health checks.
- Commands 43, 41, 65, 40, 145 and 132 capture CMD53/data-block TX/RX, TX-buffer stability,
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

The presentation contract runs the production serializer/parser, message
admission/worker and queue-state helpers with HaLow enabled and disabled.
It verifies legacy output, the v1 high mask byte, round trips, malformed inputs,
name bounds and duplicate-peer mapping merges. It also checks blocked-queue
deduplication, independent replies/peers/reasons/media, failure cleanup,
dequeue/timeout/completion boundaries, three-byte result storage and exact-once
frees. The adapter contract exercises the production reservation under the
queue mutex, including retained reservations during QoS resets and driver work.

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
