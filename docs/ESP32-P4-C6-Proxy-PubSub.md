# ESP32-P4/C6 Proxy PubSub

This guide describes the production Robusto proxy for the Waveshare
ESP32-P4-WIFI6. The ESP32-P4 is the controller. The onboard ESP32-C6 runs a
complete Robusto instance and exposes its PubSub server through the raw SDIO
proxy.

The application chooses local or remote PubSub explicitly:

- `robusto_pubsub_server_*` operates on the local Robusto instance.
- `robusto_proxy_pubsub_*` operates on the C6 instance named by the supplied
  `robusto_proxy_client_t`.
- Proxy failure never falls back to local PubSub.

The portable protocol, client, service, and PubSub adapter are part of
Robusto. The qualified Waveshare SDIO host/slave bindings and firmware entry
points are currently project code in the sibling `Robusto-proxy-development`
repository.

## Qualified platform

The production pair was qualified with:

- Waveshare ESP32-P4-WIFI6
- ESP-IDF v6.0.2
- one-bit SDIO at 10 MHz
- P4 pins: CLK 18, CMD 19, DAT0 14, DAT1/interrupt 15, C6 reset 54
- C6 pins: CLK 19, CMD 18, DAT0 20, DAT1/interrupt 21

DAT1 is required even in one-bit mode because the P4 uses it as the C6
new-packet interrupt.

The accepted reference artifacts are:

| Artifact | Identity |
|---|---|
| C6 application | `network_adapter` / `2.9.7-proxy1` |
| C6 binary SHA-256 | `0A827DBABB99DD4DBFCC901DEBB2EB736615BA3EE6D73527C2A45BDDE8D55F3F` |
| C6 ELF SHA-256 | `ED7FAAA4C136A0FB45FF3F6B55AD0360E702E2D1E57DBE6F5A1C8ABDC977844E` |
| P4 application | `p4-proxy1` |
| P4 binary SHA-256 | `4385A6350FC2AD33C0631F860A358E6C9C190F53A3A763D221F4DFD42F2C7EF5` |
| P4 ELF SHA-256 | `13280DD7E9CF2C68EA9804F52549018CC4F8D49B2876D9A26DDD97C3D75AFFF1` |

A rebuild normally has a new application identity because ESP-IDF embeds
build metadata. Record and qualify the new binary and ELF hashes instead of
assuming that they still match this table.

## Repository layout

The examples below assume these repositories are siblings:

```text
Projects/
    Robusto/
    Robusto-proxy-development/
```

Important implementation locations are:

```text
Robusto/components/robusto/proxy/                  portable proxy code
Robusto-proxy-development/firmware/common/         raw packet contract
Robusto-proxy-development/firmware/
    esp_hosted_2_9_7_slave/                        production C6 project
    p4_proxy_controller/                           production P4 example
    p4_test_controller/main/                       P4 raw SDIO binding
```

## Build the C6 application

Open an ESP-IDF v6.0.2 PowerShell and build the raw-only production variant:

```powershell
$env:IDF_PATH = 'C:\esp\v6.0.2\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.0.2\venv'
. $env:IDF_PATH\export.ps1

Set-Location Robusto-proxy-development\firmware\esp_hosted_2_9_7_slave
idf.py -B build-proxy1 `
  -D ROBUSTO_A_GENERATION=6 `
  -D ROBUSTO_B1_RAW_SDIO=ON `
  -D ROBUSTO_B2_RAW_ONLY=ON `
  -D ROBUSTO_PROXY_PRODUCTION=ON `
  build
```

The application image is:

```text
build-proxy1/network_adapter.bin
```

These options are all required:

| Option | Purpose |
|---|---|
| `ROBUSTO_A_GENERATION=6` | Selects the replacement-updater generation. |
| `ROBUSTO_B1_RAW_SDIO=ON` | Selects the project raw SDIO frontend. |
| `ROBUSTO_B2_RAW_ONLY=ON` | Excludes the ESP-Hosted C6 runtime. |
| `ROBUSTO_PROXY_PRODUCTION=ON` | Sets the production `2.9.7-proxy1` identity. |

The project CMake explicitly includes the Robusto proxy server and adapter
sources. Its tracked defaults enable `CONFIG_ROBUSTO_PUBSUB_SERVER=y`, which
provides the local C6 PubSub backend.

For a new C6 project that consumes the Robusto component normally rather than
using this project's explicit source list, also select:

```text
CONFIG_ROBUSTO_PROXY=y
CONFIG_ROBUSTO_PROXY_SERVER=y
CONFIG_ROBUSTO_PROXY_SERVICE_PUBSUB=y
CONFIG_ROBUSTO_PROXY_PROFILE_LOW_MEMORY=y
CONFIG_ROBUSTO_PUBSUB_SERVER=y
```

The qualified C6 startup sequence is:

1. Initialize NVS and apply the project boot guard.
2. Initialize the raw SDIO frontend.
3. Register diagnostics and the proxy server checked service at runlevel 1.
4. Call `init_robusto_checked()`.
5. Initialize the updater frontend.
6. Start raw SDIO receive processing.

The server binding adapts the local `robusto_pubsub_server_*` API to
`robusto_proxy_pubsub_server_adapter_t`. Requests and DELIVERY events are
processed by tasks, not by the SDIO ISR.

## Provision the C6

Building an application and installing it are separate operations. Do not use
`erase-flash`, erase NVS, or replace a qualified partition table as part of a
normal update.

The accepted production installation path starts from the exact confirmed B3
C6 and P4 migration phase 13. It packages the production C6 application in the
P4 updater and transfers it through the onboard SDIO updater:

```powershell
Set-Location Robusto-proxy-development\firmware\p4_test_controller
idf.py -B build-b4-updater-v3 -p <P4_PORT> flash monitor
```

This operation is valid only when all of the following are true:

- C6 is confirmed B3 with ELF SHA-256
  `4704EE3359E041770247D5C5BAF936EEEED22DF556694B254C7E6B441FD0E76D`.
- P4 NVS contains accepted phase 13.
- The updater contains the exact C6 binary it was built to verify.
- The P4 flash manifest contains no NVS image.

Keep the monitor attached through the automatic restarts. Successful
provisioning ends with:

```text
[PASS] B4 production C6 and confirmed raw runtime survive restart
```

The current solution does not define a universal factory-C6-to-production
installer. A board in another starting state must follow a separately reviewed
migration or recovery procedure. Direct C6 UART flashing is a recovery path,
not the accepted normal production installation path.

After rebuilding the C6, the existing B4 updater will reject the new artifact
unless its pinned size and hashes are deliberately rotated and the new pair is
qualified. This rejection protects against silently installing an unreviewed
image.

## Add the P4 client to a project

Start from `firmware/p4_proxy_controller`; it is the production PubSub example.
For another ESP-IDF P4 application, include the Robusto component and copy or
vendor these board-owned files without changing their ownership boundary:

```text
firmware/common/robusto_b1_sdio_protocol.c
firmware/common/robusto_b1_sdio_protocol.h
firmware/common/robusto_a4_bridge_protocol.h
firmware/p4_test_controller/main/robusto_b1_sdio_host.c
firmware/p4_test_controller/main/robusto_b1_sdio_host.h
firmware/p4_test_controller/main/robusto_p4_proxy_binding.c
firmware/p4_test_controller/main/robusto_p4_proxy_binding.h
firmware/p4_proxy_controller/main/robusto_proxy_hosted_gate.c
```

The application component needs these private dependencies:

```cmake
PRIV_REQUIRES
    robusto
    esp_timer
    esp_driver_gpio
    esp_driver_sdmmc
    sdmmc
    espressif__esp_serial_slave_link
```

The raw SDIO binding must be the only owner of the P4 SDMMC controller. The
qualified project includes ESP-Hosted through the remote-Wi-Fi dependency, so
it wraps the component's automatic initializer:

```cmake
target_link_libraries(${COMPONENT_LIB}
    INTERFACE "-Wl,--wrap=esp_hosted_init")
```

`robusto_proxy_hosted_gate.c` makes that initializer return successfully
without claiming SDIO. Do not start ESP-Hosted and the raw proxy together.

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

Also retain the qualified one-bit, 10 MHz SDIO and GPIO54 reset settings from
`firmware/p4_proxy_controller/sdkconfig.defaults`.

## Initialize the P4 proxy

The binding, client service, and PubSub subscription storage are caller-owned
and must remain alive while the proxy is running:

```c
#include "esp_random.h"
#include "robusto_init.h"
#include "robusto_p4_proxy_binding.h"
#include "robusto_proxy_pubsub_client.h"

static robusto_p4_proxy_binding_t proxy_binding;
static robusto_proxy_pubsub_client_subscription_t subscriptions[4];

static uint32_t random_nonzero_u32(void)
{
    uint32_t value;
    do {
        value = esp_random();
    } while (value == 0U);
    return value;
}

static uint64_t random_nonzero_u64(void)
{
    uint64_t value;
    do {
        value = ((uint64_t)esp_random() << 32) | esp_random();
    } while (value == 0U);
    return value;
}

static rob_ret_val_t start_proxy(void)
{
    const robusto_p4_proxy_binding_config_t config = {
        .profile = ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        .controller_boot_id = random_nonzero_u64(),
        .correlation_seed = random_nonzero_u32(),
        .sequence_seed = random_nonzero_u32(),
        .operation_seed = random_nonzero_u64(),
        .request_timeout_ms = 2000U,
        .runlevel = 1U,
    };
    rob_ret_val_t result;

    result = robusto_p4_proxy_binding_configure(&proxy_binding, &config);
    if (result == ROB_OK) {
        result = robusto_proxy_client_service_register(
            &proxy_binding.service, &proxy_binding.service_config);
    }
    if (result == ROB_OK) {
        result = init_robusto_checked();
    }
    if (result == ROB_OK) {
        result = robusto_proxy_pubsub_configure(
            &proxy_binding.client, subscriptions,
            sizeof(subscriptions) / sizeof(subscriptions[0]));
    }
    return result;
}
```

Runlevel 1 starts the transport, receive and event workers, initializes the
portable client, performs HELLO, and negotiates PubSub before later Robusto
services start. Every seed and boot ID must be nonzero. A startup call made
before successful HELLO returns `ROB_ERR_NOT_READY`; it does not execute
locally.

`robusto_proxy_pubsub_configure()` does not allocate subscription storage. The
application chooses the fixed capacity and owns the array. The production
example uses four slots, matching its four-entry DELIVERY event queue.

## Subscribe and receive DELIVERY events

The callback runs on the P4 proxy event task, outside the SDIO ISR, transport
lock, and synchronous request lock. Keep it bounded and return its result:

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
    &proxy_binding.client,
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
    &proxy_binding.client,
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
    &proxy_binding.client, &status);
if (result == ROB_OK && status.state != 1U) {
    result = ROB_ERR_NOT_READY;
}

if (result == ROB_OK) {
    result = robusto_proxy_pubsub_unsubscribe(
        &proxy_binding.client, temperature_subscription);
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
- A publish payload is at most 3,824 bytes.
- The low-memory protocol profile negotiates at most 16 remote subscriptions,
  but the P4 application storage may intentionally allow fewer.
- DELIVERY ordering is FIFO per subscription.
- DELIVERY retention, QoS, retained messages, replay history, and
  exactly-once delivery are not provided.
- Delivery queue pressure drops the newest event and increments counters.
- A sequence gap is observable through
  `client.pubsub_delivery_sequence_gaps`.
- C6 reset changes its boot ID. Reconnection reconciles desired active
  subscriptions into the existing P4 handles.

The public calls return `rob_ret_val_t`. For diagnostics, the client also
retains the latest wire result in `client.last_status`,
`client.last_result_flags`, and `client.last_retry_after_ms`.

## Expected production boot

A healthy P4 boot should show:

1. the raw proxy taking exclusive SDIO ownership
2. successful proxy service initialization at runlevel 1
3. HELLO and PubSub capability negotiation
4. later Robusto runlevels completing
5. application PubSub operations succeeding

The production example additionally performs a bounded loopback test and
prints:

```text
[DELIVERY][PASS] exact callback, unsubscribe, no redelivery
```

The tested P4 identity uses `ESP_MAC_BASE`. An all-zero peer identity or an
`esp_read_mac()` error is a startup defect, not a condition to ignore.

## Troubleshooting

`no available sd host controller` means another component, usually
ESP-Hosted, claimed the P4 SDMMC controller before the raw binding. Verify the
linker wrap and ensure only one transport is initialized.

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

If C6 provisioning stops, preserve the complete log, P4 NVS phase, current C6
identity, and both build hashes. Do not erase state or retry with a different
artifact until the failed gate is understood.