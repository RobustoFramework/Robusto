# ESP32-P4/C6 Proxy PubSub

This guide describes Robusto service delegation between ESP32-P4 and ESP32-C6
over SDIO. Both processors run complete Robusto instances. The ESP32-P4 is the
delegating controller; the ESP32-C6 is the delegated instance and exposes its
configured PubSub server through the raw SDIO proxy. The qualified hardware
profile is Waveshare ESP32-P4-WIFI6, but P4 routing is configurable.

The application chooses local or remote PubSub explicitly:

- `robusto_pubsub_server_*` operates on the local Robusto instance.
- `robusto_proxy_pubsub_*` operates on the C6 instance named by the supplied
  `robusto_proxy_client_t`.
- Proxy failure never falls back to local PubSub.

Robusto contains the portable proxy, ESP raw-SDIO transport, P4 and C6
applications, native contracts, and provisioning application. No sibling
repository is required to build or use the solution.

Qualified example project on GitHub:

https://github.com/RobustoFramework/Robusto/tree/main/examples/proxy/esp32_p4_c6_sdio

## Current project direction

The actively maintained production direction is the ESP32-P4 to ESP32-C6
one-bit SDIO binding documented in this guide, including Waveshare
ESP32-P4-WIFI6 variants that route to ESP32-C6-MINI over SDIO.

SPI transport is not part of the current qualified scope and is not required
for this implementation. A future SPI binding remains optional exploratory
work for other board families, not a dependency for this project.

## Qualified platform

The production pair was qualified with:

- Waveshare ESP32-P4-WIFI6
- ESP-IDF v6.0.2
- one-bit SDIO at 10 MHz
- P4 pins: CLK 18, CMD 19, DAT0 14, DAT1/interrupt 15, C6 reset 54
- C6 pins: CLK 19, CMD 18, DAT0 20, DAT1/interrupt 21

DAT1 is required even in one-bit mode because the P4 uses it as the C6
new-packet interrupt.

The P4 routing and link clock are normal project settings. Configure them with
`idf.py -C examples/proxy/esp32_p4_c6_sdio/p4_controller menuconfig` under
**Robusto Configuration > Robusto Proxy > ESP SDIO transport**:

| Setting | Qualified default |
|---|---:|
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_CLK_GPIO` | 18 |
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_CMD_GPIO` | 19 |
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT0_GPIO` | 14 |
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO` | 15 |
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_C6_RESET_GPIO` | 54 |
| `CONFIG_ROBUSTO_PROXY_SDIO_P4_CLOCK_KHZ` | 10000 |

The ESP32-C6 slave pins are fixed by its SDIO slave peripheral and cannot be
routed through Kconfig. The transport intentionally supports only the
qualified one-bit mode; DAT2 and DAT3 are neither required nor configured.

The hardware-qualified reference artifacts from the original production pair
are:

| Artifact | Identity |
|---|---|
| C6 application | `network_adapter` / `2.9.7-proxy1` |
| C6 binary SHA-256 | `0A827DBABB99DD4DBFCC901DEBB2EB736615BA3EE6D73527C2A45BDDE8D55F3F` |
| C6 ELF SHA-256 | `ED7FAAA4C136A0FB45FF3F6B55AD0360E702E2D1E57DBE6F5A1C8ABDC977844E` |
| P4 application | `p4-proxy1` |
| P4 binary SHA-256 | `4385A6350FC2AD33C0631F860A358E6C9C190F53A3A763D221F4DFD42F2C7EF5` |
| P4 ELF SHA-256 | `13280DD7E9CF2C68EA9804F52549018CC4F8D49B2876D9A26DDD97C3D75AFFF1` |

A rebuild normally has a new application identity because ESP-IDF embeds build
metadata. The self-contained projects below were hardware-qualified on the
Waveshare board on 2026-07-22:

| Artifact | Size | Binary SHA-256 | ELF SHA-256 |
|---|---:|---|---|
| C6 delegate | 341,904 | `EE92EE0A842AADE6926DDBEBC251191341A454A23AFB664FAB1FBB504975762C` | `755AB43AC7176EE368E388ED600E5023D5714A1A500F47A1A44FEBFD6AFCF769` |
| P4 controller | 315,248 | `FE74B07694C0C92BA4EC7CABF286065D7F5293E909D5BFAAF6B6667365A2017F` | `027571E35A619AC51D4CC64D9C555B153760007C15766E8BA78554D3F9EBBAF1` |
| P4 provisioner | 277,376 | `C03B599F9B2341CB2506CBB9C9401FE148EE1153AB0866EDAE78726F0B7A8B40` | `632CA88C8F5D6DBC1DBD4D013925CF91F99A38F72104C2941AFBBEAE3B409E83` |

The provisioner transferred all 341,904 bytes over SDIO, activated the C6
candidate, restarted, and confirmed its exact ELF identity. The controller
then completed HELLO, reached Robusto runlevel 5, and passed the remote PubSub
subscribe, publish, and DELIVERY loopback. Record and hardware-qualify new
hashes after rebuilding before treating them as production artifacts.

## Diagnostic C6 UART and emergency recovery

Normal installation, updates, and example operation use only P4 USB and onboard
SDIO. Direct C6 UART is not a second installation path and is not a prerequisite
for this example. Read-only UART logging may be used for diagnostics. Writing
C6 flash through UART is reserved strictly for an emergency in which the normal
P4-driven provisioning or recovery procedure has failed and can no longer
restore a bootable C6.

The Waveshare C6 programming pads are numbered as follows. Use the board
silkscreen and the programming-port image in the Waveshare FAQ to identify the
pad orientation rather than assuming a left-to-right order.

| Pad | Signal |
|---:|---|
| 1 | C6 GPIO9/BOOT |
| 2 | GND |
| 3 | C6 UART RX |
| 4 | C6 UART TX |

For passive logs, power the board normally through P4 USB and use a 3.3 V logic
USB-TTL adapter:

- adapter GND to pad 2 (GND)
- adapter RX to pad 4 (C6 UART TX)
- optionally adapter TX to pad 3 (C6 UART RX)
- leave pad 1 (GPIO9/BOOT) unconnected
- do not connect the adapter's VCC/power output

The normal ESP32-C6 console rate is 115200 baud. Connecting only GND and the
adapter RX line is sufficient for read-only boot logs.

For Waveshare's direct ROM-download recovery procedure, first disconnect board
power, short pad 1 (C6 GPIO9/BOOT) to pad 2 (GND), and connect the crossed UART
lines above. Hold the P4 BOOT button before applying board power so the P4 does
not control C6 reset. Remove the GPIO9 short after recovery.

Use this mutating procedure only to restore the board to a verified,
board-compatible recovery image, for example the qualified ESP-Hosted image,
when P4-driven recovery is impossible. Verify the recovery image source,
target, version, partition assumptions, and recovery instructions before
writing it. Do not use UART flashing to bypass a provisioning failure or for a
routine update. After restoring C6 communication, disconnect the UART adapter
and return to the normal P4 USB and onboard-SDIO provisioning workflow.

## Other P4 Wi-Fi coprocessor boards

Do not assume that another ESP32-P4 board uses the Waveshare wiring, an
ESP32-C6, SDIO, or a user-programmable coprocessor. Check its schematic and
vendor firmware contract first.

A board with an ESP32-P4 SDIO host and ESP32-C6 SDIO slave can reuse the current
ESP SDIO binding when all of the following are true:

- CLK, CMD, DAT0, DAT1/interrupt, reset, bus width, pull-ups, and voltage levels
    are known; configure the P4 GPIOs and clock through Kconfig.
- The C6 fixed SDIO peripheral pins are wired compatibly.
- The P4 has exclusive ownership of that SDIO controller.
- The board has a qualified P4-driven way to install and recover the delegated
    firmware; factory ESP-Hosted updater protocols are board/version-specific.
- Reset, interrupted update, boot confirmation, and rollback behavior are
    tested on that board.

If the companion is not an ESP32-C6 or the link is SPI, UART, or another bus,
the portable proxy client/server and RSD1 framing can still be reused, but the
target needs a new transport binding and delegated runtime. Direct UART access,
when exposed, must follow that board's pad order, I/O voltage, boot strap, and
reset topology; the Waveshare four-pad procedure does not carry over.

## Repository layout

All required implementation locations are in Robusto:

```text
components/robusto/proxy/                          portable proxy code
components/robusto/proxy/transports/rsd1/          wire contracts
components/robusto/proxy/transports/esp_sdio/      complete ESP-IDF runtime
examples/proxy/esp32_p4_c6_sdio/
    c6_delegate/                                      C6 startup application
    p4_controller/                                 P4 PubSub example
    provisioning/                                  P4 provisioning application
development/proxy/test/                            native contracts
```

## Build the C6 application

Activate an ESP-IDF v6.0.2 environment using Espressif's instructions for your
operating system. ESP Hosted and ESP Wi-Fi Remote use `ESP_IDF_VERSION` as a
Kconfig filename selector. Set it to the compatibility series `6.0`, not the
installed patch release `6.0.2`, before configuring the P4 projects. In
PowerShell:

```powershell
$env:ESP_IDF_VERSION = "6.0"
```

Define the absolute C6 project path before building. `idf.py -C` selects the
project, but a relative `-B` path is resolved from the shell's working
directory. Using project-local absolute build paths keeps both artifacts under
`c6_delegate` regardless of the caller's working directory:

```powershell
$robustoRoot = (Resolve-Path "<path-to-Robusto>").Path
$c6Delegate = Join-Path $robustoRoot "examples/proxy/esp32_p4_c6_sdio/c6_delegate"
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build") build
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build-bootstrap") -D "ROBUSTO_C6_BOOTSTRAP=ON" build
```

The application images are:

```text
examples/proxy/esp32_p4_c6_sdio/c6_delegate/build/robusto_c6_delegate.bin
examples/proxy/esp32_p4_c6_sdio/c6_delegate/build-bootstrap/robusto_c6_delegate.bin
```

The project consumes Robusto through normal component and Kconfig selection:

```text
CONFIG_ROBUSTO_PROXY=y
CONFIG_ROBUSTO_PROXY_SERVER=y
CONFIG_ROBUSTO_PROXY_SERVICE_PUBSUB=y
CONFIG_ROBUSTO_PROXY_PROFILE_LOW_MEMORY=y
CONFIG_ROBUSTO_PUBSUB_SERVER=y
```

The C6 startup sequence is:

1. Initialize NVS without erasing it and apply the hash-bound boot guard.
2. Initialize the raw SDIO frontend.
3. Register the revision-2 updater and recovery identity endpoints.
4. Register the proxy server checked service at runlevel 1.
5. Call `init_robusto_checked()`.
6. Start raw SDIO receive processing.

The server binding adapts the local `robusto_pubsub_server_*` API to
`robusto_proxy_pubsub_server_adapter_t`. Requests and DELIVERY events are
processed by tasks, not by the SDIO ISR.

## Install the C6 through P4 USB

The hardware workflow exposes only the P4 USB connection. The user does not
connect to C6 UART and does not need to identify the running C6 firmware. A P4
installer must identify the available C6 protocol, select a qualified migration
step, and transfer the corresponding C6 image over onboard SDIO.

The provisioning project integrates the factory ESP-Hosted bootstrap and the
project-owned generation-6 revision-2 updater. It first attempts to read the
raw recovery identity. If the factory C6 does not expose that protocol, the P4
transfers the bootstrap through the factory-compatible transport. Later phases
confirm the bootstrap, install and confirm the final delegate, and verify that
the final identity survives another P4 restart.

The P4 persists the migration phase in the `c6_migrate` NVS namespace. The
phases are discovery, bootstrap pending, bootstrap ready, final pending, final
verification, and complete. A restart resumes from the recorded phase rather
than assuming that the preceding transfer or confirmation succeeded.

The P4 provisioning project packages both exact C6 applications into the P4
`slave_fw` partition. Its build derives and embeds these identities for each
image:

- SHA-256 over the exact C6 application file, checked before and after transfer
- the C6 image's embedded ELF SHA-256, checked before confirmation

Build both C6 variants first, then build the provisioner. These commands use
absolute project-local build directories and are independent of the shell's
working directory:

```powershell
$robustoRoot = (Resolve-Path "<path-to-Robusto>").Path
$c6Delegate = Join-Path $robustoRoot "examples/proxy/esp32_p4_c6_sdio/c6_delegate"
$provisioning = Join-Path $robustoRoot "examples/proxy/esp32_p4_c6_sdio/provisioning"
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build") build
idf.py -C $c6Delegate -B (Join-Path $c6Delegate "build-bootstrap") -D "ROBUSTO_C6_BOOTSTRAP=ON" build
idf.py -C $provisioning -B (Join-Path $provisioning "build") build
```

To package other built C6 images, set both inputs during configure:

```console
idf.py -C examples/proxy/esp32_p4_c6_sdio/provisioning -D "C6_BOOTSTRAP_FIRMWARE=<path-to-bootstrap-image>" -D "C6_FINAL_FIRMWARE=<path-to-final-image>" build
```

The generated `robusto_c6_bundle.bin` contains a 64-byte-header bootstrap
container at offset `0` and a final container at `0x60000`. Their payloads
therefore begin at offsets `64` and `393280`. The generated P4 flash manifest
writes the P4 bootloader, partition table, provisioning application, and this
bundle in `slave_fw` at P4 flash address `0x110000`. It does not write P4 NVS.
Run the provisioner through the normal P4 USB interface:

```console
idf.py -C examples/proxy/esp32_p4_c6_sdio/provisioning -p <P4_PORT> flash monitor
```

The raw updater verifies the selected packaged file before transfer, sends
ordered 1,500-byte chunks, requires ESP image validation and exact-file SHA-256
finalization, and activates the inactive OTA slot. After both processors
restart, the provisioner reads the running C6 ELF identity and confirms only
an exact match.

The dual-image build was validated with ESP-IDF v6.0.2 on 2026-07-23. Both C6
images were 344,400 bytes, `robusto_c6_bundle.bin` was 737,680 bytes, and the P4
provisioning application was `0x94d90` bytes with 42% of its application
partition free. These sizes are evidence for that build, not fixed format
requirements. Hardware execution of the integrated factory migration remains
to be qualified.

If the target image is already confirmed, no OTA write occurs. If it was armed
but left pending, the next C6 reset can roll it back before the provisioner
connects; rerunning the same provisioner then reinstalls and confirms the exact
target.

## Recovery boundary

The C6 retains two 1,536 KiB OTA slots. A running candidate becomes armed when
the P4 reads its identity. Confirmation is bound to that candidate's embedded
ELF SHA-256. If the armed candidate resets before confirmation, its boot guard
selects the other OTA slot, clears only the arm record, and restarts. It does
not erase NVS.

This protects an update that reaches the recovery identity service but is not
confirmed. A candidate that cannot boot far enough to start raw SDIO cannot arm
itself and cannot be recovered by the current provisioner. Such a state needs a
P4-driven recovery design; it is not shifted to a user-operated C6 connection.
The factory transport only covers the qualified factory ESP-Hosted starting
state; other factory protocols require their own identified migration step.

## Add the delegating controller to a project

Start from the complete P4 PubSub example:

```text
examples/proxy/esp32_p4_c6_sdio/p4_controller
```

Build it with:

```console
idf.py -C examples/proxy/esp32_p4_c6_sdio/p4_controller build
```

The Robusto component declares the target-specific P4 dependencies. An
application component depends only on Robusto:

```cmake
PRIV_REQUIRES
    robusto
```

The raw SDIO binding is the only owner of the P4 SDMMC controller. The clean
project does not include ESP-Hosted, remote Wi-Fi, or a linker wrapper.

Use the production partition table if retained recovery data is required:

```text
nvs       0x009000  0x006000
phy_init  0x00f000  0x001000
factory   0x010000  0x100000
slave_fw  0x110000  0x150000
```

The normal production P4 flash manifest writes the bootloader, partition
table, and P4 application. It does not write NVS or `slave_fw`.

Select the P4 Robusto configuration:

```text
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ROBUSTO_PROXY=y
CONFIG_ROBUSTO_PROXY_CLIENT=y
CONFIG_ROBUSTO_PROXY_SERVICE_PUBSUB=y
CONFIG_ROBUSTO_PROXY_PROFILE_LOW_MEMORY=y
```

The Robusto `proxy/transports/esp_sdio` module owns the one-bit SDIO runtime.
Its P4 GPIO and clock defaults are listed under **Qualified platform** and may
be changed through Kconfig for another P4 board routing.

## Initialize the P4 proxy

The component owns the single physical P4-to-C6 proxy runtime. The application
only supplies fixed PubSub subscription storage, which must remain alive while
the proxy is running:

```c
#include "robusto_init.h"
#include "robusto_proxy_pubsub_client.h"
#include "robusto_proxy_sdio.h"

static robusto_proxy_pubsub_client_subscription_t subscriptions[4];

static rob_ret_val_t start_proxy(void)
{
    rob_ret_val_t result = robusto_proxy_sdio_register(
        subscriptions, sizeof(subscriptions) / sizeof(subscriptions[0]));
    if (result == ROB_OK) {
        result = init_robusto_checked();
    }
    return result;
}
```

Runlevel 1 starts the transport, receive and event workers, initializes the
portable client, performs HELLO, and negotiates PubSub before later Robusto
services start. The component generates the required nonzero session identity
and seeds. A call made before successful HELLO returns `ROB_ERR_NOT_READY`; it
does not execute locally.

`robusto_proxy_pubsub_configure()` does not allocate subscription storage. The
application chooses the fixed capacity and owns the array. The production
example uses four slots, matching its four-entry DELIVERY event queue.

## Migrate an existing S3 subscriber to P4 + C6

Use this sequence when moving an existing Central subscriber role from
ESP32-S3 to the ESP32-P4 controller with an ESP32-C6 delegated runtime.

### 1. Inventory the existing S3 subscriber behavior

Collect and record the current behavior before modifying code:

- peer identity and timing settings (name, heartbeat, receipt timings)
- media settings (for example ESP-NOW PMK, LMK, channel, mode)
- subscriber topic names and callback parse expectations
- startup assumptions for "ready" state

Create a migration table with setting name, old value, new owner (P4 or C6),
and migration action.

### 2. Define the runtime role split

Document ownership before implementation:

- P4 owns application logic, proxy client runtime, and remote PubSub calls
- C6 owns proxy server runtime, local PubSub backend, and migrated media role

Do not add a fallback path that reroutes proxy failures to local P4 PubSub.

### 3. Port Central code to proxy client APIs on P4

Keep the required lifecycle order:

1. allocate fixed proxy subscription storage
2. register proxy SDIO binding
3. initialize Robusto
4. wait for HELLO-negotiated readiness
5. subscribe through `robusto_proxy_pubsub_subscribe()`
6. process complete DELIVERY callbacks after any negotiated chunk reassembly

Treat the proxy as ready only when `robusto_proxy_pubsub_is_ready(...)`
returns true.

Handle all non-OK return values explicitly:

- `ROB_ERR_NOT_READY`: session state issue, not a local-fallback trigger
- `ROB_ERR_OUTCOME_UNKNOWN`: mutation result is ambiguous

### 4. Mirror the S3 media role on C6

Set equivalent C6 identity and media behavior for existing peers:

- peer identity and timing behavior required by the current ecosystem
- media role details used previously on S3 (for example ESP-NOW keys/channel)

Do not assume defaults match prior S3 behavior.

For the current ESP SDIO path on ESP32-P4/ESP32-C6, use
`CONFIG_ROBUSTO_PROXY_PROFILE_LOW_MEMORY=y`. The standard profile is not
implemented for this target pair.

### 5. Build and provision in strict order

Order is mandatory:

1. build the final C6 delegate
2. build the C6 factory-migration bootstrap
3. build the P4 provisioner with both exact C6 images packaged
4. flash+monitor provisioner on P4 (migrate/install/confirm C6 over SDIO)
5. flash+monitor P4 Central application

Command pattern:

```console
idf.py -C <c6_delegate_project> set-target esp32c6
idf.py -C <c6_delegate_project> build
idf.py -C <c6_delegate_project> -B <bootstrap_build_directory> -D "ROBUSTO_C6_BOOTSTRAP=ON" build
idf.py -C <provisioning_project> set-target esp32p4
idf.py -C <provisioning_project> -D "C6_BOOTSTRAP_FIRMWARE=<absolute_path_to_bootstrap_bin>" -D "C6_FINAL_FIRMWARE=<absolute_path_to_final_bin>" build
idf.py -C <provisioning_project> -p <P4_PORT> flash monitor
idf.py -C <p4_central_project> set-target esp32p4
idf.py -C <p4_central_project> -p <P4_PORT> flash monitor
```

### 6. Validate build-time configuration before runtime tests

P4 configuration must include:

- proxy client role enabled
- proxy PubSub service enabled
- low-memory proxy profile selected
- expected SDIO pin/clock values

C6 configuration must include:

- proxy server role enabled
- PubSub server enabled
- expected media settings for the migrated role

### 7. Run acceptance gates from logs

C6 gates:

- runlevel startup completes
- proxy server starts
- media initialization confirms active state
- proxy backend logs subscription activity when P4 subscribes

P4 gates:

- proxy client starts and exchanges succeed
- PubSub ready state is reached
- subscription succeeds
- Central receives expected Collector payloads

Data-path gates:

- effective payload behavior matches pre-migration baseline
- no hidden local fallback path exists in subscriber logic

### 8. Use staged failure isolation

If Central does not receive data, isolate in order:

1. C6 boot and media startup
2. C6 proxy server startup
3. P4 HELLO and readiness
4. subscription request success
5. delivery callback execution and parsing

Preserve full logs before changing configuration.

## Subscribe and receive DELIVERY events

The callback runs on the P4 proxy event task, outside the SDIO ISR, transport
lock, and synchronous request lock. Inline data arrives in one event. For a
negotiated chunked delivery, the P4 validates begin/chunk/commit ordering and
invokes the callback once with the complete reassembled payload. Keep the
callback bounded and return its result:

```c
static rob_ret_val_t temperature_callback(void *context,
                                          uint8_t *data,
                                          uint32_t data_length)
{
    application_state_t *state = context;

    if (data_length != sizeof(state->last_temperature)) {
        return ROB_ERR_PARSING_FAILED;
    }
    memcpy(&state->last_temperature, data, data_length);
    return ROB_OK;
}

robusto_proxy_pubsub_client_subscription_t *temperature_subscription = NULL;

rob_ret_val_t result = robusto_proxy_pubsub_subscribe(
    robusto_proxy_sdio(),
    "sensor.temperature",
    temperature_callback,
    &application_state,
    &temperature_subscription);
if (result != ROB_OK) {
    /* Handle the exact startup, capacity, transport, or remote result. */
}
```

The returned handle is opaque and belongs to this client instance. Do not
construct it, copy its private storage, or use it with another client.

## Publish through the C6

The topic is an exact, case-sensitive UTF-8 name. The payload is borrowed only
for the synchronous call:

```c
uint8_t payload[] = {0x19U, 0x2AU};

rob_ret_val_t result = robusto_proxy_pubsub_publish(
    robusto_proxy_sdio(),
    "sensor.temperature",
    payload,
    sizeof(payload));
if (result == ROB_ERR_OUTCOME_UNKNOWN) {
    /* The C6 may have published. The application decides whether a duplicate
       is acceptable before issuing a new publish. */
} else if (result != ROB_OK) {
    /* Report or handle the explicit failure. */
}
```

Do not automatically replay `ROB_ERR_OUTCOME_UNKNOWN`. It means the complete
mutation may have reached the C6 but no trustworthy terminal response was
received.

## Query status and unsubscribe

```c
robusto_proxy_pubsub_status_response_t status = {0};

rob_ret_val_t result = robusto_proxy_pubsub_query_status(
    robusto_proxy_sdio(), &status);
if (result == ROB_OK && status.delivery_drops != 0U) {
    /* Report C6 allocation or delivery queue pressure. */
}
if (result == ROB_OK && status.state != 1U) {
    result = ROB_ERR_NOT_READY;
}

if (result == ROB_OK) {
    result = robusto_proxy_pubsub_unsubscribe(
        robusto_proxy_sdio(), temperature_subscription);
}
if (result == ROB_OK) {
    temperature_subscription = NULL;
}
```

Successful unsubscribe invalidates the local handle. Reusing it returns
`ROB_ERR_INVALID_ID` without another wire operation. A DELIVERY event already
queued before unsubscribe may still reach the P4 transport, but the client
discards it because the subscription is inactive.

## Shutdown

Use checked shutdown and handle its return value:

```c
rob_ret_val_t result = stop_robusto_checked();
if (result != ROB_OK) {
    /* The binding reports a bounded worker or transport shutdown failure. */
}
```

Do not deinitialize SDIO independently while proxy workers are running.

## Limits and behavior

- Topic names are 1 to 255 UTF-8 bytes, with no control characters.
- Topic names are exact and have no wildcard syntax.
- Publish and delivery payloads up to 3,824 bytes use one proxy frame.
- Larger P4-to-C6 publishes use sequential 4,080-byte chunks and are
    reassembled in C6 internal SRAM before local PubSub dispatch. If allocation
    fails, C6 returns `OUT_OF_MEMORY` before accepting chunks.
- Larger C6-to-P4 deliveries use `DELIVERY_BEGIN`, ordered `DELIVERY_CHUNK`
    events, and `DELIVERY_COMMIT`. The generic codec allows up to 4,080 data
    bytes per chunk; onboard ESP-SDIO uses 4,028 so the complete RSD1 packet
    does not exceed the C6 driver's 4,092-byte limit. P4 prefers PSRAM for one
    contiguous reassembly buffer and invokes the application callback only
    after a complete commit.
- The public and wire length is `uint32_t`; there is no smaller directional
    payload cap. Practical size in either direction is determined by contiguous
    allocations that succeed under the current runtime load.
- Large-message support is negotiated independently in each direction. A peer
    without `PUBSUB_CHUNKED_PUBLISH` or `PUBSUB_CHUNKED_DELIVERY` continues to
    support inline data. Large publishes return `ROB_ERR_NOT_SUPPORTED`; a C6
    large delivery for a controller without delivery support is dropped and
    counted.
- The C6 has no PSRAM. Its practical limit is the largest available 8-bit heap
    block under load. A queued outbound delivery owns its complete payload until
    all chunks are emitted. When an inbound chunked publish immediately produces
    a delegated delivery of the same buffer, ownership is transferred to the
    outbound queue without a second full allocation.
- The low-memory protocol profile negotiates at most 16 remote subscriptions,
  but the P4 application storage may intentionally allow fewer.
- DELIVERY ordering is FIFO per subscription.
- DELIVERY retention, QoS, retained messages, replay history, and
  exactly-once delivery are not provided.
- Delivery allocation or queue pressure drops the complete newest delivery and
    increments `delivery_drops` in PubSub status.
- A sequence gap is observable through
  `client.pubsub_delivery_sequence_gaps`.
- C6 reset changes its boot ID. Reconnection reconciles desired active
  subscriptions into the existing P4 handles.

The public calls return `rob_ret_val_t`. For diagnostics, the client also
retains the latest wire result in `client.last_status`,
`client.last_result_flags`, and `client.last_retry_after_ms`.

## Large delegated PubSub data and delivery pressure

Inline publishes and deliveries up to 3,824 bytes continue to use one proxy
frame. Larger P4-to-C6 publishes require negotiated
`PUBSUB_CHUNKED_PUBLISH`; larger C6-to-P4 deliveries require negotiated
`PUBSUB_CHUNKED_DELIVERY`. These capabilities are independent. If one is not
negotiated, the supported inline and opposite-direction operations remain
available rather than forcing the complete PubSub service to behave the same
way in every session.

The C6 sender processes delivery descriptors in FIFO order and serializes a
large descriptor from `DELIVERY_BEGIN` through `DELIVERY_COMMIT` before taking
the next descriptor. It advances that descriptor only after the SDIO frontend
confirms the event was queued; a failed send retries the same stage and offset.
The P4 parser nevertheless permits a valid inline delivery for another
subscription during active chunk reassembly and preserves the partial large
delivery. Events for the same subscription must retain sequence order.

The C6 has no PSRAM. A large inbound publish therefore depends on a contiguous
C6 internal-heap allocation. An independent large outbound delivery also needs
queue-owned C6 storage until its chunks are sent; an immediate delegated
delivery of the same completed publish transfers that buffer instead of making
a second full copy. The P4 prefers PSRAM for large delivery reassembly. In all
cases, current allocation and queue availability determine whether a particular
large operation can proceed; there is no smaller configured directional cap.

When a large delegated topic is missing, query PubSub status before diagnosing
provisioning or subscription state. A rising C6 `delivery_drops` count is the
first signal of C6 allocation, event-descriptor, event-pool, or negotiated
delivery-capability pressure. A rising P4
`client.pubsub_delivery_sequence_gaps` count confirms that one or more assigned
delivery sequences did not arrive. Those counters moving means the subscription
can still be valid while complete deliveries are being dropped under current
conditions. If both remain unchanged, continue with the normal subscription,
session, and provisioning checks in Troubleshooting below.

## Expected production boot

A healthy P4 boot should show:

1. the raw proxy taking exclusive SDIO ownership
2. successful proxy service initialization at runlevel 1
3. HELLO and PubSub capability negotiation
4. later Robusto runlevels completing
5. application PubSub operations succeeding

The example performs a bounded inline loopback, a 200 KiB chunked publish from
P4 to C6, and a 200 KiB chunked delivery from C6 to P4. It then prints status
counters and the final readiness marker:

```text
[PASS] sent 204800-byte chunked publish
[PASS] verified 204800-byte chunked delivery
PubSub status: deliveries=... drops=0 errors=0 sequence_gaps=0
remote PubSub example ready
```

The tested P4 identity uses `ESP_MAC_BASE`. An all-zero peer identity or an
`esp_read_mac()` error is a startup defect, not a condition to ignore.

## Troubleshooting

`no available sd host controller` means another component claimed the P4
SDMMC controller before the raw binding. Ensure only one transport is linked
and initialized; the clean examples do not include ESP-Hosted.

Repeated idle `sdmmc_host_io_int_wait` errors indicate that an older host
binding is in use. The production binding waits on the DAT1 GPIO interrupt
semaphore; normal idle polling is quiet.

`ROB_ERR_NOT_READY` means HELLO has not completed, the session became
degraded, or the service is stopping. Do not redirect the operation to local
PubSub.

`ROB_ERR_QUEUE_FULL` can mean the caller-owned subscription slots, negotiated
subscription limit, or a bounded protocol queue is full. Increase capacity
only after measuring the corresponding static memory and event-queue needs.

`ROB_ERR_OUTCOME_UNKNOWN` applies to a mutation whose transport outcome is
ambiguous. Preserve it as a distinct application state.

If C6 provisioning stops, preserve the complete log, current C6 identity, and
both build hashes. Do not erase state or retry with a different artifact until
the failed gate is understood.

### Subscriber migration checklist and diagnostics

When validating an S3-to-P4+C6 subscriber migration:

- distinguish proxy-ready from media-ready in logs
- do not map `ROB_ERR_NOT_READY` to local fallback behavior
- verify C6 media settings match peer requirements (for example ESP-NOW keys/channel)
- verify subscription success before debugging callback parsing
- verify payload expectations against pre-migration baseline data

## Acceptance checklist for production sign-off

All gates should pass before production sign-off:

- C6 image is provisioned through the P4 workflow and identity-confirmed
- C6 media role is active with expected configuration
- P4 proxy client reaches ready state
- expected subscriptions are installed
- expected payload flow is observed at Central
- no proxy-to-local fallback path is used