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

The bundled backend calls `hgic_raw_send`, which constructs a 14-byte Ethernet
header (destination radio MAC, module source MAC, EtherType `0x66AA`) and adds
the 8-byte vendor header. The driver return must equal raw
payload length plus 22; only then return the raw payload length to Robusto.
A positive short write is an error. Submission uses the existing backend mutex
and vendor buffer; no IP stack or extra fragmentation is added. The
`hgic_raw_send_ether` comparison experiment was reverted after the supplied
Collector/Central logs still showed 117 full-sized fragments missing and only
the short final fragment stored. The receive path, credit handling and TX ceiling
are unchanged. Host tests verify the wire bytes at payload lengths 1, 256, 512,
1024 and 1642, including MACs, EtherType, cookie and payload.

### Optional normal FRM2 size sweep

This experiment is not enabled by default. Both endpoints have
passed the module-local TEST2 sweep below at 40 MHz while full normal JPEG
fragments still disappeared. TEST2 bypasses normal FRM2 processing and RF, so
its success does not locate that remaining loss.

Enable `CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX` on the receiving peer.
Enable `CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX` on **one sender only**; it selects
the receiver option locally too and requires a TX ceiling of at least 512.
Use a controlled two-device setup. The sender selects the first non-diagnostic
FRM2 source MAC that is nonzero and unicast while its link is ready; this is
wire-level peer selection, not Robusto-envelope validation. It logs the chosen
MAC. If no such frame arrives, it stays explicitly armed without claiming a
test result. Normal peer discovery/control traffic remains enabled.

Five seconds after selection, send a 24-byte start marker, then ten patterned
packets at each raw length **256, 384, 448, 490, 491 and 512 bytes**, in that
order, followed by a 24-byte end marker. These are individual normal FRM2
packets through the unchanged `hgic_raw_send`, not TEST2, JPEG fragments or
Robusto envelopes. Vendor/Ethernet overhead remains 22 bytes. Sends are paced
at least one second apart, without catch-up bursts; no RF/Robusto-level ACKs,
diagnostic retries or fragment recovery are added. A local write, credit or
readiness error explicitly aborts the sweep. `TX submitted` and `TX complete`
mean local submission only, **never remote delivery**.

Raw packets begin with `HLOWFS01`, followed by a little-endian 64-bit run value,
16-bit sequence and 16-bit exact length. Remaining bytes are deterministic
position/sequence/run-dependent patterns. Sequence 0 is start, 1–60 are data,
61 is end. The run value is sender uptime in microseconds at peer selection:
it distinguishes runs within that boot, not a globally unique identity across
reboots. There is no dynamic allocation. SPI clock, EtherType, normal ceilings,
vendor code, fragmentation and the independent local echo/write-check flags
are unchanged.

The receiver consumes diagnostic packets before Robusto envelope dispatch and
verifies the exact raw length and every byte. It fixes the source and run from
the first valid diagnostic packet. Each data sequence counts once. Missing
start/end markers, corruption, duplicates, foreign source/run and late packets
are explicit; malformed diagnostic magic instead follows normal envelope
rejection and cannot count as a verified diagnostic packet. After end plus a
two-second receive grace, or a 70-second deadline from the first valid packet,
report each missing sequence/size/trial, six size totals and a final
`FRM2_SWEEP RX PASS/FAIL ... verified=N/60 start=... end=...`. Receiver shutdown
also reports an unfinished active run. A receiver that sees no valid diagnostic
packet remains armed, not a pass. PASS requires all 60 distinct byte-exact data
packets, both markers, no corruption and no duplicates.

Each receiver accepts one run per backend start; foreign runs never replace
the active/completed result. **Restart both backends for another trial**.
Keep the receiver deployed/armed before starting the sender and capture both
logs. Collector deliberately pauses its example publisher when the sender
option is enabled; it does not resume it after the sweep. Disable the sender
option and restart to restore that publisher. Other application publishers
are not suppressed by the shared backend; ensure they are inactive for this
controlled experiment. Receiver-only and default-off devices keep their
existing application behavior.

Interpretation:

- Large isolated FRM2 packets missing while small ones arrive reproduces the
  size-dependent failure without JPEG reassembly or an application burst.
- All sizes passing shows the isolated normal path worked for this pattern
  and pacing; it does not certify JPEG-specific contents or sustained traffic.
- Either result still cannot, by itself, distinguish module processing, RF
  delivery and the peer's normal FRM2 SPI handoff.

Native tests cover STA/AP receiver-only and sender variants (including sender
combined with the existing write checks and local echo), every byte
mutation, lengths, source/run filtering, duplicate/missing/control/timeout
reporting, exact vendor wrapping and padded parsing at every size, pacing,
one-shot completion, explicit local failures, pre-Robusto RX consumption and
unchanged forwarding of non-diagnostic packets. Delayed sends do not catch up
with a burst.
The supplied hardware run `10184730` completed with 256 raw bytes verified
10/10 and all five larger sizes verified 0/10. Start/end markers arrived, with
no diagnostic corruption/duplicates observed, and Collector reported all
60 submissions with clean recorded SPI write checks. This reproduces the loss
without JPEG fragmentation or burst traffic, but does not measure the exact
transition within 256..384 or identify where the normal delivery path loses it.

### Optional one-byte normal FRM2 scan

This option defaults off and is native-tested. Collector has separately
enabled/deployed it with user authorization; deployment alone does not
establish a hardware cutoff. Set
`CONFIG_ROBUSTO_HALOW_SPI_FRM2_BYTE_SCAN_TX=y` on the sender in addition to the
existing sweep TX option to replace its coarse sequence with every raw length
from **256 through 384 inclusive**, in three passes:

1. Ascending, 256..384.
2. Descending, 384..256.
3. Ascending, 256..384.

These are 129 distinct lengths, each tested three times, for 387 data packets.
Pacing remains at least one second between submissions without catch-up,
diagnostic ACKs or retries. Expect about 6.5 minutes after the start marker,
plus normal startup and the five-second peer-selection delay. Publisher pause,
SPI clock, raw-send API, EtherType, ceilings and the separate local echo/write
checks remain unchanged. The byte-scan option defaults off; the original
60-packet coarse sweep remains available.

Byte-scan packets use **`HLOWFS02`**, the same header layout and pattern rules,
sequence 0 for start, 1..387 for data and 388 for end. The updated receiver
recognizes both `HLOWFS01` and `HLOWFS02`, fixing the mode as well as source/run
for an active trial. The old receiver cannot decode `HLOWFS02`: **deploy the
updated receiver before enabling the byte-scan sender**. Its boot log announces
`supports=HLOWFS01,HLOWFS02`; a byte-scan sender announces
`byte_scan=1 data_packets=387`. No RX-specific byte-scan setting is needed.
Restart both backends per trial as with the coarse test.

After end plus the same two-second grace, report every missing sequence with
its raw size and pass number, and 129 per-byte totals `verified=N/3`. The
byte-scan receive deadline is seven minutes from the first valid packet;
the coarse deadline remains 70 seconds. The final transport summary is
`FRM2_SWEEP RX PASS/FAIL ... verified=N/387`. PASS still requires every data
packet, both controls and no observed corruption/duplicates.

Separately, `FRM2_BYTE_SCAN boundary established=1 last_full=L first_missing=F`
requires **F = L + 1**, all tested sizes up to L received 3/3, all tested sizes
from F upward received 0/3, both markers, completed receive grace and no
corruption/duplicates. Shutdown/timeout summaries cannot establish a cutoff. It also
prints adjacent raw lengths, vendor lengths (raw + 22), and 64-byte block
counts. Finding a stable cutoff therefore accompanies transport **FAIL**
because frames above that cutoff are missing; it is not a successful link test.

Mixed 1/3 or 2/3 reception, a larger passing size above a failing one, all
passing, all missing, incomplete controls or integrity failures cannot establish
a cutoff. Report `established=0`, `mixed_sizes`, `monotonic` and per-size counts
rather than asserting a single threshold. This is an observed boundary across
three trials per size, not proof of a universal MTU or a cause.

For interpretation, raw 298/299 maps to vendor 320/321 (five/six SPI blocks),
and raw 362/363 maps to vendor 384/385 (six/seven blocks). Alignment with one of
these boundaries is only a clue: local TEST2 already worked across those block
counts. Raw/vendor lengths are not a measurement of the module's over-the-air
frame format.

Native tests verify all 387 lengths/order/identities and padded vendor frames,
all 128 possible adjacent cutoffs in the tested interval, mixed/non-monotonic
and unbounded results, bitmap boundaries beyond sequence 255, mode isolation,
the extended timeout, local failures, and coarse compatibility. STA/AP variants
also combine both sender modes with the existing write checks and local echo.

The completed hardware byte scan, run `8954846`, received every raw length
256..340 at 3/3 and every length 341..384 at 0/3. Both markers arrived, with
zero observed corruption/duplicates: `verified=255/387`,
`established=1 last_full=340 first_missing=341`. Collector submitted all 387
packets. Raw 340/341 maps to vendor 362/363, both padded to 384 bytes in six
SPI blocks. This observed boundary is therefore not a block-count transition.
It does not identify the failing stage or prove a software cause.

### Optional randomized tight normal FRM2 scan

Native-tested; Collector has separately enabled/deployed this sender with
user authorization (ESP-IDF 6.0.3, COM7, image `0x964c0`, hashes verified).
This does not establish peer readiness or a hardware tight-scan result.
Select `CONFIG_ROBUSTO_HALOW_SPI_FRM2_TIGHT_SCAN_TX=y` with sweep TX enabled.
The sweep-pattern choice makes coarse, byte-scan and tight TX mutually
exclusive; its default remains coarse. An existing byte-scan selection is
preserved. RX remains a separate option selected automatically by sweep TX.

The tight scan sends **five packets each at raw 337, 338, 339, 340, 341 and
342 bytes**, a balanced 30-packet set shuffled using Fisher-Yates. Two hardware
random words form a nonzero 64-bit run identity. Its XOR-folded 32-bit seed
drives a reproducible LCG shuffle; this is not security-grade randomness.
A zero run identity explicitly aborts without retry. The peer derives the
same expected order from the run, rather than trusting a received length.

The new **`HLOWFS03`** format retains the header layout but salts its payload
pattern separately from the older modes, including control markers. Sequence
0 is the 24-byte start, 1..30 are shuffled data, and 31 is the 24-byte end.
**Deploy and confirm the updated receiver before activating tight TX**:
its banner must include `supports=HLOWFS01,HLOWFS02,HLOWFS03`. The previously
deployed dual-format receiver is insufficient. No RX-specific tight setting
is needed. The sender announces `byte_scan=0 tight_scan=1 data_packets=30`.

Pacing remains one packet per second without catch-up bursts or diagnostic
retries: approximately 31 seconds from start to end, then two seconds grace.
The receive deadline is 70 seconds from the first valid packet. Normal
peer selection/start delay, SPI settings, ceilings, raw-send path, local
echo/write checks and publisher pause are unchanged. Restart/arm the receiver
before the sender starts; one run per backend start, as with the other modes.

RX reports six `verified=N/5` counts and a final `verified=N/30`. The same
strict boundary conditions apply, now under `FRM2_TIGHT_SCAN boundary`:
all five received at each size below an adjacent cutoff, none at sizes above,
both controls, completed grace, no corruption/duplicates. Mixed counts,
non-monotonic reception, all-pass/all-missing or incomplete runs do not
establish a cutoff. Repeating the measured boundary would give sizes 337..340
each 5/5, sizes 341..342 each 0/5, and a transport **FAIL** with 20/30 received.
Interleaving tests timing/order effects; a precise result alone neither proves
a programmatic cause nor rules out power-related effects.

Native tests cover balanced/reproducible shuffles across 128 seeds, every
wire byte and length, all five possible adjacent boundaries, mixed and
non-monotonic results, incomplete controls, corruption, duplicates, timeout,
mode isolation, real vendor TX framing/pacing and zero-identity failure.
The full SPI matrix includes 14 STA/AP diagnostic variants, retaining coarse
and byte-scan coverage and combinations with write checks/local echo.

### Optional module-local echo sweep

`CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP` defaults off. Collector temporarily enables
it to test both SPI directions without relying on radio delivery. After normal
role configuration/readback and the existing four-byte AP probe, but before
readiness or the receive task starts, send ten TEST2 local echoes at each of:

| Raw-frame-equivalent bytes | Vendor packet bytes | 64-byte blocks |
| --- | --- | --- |
| 256 | 278 | 5 |
| 384 | 406 | 7 |
| 448 | 470 | 8 |
| 490 | 512 | 8 |
| 491 | 513 | 9 |
| 512 | 534 | 9 |
| 1024 | 1046 | 17 |

TEST2 uses an eight-byte vendor header followed by a synthetic payload. Choose
payload length as raw-equivalent length plus 14 so the complete vendor packet
and padded block count match normal raw-frame submission (raw plus 22). This
does not change Collector's normal 512-byte radio TX ceiling. Pattern bytes
vary with position and a monotonic per-trial identity; the echo must match both
the exact declared payload length and every payload byte, including the tail.
Padding is excluded. Identities are not reused during reset/reinitialization
within a host boot, and exhaustion fails explicitly rather than wrapping.

Each size produces one `SPI_ECHO PASS ... verified=10/10` summary; a complete
sweep reports `70/70`. Errors identify size/trial and submission, receive,
timeout, length or first mismatching payload offset. There are no payload dumps.
The clock remains 40 MHz; the existing TX credits, response timeout and SPI
transport are used. No test retry or fallback is added. Failure stops startup
without publishing readiness. The separate configured module-reset policy is
unchanged and reapplies the sweep on normal reinitialization.

The module echoes TEST2 locally, not over RF. A passing sweep demonstrates
byte-exact round trips through the host/module SPI transport for those sizes
and conditions. It does not establish that normal FRM2 processing, over-the-air
delivery or Central's SPI receive path works. The write checker below captures
normal FRM2 only; TEST2 results are established by the byte-exact echo rather
than treating local submission as success. Default-off devices are unchanged.

### Optional SDSPI write checks

`CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS` defaults off. Collector enables it locally
to instrument the existing pinned driver without changing its source, download
or integration. The project-owned SPI callbacks observe each raw-frame write,
checking one block-mode CMD53, its declared block count, the actual block count,
the submitted non-padding bytes against the vendor buffer, each SD SPI
data-response token, one stop token and busy release. The driver clock remains
40 MHz. No additional transfers, delays, retries or timeout changes are made.

The pinned multi-block loop ignores the block helper's return and final busy
wait result. These checks independently detect rejected/missing data-response
tokens or busy remaining asserted after the driver's existing waits, even if
the outer vendor write reports the requested byte count. Detected failures log
an error and fail the normal raw-write path; the backend becomes faulted rather
than reporting success. Explicit reset/reinitialization is still required.
The checker does not rewrite the driver's internal response decoder.

`SDSPI_CHECK` records vendor byte length (raw length plus 22), observed/expected
and declared block counts, CMD53/stop counts, raw response/busy byte pairs in
block order, busy-read count, first bad zero-based block (`-1` when unavailable),
the original vendor return, and elapsed microseconds. `flags` is a bitmask:

| Bit | Meaning |
| --- | --- |
| `0x01` | CMD53 count/mode or declared block-count mismatch |
| `0x02` | Observed block-count mismatch or capture bound exceeded |
| `0x04` | Submitted non-padding bytes differ from the vendor buffer |
| `0x08` | Missing, rejected or unexpected data-response token |
| `0x10` | Busy not released before the next block/stop or write completion |
| `0x20` | ESP-IDF SPI transaction failed |
| `0x40` | Unexpected command/block/stop ordering or missing stop |
| `0x80` | Vendor byte-count return mismatch |

Accepted SD SPI data-response tokens have low five bits `0x05` (including the
captured `0xe5`); `0x0b` and `0x0d` indicate CRC/write rejection. Raw token bytes
are retained rather than printing the payload. Successful metadata is logged
only for the first four checked writes, first occurrence of each block count
and every 64th write. Every failed check is logged. There is no bulk trace dump
or logging inside an individual SPI transaction.

This is local submission/response evidence, not proof of RF delivery. Comparing
the submitted software buffer cannot detect every on-wire corruption, and the
checker does not add RX CRC validation. Commands, keys and credit-query packets
are outside this raw-frame-only capture. Central's default behavior is unchanged.
Native tests exercise the real pinned vendor block loop at raw sizes
1/256/384/448/490/491/512/1024/1642, including positive vendor returns despite
injected token rejection and ignored per-block/final busy timeouts. STA/AP
backend tests cover callback wiring, fault gating, no implicit retry and bounded
logging. Hardware behavior remains unverified until a diagnostic firmware is
explicitly flashed and logs are collected.

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
Replies and small ordinary traffic keep their existing admission policy. See
[presentation ownership](../components/robusto/network/presentation.md)
for the mutex-protected reservation and deferred-free contract. This prevents
repeated offline requests from exhausting the queue; it does not fix association.

Lifecycle/configuration calls must be serialized by the firmware. The
adapter serializes physical writes with a mutex; a receive-side receipt or
fragment response can write while the send worker waits.

### Transmit diagnostics

The temporary per-fragment INFO attempt/local-write logs have been removed
after diagnosis. Short/failed writes and transport errors remain reported.
A successful local write alone is not proof of RF delivery.

`CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE` is a default-off temporary
one-shot SPI trace. It does not enable the startup/UART diagnostics option.
Collector's local configuration now disables it after a captured watchdog abort
during the synchronous UART dump. No vendor-source modifications or vendor patches are needed:
the project-owned SPI callbacks already see the response bytes.

The first outgoing fragment request arms the capture using its advertised
transfer hash/count. Fixed memory holds the request and up to 64 fragment
records. Credit refusals retain a call/busy count and first/latest envelope
sequences; they do not create an artificial successful-write record. Metadata
includes destination, frame/vendor lengths, actual vendor/backend returns,
credit windows and recorded ESP SPI errors. `raw_called`, `raw_bytes`,
`hdr_bytes`, `cookie`, `ifidx` and `sdspi_rc` separately identify entry to the
project raw writer, the actual vendor-header length/cookie/interface and the
SDSPI return. A record with `called=0` never reached `hgic_raw_send`; an
unvisited fragment has no record.

During the vendor call, capture records actual SPI-call/error counts, the
CMD53 declared block count and ten raw response-window bytes, actual
64-byte data-block count, each block's raw response/busy bytes (offsets 67/68
of the existing 69-byte exchange), stop-token count, and busy-read zero/nonzero
byte totals. Up to 26 block samples fit each record, covering the 1664-byte
physical maximum. These are raw observations, not a new response decoder or
an assertion of module acceptance. Existing vendor error handling is unchanged.
`wire_seen`/`wire_match` report whether a completed first data-block exchange
contained the expected destination and RH header/hash/index/sequence/length
in the SPI TX buffer. This is not a DMA-memory or on-wire logic-analyzer check.
Busy-read totals cover reads during that vendor call; a vendor-internal failure
reinitialization can also contribute reads/commands.

The outgoing worker freezes and prints `TXTRACE` summaries after the initial
fragment pass, or on request failure, outside the adapter/SPI mutexes.
No per-block logging, extra SPI calls, delays or retries are introduced.
Summary output itself still consumes time after the pass. The summary reports
the declared/recorded fragment counts, capacity, truncation and unrecorded calls; block
sample overflow is reported separately. Records cannot be overwritten by
retransmissions or later images and remain one-shot across module reset;
reboot the host for a fresh capture.

For the current 55,002-byte image, expect 55 records with `frame=1024`,
`vendor=1046`, `declared_blocks=17`, and a final index 55 with `frame=362`,
`vendor=384`, `declared_blocks=6`. Expected counts are not proof that the
observed counts or response bytes agree. No payload bytes or credentials are
captured or printed.

### Transmit backpressure

The SPI backend treats fewer than two TX credits as `ROB_ERR_QUEUE_FULL`:
no data frame was submitted. It requests fresh credits immediately if no
SOFTFC query is already pending. A failed credit query remains a transport
failure, not a retryable busy result.

For fragment data and outgoing transfer-init/status packets, the adapter returns
busy immediately. The outgoing worker retains the unsent packet's bytes/index,
services small traffic and yields
one scheduler tick before checking again. It holds neither the adapter TX
mutex nor the SPI mutex while waiting, so RX can read the SOFTFC response.
Busy neither counts as a successful fragment nor enters status/resend recovery.
Submission resumes at the same fragment when credits return, bounded by the
existing **30-second whole-transfer deadline**, not the 100 ms receipt timeout.
Permanent starvation is an explicit transfer timeout; hard transport errors
still fail the attempt rather than being silently retried.

RX records resend maps and transfer results in a mutex-protected feedback
mailbox; only the outgoing worker executes retransmissions and owns outgoing
transfer cleanup. The mailbox has independent storage, and is detached under
the mutex before freeing it. Unknown late/unmatched feedback is warned and
ignored. Exact duplicate completion results for recently finished outgoing
transfers are recognized separately (see completion handling below);
RX never retains a pointer to the outgoing transfer. Fragment status is
probed at 250 ms intervals while awaiting feedback, but crossing one interval
does not terminate the transfer. This also prevents small-message receipt
retries from falsely timing out the image's status wait.

Receive-side acknowledgements and control responses remain nonblocking:
RX itself must remain able to read credit responses. Busy RX control responses
are explicitly reported, not buffered. Other outgoing small/non-fragment control messages
retain their existing 10 ms credit polling, receipt-timeout-bound credit wait,
and identical-frame/correlation-sequence retry behavior. Receipt timeout
settings and non-HaLow media behavior are unchanged.

Only one fragmented HaLow message per peer may be queued or running.
Admission returns `ROB_ERR_QUEUE_FULL` for another until the existing worker
finishes, including its fragment-result wait or failure/timeout. Reservations
are released on allocation/admission failure and worker completion. Small
messages and different peers retain their existing bounded queue admission.
Native PubSub uses the same generic two-phase admission API for every medium:
`robusto_prepare_send()` before allocating/copying the encoded message, followed
by `send_message_raw_admitted()`, or `robusto_cancel_send()` if construction
fails. A busy peer therefore does not cause another image-sized allocation.
PubSub does not inspect HaLow fragmentation thresholds or call a
HaLow-specific send entry point. The existing ESP-NOW queue preflight remains
unchanged.

Queues may provide optional `on_admit_cb` / `on_release_cb` policies. HaLow's
backend alone selects its fragmentation boundary, early queue preflight and
per-peer reservation. Ordinary `send_message_raw()` uses the same admission
machinery implicitly, without adding the early preflight to ordinary enqueue.
Media without an admission policy retain their normal queue behavior.
An admission token must be zero-initialized and not copied while active.
Cancellation is idempotent; an admitted send consumes the token on success or
failure, but transfers payload ownership only on `ROB_OK`. Ownership moves to
the work item before it becomes visible to a worker, including when completion
occurs before enqueue returns. Allocation/enqueue failure and final worker
completion release the admission exactly once, after any retries or cross-media
handoff. Media discard callbacks must also release admission for discarded
items; those callbacks run under the queue lock, so their release policy must
respect that locking context. Small HaLow work does not release another
message's large-transfer reservation.

The native presentation/PubSub contracts exercise this generic lifecycle with
HaLow enabled and disabled, including a non-HaLow admission policy, failure
rollback, completion before enqueue returns and reservation lifetime across
send retries. The adapter matrix checks the production HaLow hooks at the
exact fragmentation boundary, as well as blocked/full preflight and implicit
enqueue behavior. These are native tests, not deployment or RF evidence.

The same outgoing worker cooperatively services one queued small message
before a fragment packet, while waiting for data-frame TX credits, and during
fragment state/result waits. It can bypass queued large messages, but does not
start another worker or another simultaneous receipt request. Nested small
sends cannot recursively drain the queue. RX never runs this service or waits
for TX credits. A small message can still wait for the current synchronous
receipt operation; this is not a separate preemptive channel.
This policy prevents image backlog; it does not guarantee delivery or 1 Hz
image throughput. Producers must report unsuccessful admission rather than
treating it as delivery.

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
| `CONFIG_ROBUSTO_HALOW_TX_FRAME_MAX` | Outgoing raw-frame ceiling, including the Robusto envelope; 256-1642 bytes, default 1024; independent of the RX/SPI capacity |
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

### Minimum transmit MCS

MCS means **Modulation and Coding Scheme**: the radio's choice of modulation
and error-correcting code rate. `CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN=2` sets a
**minimum for automatic transmit-rate selection**, not a fixed transmit rate.
The module can select MCS2 or a higher supported MCS as conditions permit.
It does not set 2 MHz bandwidth, alter the SPI clock, or change the peer's
transmit configuration. Configure each transmitting endpoint separately;
the selected Collector/Central operating profile applies the floor on both modules.

At 1 MHz and one spatial stream, MCS2 is QPSK with coding rate 3/4, with a
nominal PHY rate of 0.90 or 1.00 Mbit/s depending on guard interval
([802.11ah rate table](https://en.wikipedia.org/wiki/IEEE_802.11ah)).
These are radio bit rates before framing, acknowledgements, contention,
processing and application overhead. A minimum of two does not establish
which MCS or guard interval a particular packet actually used, nor the maximum
sustained application throughput. Higher floors prevent adaptation to the
more robust lower rates, which can reduce weak-link reliability or range.
A short intended link distance is not a guarantee of adequate RF conditions.

`CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN` defaults to `-1`, leaving the module's
setting unchanged; `-1` does not restore a previous module setting.
Values `0..7` apply `AT+TX_MCS_MIN=<value>` during normal
startup and after explicit reinitialization. Before publishing readiness, the
backend requires command 115 acknowledgement and the exact asynchronous
`FWDBG_INFO` value report, then sends `AT+TX_MCS_MIN=?` and independently
verifies both again. Rejection, missing acknowledgement/report, mismatched
value or transport failure fails startup explicitly; there is no fallback.
Vendor sources are unchanged. The command path and report format were traced
in the matching 1.6.4.5-38054 module firmware; other firmware must provide the
same response contract to use this option.

For value two, the set command is `AT+TX_MCS_MIN=2` and each value report must
be exactly `limit tx mcs min= 2\r\n`, with an optional single trailing NUL.
The acknowledgement and report may arrive in either order. The independent
query must satisfy the same acknowledgement/report contract before the backend
logs `TX_MCS_MIN=2 set and independently queried/readback verified`.
That banner establishes the configured rate floor, not remote packet delivery.

#### Why this changed full-frame delivery

The analyzed firmware also uses the floor for packet-length admission:
in 1 MHz mode, minimum zero
selects a special 383-byte internal budget. For normal non-QoS raw frames,
`ALIGN_UP(raw_length + 32, 4) + 8` is compared with that budget, predicting
raw340 acceptance and raw341 rejection. Minimum two selects a 2297-byte
internal budget in that check, above the 1684-byte checked length of a
1642-byte raw frame. These internal budgets are not application payload sizes,
Ethernet MTUs or SPI transfer ceilings. The rule is specific to the analyzed
firmware and path, not a universal 802.11ah packet-size limit.

#### Selected operating profile (2026-10-29)

Following the successful MCS4 image-transfer capture below, the user explicitly
selected retaining minimum MCS4 on both devices. This is the current normal
operating profile, not a pending rate-change experiment:

| Setting | Value |
| --- | --- |
| Channel bandwidth | 1 MHz, unchanged |
| `CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN` | `4` on both endpoints |
| `CONFIG_ROBUSTO_HALOW_TX_FRAME_MAX` | `1642` raw bytes |
| SPI clock | 40 MHz, unchanged |
| Echo/FRM2 sweeps, all sweep TX patterns, write checks, RX/TX traces | Disabled |
| Credentials and configured one-shot reset recovery | Preserved |

Require `TX_MCS_MIN=4 set and independently queried/readback verified` at
startup; command acknowledgement or AP interface readiness alone does not
prove peer association or image delivery. The earlier MCS2 baseline and MCS7
trial remain historical evidence, not the selected settings. The successful
short MCS4 capture does not establish long-duration reliability, maximum
throughput or actual per-frame MCS. Further rate changes, diagnostic activation
or hardware actions require a new explicit instruction; this selection alone
requires no rebuild or flash because both devices are already configured at four.

With the existing v2 format, a full raw frame contains 20 envelope bytes,
22 fragment-header bytes and 1600 fragment-data bytes. The Ethernet/vendor
wrappers add 22 bytes, making a 1664-byte complete vendor packet.
This keeps normal Robusto fragmentation; it does not introduce IP or rely on
the failed module fragmentation-threshold command.

#### Observed hardware validation

On 2026-10-27, Central's capture recorded the verified MCS2 banner and link
readiness after restart, then 11 complete 55002-byte image messages, each
reassembled from 35 fragments with `incoming_rc=0` and queued to the image
subscriber (frames 272..282). There were no missing-fragment warnings in that
capture. Each message used 34 full 1600-byte data chunks and a final 602 bytes;
normal SPI RX explicitly logged 1642-byte raw frames. Request-to-reassembly
times were 404..794 ms, averaging 602 ms.

A subsequent user-provided capture on 2026-10-29 showed four more complete
messages (frames 275..278), taking 762..774 ms each. That is approximately
0.57 Mbit/s of reassembled message bytes during a transfer, not a measured
maximum PHY or sustained link rate. Publishing one 55002-byte message every
five seconds instead averages about 88 kbit/s because of the scheduled idle
time. Subscriber queuing does not independently establish image rendering,
sender receipt of final acknowledgements, or long-duration reliability.

Together, the firmware admission-rule analysis and normal full-frame delivery
support using minimum MCS2 for this tested setup; local TEST2 echo success
alone did not establish this. Geographic RF profiles remain deployment-specific;
the EU example is not a project-wide bandwidth limit.

#### Minimum-MCS7 trial (2026-10-29)

The authorized follow-up changed the minimum to 7 on both endpoints, retaining
1 MHz bandwidth, raw1642, disabled diagnostics, credentials and reset policy.
Both deployments passed flash hash verification and independently set/queried
minimum MCS7. Collector's deployment capture showed AP interface readiness
with `associated=0`; this was not evidence of peer association.

Central initially disconnected and did not reconnect. After the user restarted
both devices, Central again verified minimum MCS7 at 7390 ms and started with
`associated=0`. Its capture through 131660 ms contained 246 connection-failed
events (event 22, mostly detail `0xff`, meaning unverified), no connected event
or link-ready transition to 1, and no image requests, reassemblies or queued
images. No separate post-restart Collector capture was available.

This trial established neither stable peer association nor an MCS7 throughput
measurement. It does not establish that MCS7 is unsupported or identify the
failure's cause. No automatic rollback or further reset was performed; the
minimum-MCS2 results above remain the successful baseline.

#### Minimum-MCS4 trial (2026-10-29)

The next authorized trial set minimum MCS4 on both endpoints, retaining
1 MHz, raw1642, disabled diagnostics, credentials and reset policy. Flash
hashes passed and both endpoints independently set/queried minimum MCS4.
Collector's deployment capture reported `associated=0` and
`ap_interface_ready=1`, not peer association.

After the user restarted/power-cycled both devices normally, Central verified
minimum MCS4 at 7390 ms but showed no connected event or link-ready transition
to 1. Its capture through 133660 ms (approximately 126 seconds after readback)
recorded 247 connection-failed events (event 22, mostly detail `0xff`, meaning
unverified), with no image requests, reassemblies or queued images.
No separate post-restart Collector capture was available.

That capture provided no MCS4 throughput measurement or established cause. This observation
does not prove that minimum MCS4 always fails or establish a weak-RF diagnosis.
Both endpoints remained configured at minimum MCS4; no automatic rollback,
further reset or build was performed. The minimum-MCS2 success and minimum-MCS7
trial above remain separate historical results.

A later user-provided 161-line Central snapshot on the same date demonstrated
successful MCS4 operation in that run: independent minimum-MCS4 readback,
`associated=1`, link ready and completed peer presentation/subscriptions.
Three 55002-byte messages each reassembled from 35 fragments with
`incoming_rc=0`; images 331..333 were queued without replacing pending images.
Chunks were 1600 bytes, with a final 602 bytes, and SPI RX logged raw1642.
Request-to-reassembly times were 404, 394 and 394 ms (mean 397.3 ms), about
1.1 Mbit/s of reassembled message bytes during transfers, not a sustained or
PHY benchmark. There were no missing-fragment warnings or later disconnects
in this short snapshot. The difference from the earlier no-association run
remains unexplained. A configured minimum does not prove actual per-frame MCS.

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

## HaLow fragmentation v2

HaLow fragmented messages use a versioned identity independent of the content
CRC. Both peers must upgrade together; an unversioned HaLow fragment packet is
rejected with an explicit error, with no legacy fallback. Non-HaLow media retain
their existing protocol.

The common prefix remains CRC/hash (4 bytes), context (1 byte), subtype
(1 byte). The subtype has bit `0x80` set for v2. Immediately after this prefix:

| Offset | Field |
| --- | --- |
| 6 | 64-bit little-endian transfer ID |
| 14 | 32-bit little-endian recovery round |
| 18 | Remaining legacy fields (if any) |

Requests are 34 bytes (length/count/chunk/content CRC at offsets 18/22/26/30),
data packets have a 22-byte header (index at 18), checks are 18 bytes,
results are 20 bytes (signed 16-bit result at 18), and resend responses are
18 bytes plus one receipt byte per fragment. Request CRC covers the original
18-byte legacy request body, with the subtype's version bit cleared; the outer
envelope CRC protects the complete versioned packet including ID and round.
Request round is zero; status checks/resend maps use nonzero rounds.

The outgoing worker generates the ID from a 32-bit boot nonce (`esp_random()`
on ESP-IDF) and a monotonically increasing 32-bit counter, independent of the
content hash. Counter wrap is rejected explicitly. IDs do not repeat within a
boot; reboot separation is probabilistic, not persistent exactly-once identity
or an authentication token. Native tests use their simulated clock as nonce.
Repeated byte-identical payloads have distinct IDs and are delivered separately.
Peer, medium, ID and content hash bind active state and cached results.

Each status probe opens a new recovery round. The receiver echoes that round
in its missing-fragment response. The sender validates bitmap length and
0/1 values, accepts at most one response per open round, ignores stale/future
round responses, and OR-merges receipts into its acknowledged map.
Duplicate feedback cannot initiate another pass; the same missing map in a
new round can, because the retransmission itself may have been lost.
Only the outgoing worker resends. The receiver accepts delayed data from older
rounds, ignores stale checks, and sends missing maps only in response to checks,
not spontaneously on retransmission boundaries. Completion can arrive in round
zero or a previously issued round and takes precedence over resend feedback.

The receiver checks the content CRC, hands the message to incoming processing
once, and caches the actual handoff result. A failed handoff is reported as a
failure, not success. Late requests, data and checks for a cached completed ID
replay that result without delivering the payload again. Losing a completion
reply is recovered by a subsequent check. This cache is bounded: eight entries
shared across media, with HaLow entries retained up to 30 seconds, subject to
oldest-entry eviction; other media keep their existing 2-second TTL.
Suppression is not guaranteed after expiry, eviction, reinitialization or reboot.
The sender's 30-second overall deadline is unchanged. Small-message semantics
and the single-fragmented-transfer admission policy are unchanged.

Status checks can overlap ordinary completion: the original result ends the
outgoing transfer, then an already-issued check elicits a replayed result.
The sender retains eight consumed completion identities/results for two seconds
in mutex-protected static storage, separate from the receiver's replay cache.
A late completion is classified as a duplicate only if peer, transfer ID,
content hash and result match, its round was already issued, and the entry
has not expired or been evicted. Such duplicates are debug diagnostics, not
warnings or a second application delivery. Unknown, contradictory, future-round
and expired late results, and unmatched resend maps, still warn. Failed or
timed-out transfers without a consumed completion do not create an entry.
Native regression tests reproduce the overlapping original/replayed results
with delayed data delivery and verify those warning distinctions, including
the full1642/35-fragment case. This does not change probe timing or remedy
radio association loss.

## Size limits

The bundled LilyGO driver raw API allows 1778 bytes, but its SPI writer
limits a write to 1664 bytes. Subtracting 22 vendor bytes gives a source-level
raw frame ceiling of 1642 bytes.

- RX/SPI raw-frame capacity: **1642** bytes maximum.
- TX raw-frame ceiling: **1024** bytes by default; configurable with
  `CONFIG_ROBUSTO_HALOW_TX_FRAME_MAX` (256-1642).
- Envelope: **20** bytes.
- Outgoing Robusto packet: **1004** bytes before fragmentation by default.
- Outgoing fragment data: **982** bytes by default, plus the 22-byte v2
  fragment header.

The 1024-byte TX default uses the pinned vendor raw-data demo's
`SPI_TX_TEST_LEN`, not the SPI writer's capacity as evidence of an RF MTU.
The 55002-byte native image message consequently uses 57 fragments.
The receiver retains the full 1642-byte capacity and honors the
fragment size advertised in the sender's request, including when its own
outgoing limit differs. The backend must return errors for truncation.
Fragment feedback uses one byte per fragment plus 18 control bytes and the
20-byte envelope. The receiver's TX ceiling must fit that feedback frame:
for example, a 55002-byte message in 214-byte chunks needs 258 fragments and
a 296-byte feedback frame. A 256-byte sender with a default-1024-byte receiver
can carry this feedback; two peers both capped at 256 bytes cannot.
Neither the smaller default nor the SPI capacity establishes successful
over-the-air delivery or the module's exact RF limit.

## Module startup radio control

The experimental module fragmentation-threshold override and its Kconfig
settings have been removed. Startup and reset/reinitialization no longer send
command 10. Earlier trials with values 0, 1500 and 20 were rejected with status
-524 by firmware 1.6.4.5 SVN 38054; this does not distinguish unsupported values
from an unsupported command. Removal does not restore any module-persisted
setting, and does not change Robusto's own fragmentation or TX frame ceiling.

`CONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START` defaults off. When enabled,
startup sends command 65 with its one-byte argument zero as the first module
command after SPI initialization. Require its acknowledgement before proceeding.
Firmware-info (43), network settings and mode selection (41) follow.
This sequence also applies after explicit reset/reinitialization; the separate
transport-only reboot path still sends reset command 132 without normal startup.
The later explicit radio-enable command 65/one is retained. Submission failure, negative
acknowledgement or timeout fails startup and prevents link readiness; no retry,
alternate encoding or fallback is attempted. The radio-off acknowledgement is
not independent RF-state readback, and later network/mode commands may reactivate
the radio. STA/AP host tests check ordering, reset reapplication and early-command
failure gating, and reject any attempt to send the removed command 10.

## Receive diagnostics

For temporary boundary tracing, enable `CONFIG_ROBUSTO_HALOW_RX_TRACE`.
The library default is off. Central's LilyGO overlay enabled it during the
earlier investigation; the selected normal MCS4 profile leaves it disabled.
Disable it after capturing a diagnostic failure.

- `RXTRACE SPI` logs positive module reads before wire parsing, including short
  or malformed packets and control/events, not just Robusto data frames.
- `RXTRACE HaLow` logs receive-function entry before state, readiness, envelope,
  CRC or peer checks. It includes byte count and safely readable header fields.
- `RXTRACE FRAG_MESSAGE` separately identifies fragment-data candidates with
  sequence, hash and index, even if later validation rejects them. Missing index
  bytes are marked with `index_present=0`.

All trace headers are explicitly `unvalidated=1`; they are not evidence of
integrity, successful admission, reassembly or over-the-air transmission.
No payload bodies or keys are dumped. SPI and receive-entry traces log the first
128 occurrences and every 64th thereafter. Fragment-data candidates have an
independent budget: the first 32 and every 64th thereafter, so an initial data
candidate is not hidden by earlier non-fragment traffic. SPI budgets reset on
transport initialization; adapter budgets span the firmware process lifetime.
Disabled builds contain none of this temporary tracing.

At INFO level, the SPI backend reports its first validated data frame and every
64th frame thereafter. The adapter independently reports its first CRC-valid
HaLow envelope and every 64th envelope. These counters describe different
boundaries, include receipts, and are not a delivery-success count. SPI counts
restart on transport initialization; adapter counts span the initialized medium's
lifetime. Frames withheld because the link is not ready are reported separately,
on the first occurrence and every 64th occurrence.

At DEBUG level, HaLow transfer diagnostics report one newly initialized receive request with its
hash, total bytes, fragment count and advertised chunk size. Stored-fragment
progress is logged for index zero, every 32nd index and the last index, only on
first reception of those indices. Duplicate packets do not repeat progress logs.
After full-message CRC verification, the incoming handoff is reported with its
return code; a rejected handoff is an ERROR, not a successful-delivery message.
An accepted handoff does not prove that an application callback has run.
Normal INFO logging omits receive-request details, stored-fragment progress and
successful-reassembly details. They are controlled by the normal Robusto log
level, not the optional `CONFIG_ROBUSTO_HALOW_RX_TRACE` setting. Startup/readback
summaries, warnings and errors remain at their existing levels. No payload bytes
or keys are included in these diagnostics.

Every HaLow check that finds all fragments missing now emits an adjacent WARN
snapshot, `HaLow RX all-missing`, with the transfer hash, statistics level,
fragment request/message counts, invalid-reference/index, wrong-length, fragment
CRC and allocation-failure counts, plus current SPI data-frame, link-unready
and CRC-valid-envelope counts. It does not wait for a periodic log or Central's
recovery path. Partial-loss checks do not add this snapshot.

The snapshot is explicitly `scope=totals`: fragment counters cover all peers and
media since their last reset, SPI counters restart on transport initialization,
and envelope counts span the initialized medium's lifetime. Compare successive
snapshots, not just the transfer hash. At VERBOSE statistics level, `rx_msg=0`
means no fragment-message handler entries were counted; a rising `rx_msg` with
rising invalid-reference/index or length counters identifies rejected fragments.
`crc` describes fragment-protocol CRC failures, not the outer HaLow envelope CRC.
SPI frames and envelopes include receipts and other non-fragment data, so they
do not by themselves prove image-fragment arrival.

`spi_rc=0` marks valid SPI counters. Other values mean the SPI snapshot failed or
is unsupported; its zero-valued fields must not be interpreted as no traffic.
Envelope counts remain available in either case. The optional driver snapshot
callback reads counters under the existing SPI mutex, performs no radio/SPI I/O
and does not reset counters or consume statistics deltas. The getter is restricted
to serialized RX-task use outside the backend lock. Snapshots do not alter
resend requests, admission, timeouts, retries or queue behavior.

Central's direct-backend `FragmentStats` snapshot includes `rx_req`, `rx_msg`,
invalid references/indices, wrong lengths, CRC errors and allocation failures.
`rx_msg` counts entry into the fragment-message handler, including subsequently
rejected fragments; it does not count unique stored fragments or completed
messages. Existing WARN/ERROR diagnostics retain rejection reasons. No retry,
timeout, queue capacity or receive-admission behavior is changed.

## SPI receive cookies

The first valid FRM2 data frame after transport initialization establishes the
module-to-host receive-cookie baseline; zero and nonzero starting values are
both valid. Subsequent cookies are compared modulo 65536, including 65535 to
zero wraparound. Malformed packets, commands, events, flow control and loopback
packets do not advance this baseline.

The shared parser extracts the already-validated FRM2 payload directly instead
of invoking the vendor raw receive formatter, whose zero-initialized counter
prints `cookie err: last:0, new:0` for a first frame with cookie zero. Vendor
sources remain unchanged. Genuine gaps, repeats and backwards cookie changes
produce a structured `RX cookie discontinuity` warning containing only the
previous, expected and received cookie values. Frames are still delivered,
matching vendor behavior; cookies alone do not establish radio packet loss.
Transport reinitialization clears the baseline; association events do not.

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
Credit tests verify identical-frame retries, unlocked waits, bounded permanent
starvation, disconnect during waiting and nonblocking receive-side writes.
Large-send reservations are checked through completion, including a worker
that frees its queue item before returning.
The adapter runs at minimum, default and maximum TX sizes, retaining full-size RX.
The SPI `Run-WireTests.ps1` runner additionally links the real wire parser,
HaLow adapter and shared reassembly implementation in `rx_integration_test.c`.
It injects 55002-byte transfers with 214-, 982- and 1600-byte v2 chunks (including
full 1664-byte vendor frames), padded reads, duplicate requests/fragments,
missing-fragment recovery, CRC errors and incoming-handoff rejection. It verifies
byte-for-byte reconstructed content, protocol replies, allocation cleanup and
quiet INFO logging and the bounded DEBUG progress/completion logs. Only platform services, peer lookup,
the physical writer and final incoming consumer are simulated; this is not an
over-the-air or application/PubSub callback test.
Cooperative-service tests cover large-item bypass, one small item per service
point, recursion prevention, blocked/locked queues, reservation lifetime,
owned-buffer cleanup and RX not servicing queued outgoing work.

The presentation contract runs the production serializer/parser, message
admission/worker and queue-state helpers with HaLow enabled and disabled.
It verifies legacy output, the v1 high mask byte, round trips, malformed inputs,
name bounds and duplicate-peer mapping merges. It also checks blocked-queue
deduplication, independent replies/peers/reasons/media, failure cleanup,
dequeue/timeout/completion boundaries, three-byte result storage and exact-once
frees. The adapter contract exercises the production reservation under the
queue mutex, including retained reservations during QoS resets and driver work.
Admission tests also cover rejection while a large transfer is queued or
dequeued/running and reservation rollback after allocation or queue failure.
The real PubSub server is linked in every admission build. Tests verify that
busy/queue-full image publication performs no image allocation, failed payload
or queue-item allocation releases reservations, successful images select
PSRAM, and small detections remain admissible.

A second contract runs the production fragmentation implementation with the
HaLow medium value and chunk size, with a different local receive ceiling to
verify the advertised chunk-size contract. It verifies a complete 55002-byte transfer
and recovery after dropping one fragment, including the final result and
exactly one requested fragment retransmission, repeated credit starvation
without a resend cycle, and stopping immediately on a hard local send error.
Further cases cover 650 ms credit shortages with an unchanged fragment index
or unchanged transfer-init/status packet,
300 ms small-message servicing, 600 ms feedback delay, allocation rollback,
late feedback after cleanup, and permanent starvation stopping at 30000 ms.
Fault-injection cases duplicate each bitmap eight times, inject stale/future
rounds and invalid map values, lose the same fragment in two successive passes,
and try to roll back an acknowledged fragment. They also send identical bytes
twice without clearing the completion cache, inject the old ID's feedback,
lose the new completion reply, and replay completed requests/data/checks.
The RX callback asserts that it never executes a HaLow data retransmission.
Existing native-format and
signedness compiler warnings in that legacy source are reported separately;
the new adapter compiles with warnings treated as errors.

Cross-media regressions run the production shared fragmentation code for
BLE, ESP-NOW, LoRa, I2C, CAN bus and mock media, with HaLow enabled and disabled.
For each medium they exercise four representative chunk sizes (64, 240, 990
and 1460 bytes), CRC-checked reassembly, a lost fragment and one-shot local
`ROB_ERR_QUEUE_FULL`/`ROB_ERR_SEND_FAIL` errors: 192 completed transfers in total.
They verify that non-HaLow media retain their existing full-burst and
missing-fragment recovery behavior, rather than inheriting HaLow's pause/stop policy.

The admission runner also builds all supported direct-media selectors together,
with and without HaLow. It verifies that each non-HaLow medium still accepts
multiple large messages for one peer, accepts small messages, enforces its
normal queue capacity and completes/frees worker-owned messages. When HaLow
is enabled, these checks run with a large HaLow transfer marked pending and
assert that other media never acquire or release its reservation.
Queue bindings and physical drivers are mocked; these checks cover shared
code isolation, not on-device operation of the individual drivers.

The shared SPI [wire/lifecycle runner](../development/halow/test/spi/Run-WireTests.ps1)
uses the same native compiler and pinned vendor sources:

```powershell
$zig = python -c "import pathlib,ziglang; print(pathlib.Path(ziglang.__file__).parent / 'zig.exe')"
.\development\halow\test\spi\Run-WireTests.ps1 -Compiler $zig -VendorSource 'C:\path\to\firmware\build\_deps\robusto_halow_vendor\examples\halow_spi_single\components\huge-ic-nonos-driver-v2.0.0-31919'
```

Use `-OutputDirectory <unique-directory>` to isolate generated tests and compiler
caches when sharing a worktree; the default remains `.test-build` beside the runner.

It checks exact 64-byte command blocks (43/41/40/132), vendor callback ordering,
full-size raw TX, the production parser and trace compaction. Header-only
commands retain prior STA bytes outside their declared length; the tests
preserve this vendor behavior. The actual backend is also compiled against
host ESP-IDF/SDIO/Robusto mocks to test startup, reset after a command-43
timeout, invalid GPIOs, runtime faults, credit/lock/quiesce/cleanup failures
and completion-token preservation. Default operation is tested with an
unavailable diagnostic DMA descriptor and occupied UART. SPI credit tests also
assert one outstanding SOFTFC request, no data submission while busy,
replenishment and explicit query failure. The portable
contracts cover optional reset dispatch, unsupported reset and configured
SPI registration, as well as the existing media/fragment/presentation paths.

These are not on-device concurrency, GPIO/DMA or RF tests. Before this source
migration, Collector logs showed firmware `1.6.4.5`/SVN `38054`, successful
43/41/40 after power cycle, and recovery from a 43 timeout using explicit
132 plus reinitialization. The migrated shared build still needs on-device
validation. Association, raw bidirectional traffic, Central integration and
TWAI management remain separate work. Existing hardware profiles are not
silently switched or deleted by enabling source support.
