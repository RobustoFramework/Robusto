# ESP32-P4/C6 Proxy Troubleshooting

This guide is for the qualified `examples/proxy/esp32_p4_c6_sdio` flow. Use it
to separate provisioning-state mistakes from actual C6 recovery failures before
changing code.

## Fast triage

Capture both logs from the same run:

- P4 USB console from the `provisioning` application
- C6 UART console if provisioning does not reach `Factory OTA sent ...` or a raw
  SDIO identity log

Work from the first matching signature below.

### Signature: stale P4 migration phase

P4 starts in a confirmation path immediately, for example:

```text
c6_provisioning: Confirm stage: initializing raw SDIO host without reset
```

Interpretation:

- P4 resumed a stored migration phase from NVS.
- This is a provisioning-state problem, not proof that the C6 transport is up.

Expected current behavior:

- If the previous bootstrap confirmation produced no C6 identity, the provisioner
  must clear the stale phase and return to `discover` on the next boot.

### Signature: raw SDIO never comes up after reset

P4 shows:

```text
c6_provisioning: Provision stage: candidate image hash verified, initializing raw SDIO host
robusto_rsd1: Host init: resetting C6 before SDIO connect
robusto_rsd1: Host init: essl_init()
robusto_rsd1: Host init: SDIO function 1 never became ready within 1500 ms (IOR=0x00)
```

Interpretation:

- The P4 can reset the C6 and enumerate enough of the card path to begin SDIO
  initialization.
- The C6 never starts the raw SDIO delegate frontend.
- This is not a proof that the P4 GPIO map is wrong by itself.

Immediate next check:

- Read the matching C6 UART log for the same reset window.

### Signature: hosted fallback also fails

P4 shows:

```text
transport: ensure_slave_bus_ready failed
c6_factory_bootstrap: ESP-Hosted reported transport failure during bootstrap handoff
```

or, on older builds:

```text
transport: ensure_slave_bus_ready failed
c6_factory_bootstrap: ESP-Hosted transport never came up within 30000 ms after bootstrap handoff
```

Interpretation:

- The factory-compatible hosted fallback also failed to find a usable C6
  transport.
- The problem is now on the C6 starting state unless a board-specific factory
  transport contract says otherwise.

### Signature: stale `ota_1` with invalid `ota_0`

C6 shows:

```text
boot: Loaded app from partition at offset 0x190000
app_init: Project name:     robusto_c6_delegate
app_init: App version:      ...
app_init: ELF file SHA256:  ...
esp_image: image at 0x10000 has invalid magic byte (nothing flashed here?)
... robusto_proxy_sdio_c6_start failed: ESP_ERR_OTA_VALIDATE_FAILED
```

Interpretation:

- The C6 is booting `ota_1`.
- The opposite OTA slot at `0x10000` (`ota_0`) is invalid.
- The running image dies before it starts either raw SDIO or the factory
  hosted-compatible frontend.
- The normal P4-only provisioner has no working transport to talk to.

This is the fastest discriminator for an out-of-envelope recovery state.

## What the provisioner can recover

The qualified provisioner can recover these states:

- factory-compatible C6 frontend that still exposes the hosted migration path
- raw-SDIO delegate image that boots far enough to answer the recovery identity
  protocol
- interrupted activation where the C6 can still boot and identify itself

The qualified provisioner cannot recover this state by itself:

- C6 boots a stale image, the other OTA slot is invalid, and the running image
  aborts before starting any provisioner-visible transport

When that happens, restore one valid OTA application slot on the C6 through the
diagnostic UART and then return to the normal P4 USB flow.

## Emergency UART recovery

Use this only when the P4 log and the C6 log together prove the out-of-envelope
state above.

Preferred recovery artifact:

- a locally built bootstrap image produced from this example's `c6_delegate`
  project

Do not assume the example checkout already contains `build` or
`build-bootstrap` directories. Those are local build outputs, not checked-in
repository content. Use the bootstrap binary that actually exists in your active
workspace.

Reason:

- it is part of the qualified provisioning flow
- the provisioner already knows how to confirm that identity and then move to
  the final delegate image
- in this example, the bootstrap build changes the example identity string and
  ELF identity, but keeps the same delegate runtime entrypoints

Restore the bootstrap image into `ota_0` at `0x10000`. Do not erase the whole
flash unless you have separate evidence that the partition table or bootloader is
damaged.

Example PowerShell commands:

```powershell
$robustoRoot = (Resolve-Path "<path-to-Robusto>").Path
$example = Join-Path $robustoRoot "examples/proxy/esp32_p4_c6_sdio"
$bootstrapImage = Join-Path $example "c6_delegate/build-bootstrap/robusto_c6_delegate.bin"
python -m esptool --chip esp32c6 --port COM4 --baud 460800 write_flash 0x10000 $bootstrapImage
```

If the bootstrap image was built in another workspace that packages the same
qualified example flow, use that explicit file path instead.

After the write succeeds:

1. remove the forced C6 ROM-boot strap used for UART flashing
2. power-cycle or reset the board normally
3. rerun the P4 `provisioning` application through normal P4 USB

Expected recovery behavior after restoring `ota_0`:

- the stale `ota_1` image can hand control back to a valid opposite slot, or
- the restored slot boots directly if the board's OTA data selects it
- once a valid delegate runtime boots, the P4 provisioner can resume the normal
  bootstrap/final confirmation flow

## Deterministic post-recovery sequence

After an emergency UART write restores a valid C6 application slot, use this
exact decision process.

### Step 1: rerun the same P4 provisioner image

Do not change artifacts between the emergency UART write and the next P4
provisioning run:

- do not rebuild the P4 provisioner
- do not repackage different C6 images
- do not clear P4 NVS
- do not flash the P4 again unless you have separate evidence that the P4 image
  itself changed

Use the same provisioner build that was already packaged with the intended
bootstrap and final C6 images.

### Step 2: accept one transport-recovery pass after UART restore

One intermediate pass may still fail before the recovered C6 runtime is visible
to the raw identity request. A known-good log shape is:

```text
Provision stage: raw SDIO host initialized, requesting C6 identity
Identity request: send READ and wait up to 30000 ms
... hosted fallback starts ...
... host restarts itself ...
```

If that exact pattern happens once immediately after the emergency UART flash,
rerun the same provisioner unchanged.

### Step 3: require these completion markers in order

The successful recovery path from this example is:

1. `Provision stage: received C6 identity subtype=0x11 boot_state=1`
2. `Provision stage: updater ready, transferring final image`
3. `Transferred 1034752/1034752 bytes`
4. `Final raw C6 installed; restarting for confirmation`
5. `Confirm stage: received C6 identity subtype=0x10 boot_state=1`
6. `Confirm stage: C6 matches target ELF and needs confirmation`
7. `Final C6 confirmed after activation; restarting for durability check`
8. `Confirm stage: received C6 identity subtype=0x10 boot_state=2`
9. `Final C6 exact identity is confirmed`

Treat that ordering as the acceptance contract for this recovery flow.

### Step 4: stop if the sequence diverges

Do not improvise if the sequence changes. Stop and capture fresh P4 and C6 logs
if any of these happen:

- the first retry after UART restore still cannot reach any C6 identity on the
  second P4 boot
- `subtype=0x11` never changes to the final-slot `subtype=0x10`
- `boot_state=1` never advances to `boot_state=2`
- the provisioner re-enters hosted fallback after the final image transfer
- any step suggests new artifacts or a new SHA-256 unexpectedly

## Process guardrails

For this example, use these rules to prevent ad-hoc debugging:

1. Always identify the running C6 partition offset and ELF SHA-256 before
  changing provisioning code.
2. Treat the packaged bootstrap SHA-256 and final SHA-256 as the source of truth
  for what the P4 is allowed to install.
3. After any C6 activation restart, expect confirmation first; another transfer
  is valid only if confirmation proves the running ELF is not the expected one.
4. If raw SDIO and hosted fallback both fail, do not guess about P4 wiring until
  the matching C6 boot log is available.
5. If the C6 log shows stale `ota_1` plus invalid `ota_0`, classify it as an
  out-of-envelope recovery state immediately.

## What not to infer from the logs

- `ESP-Hosted` lines in the P4 provisioner log do not mean ESP-Hosted is the C6
  runtime. In this flow it is only the P4 factory fallback helper.
- `sdmmc_init_ocr ... 0x107` after a failed hosted retry does not by itself prove
  bad wiring.
- `SDIO function 1 never became ready` does not prove a P4-side bug when the C6
  log simultaneously shows the delegate aborting before transport start.

## Minimal artifact set for future debugging

To avoid repeating slow diff-driven diagnosis, collect these first:

1. P4 provisioner boot log through the first provisioning failure
2. matching C6 UART boot log for the same reset window
3. running C6 partition offset from the boot log
4. running C6 ELF SHA-256 from the boot log
5. packaged bootstrap and final ELF SHA-256 values from the provisioning build

If items 3 and 4 show a stale C6 image while the packaged hashes differ, verify
the C6 recovery state before changing more P4 code.