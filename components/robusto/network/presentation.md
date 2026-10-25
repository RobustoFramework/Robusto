# Peer presentation media-mask compatibility

Robusto media masks are 16 bits. Existing media retain their assigned values;
HaLow is `0x0100`. The low byte remains at presentation offset 3.

Presentation protocol v0 is unchanged: the peer name immediately follows the
six-byte base MAC address. A sender continues to use this byte-for-byte layout
for peers that are not known to support HaLow.

For a known HaLow-capable peer, protocol v1 inserts one high-mask byte after
the base MAC address and before the peer name. The inserted byte contains bits
15 through 8 of the 16-bit mask. The v1 version byte distinguishes the shifted
name position. Receivers validate the version, required fields, and bounded,
NUL-terminated peer name before updating peer state.

The single-bit media iteration limit is exclusive: `ROBUSTO_MEDIA_TYPE_LIMIT`
is `0x0200`, so iteration includes HaLow (`0x0100`).

## Host contract

`$env:CC='python -m ziglang cc'; python development\halow\test\run_presentation_contract.py` compiles the production presentation, message admission/worker and queue-state implementations with HaLow enabled and disabled. It checks wire compatibility, malformed rejection, HaLow mapping preservation and the ownership cases below. The fixture simulates scheduling, local send completion, the envelope builder and HaLow reservations; the separate HaLow contract exercises the production reservation and completion adapter.

## Pending HaLow presentations and ownership

A blocked HaLow queue retains its original work after the presentation caller's
6000 ms wait expires. `QUEUE_STATE_DROPPED` relinquishes interest in the result;
it does **not** cancel transmission or permit the caller to free queued work.
The worker still owns the payload, queue item and deferred result-state free.
Running/trying transitions preserve this marker.

For outbound HI requests, reserve one pending request per peer, HaLow medium
and presentation reason using the existing HaLow queue mutex. Reserve before
constructing a message or updating relation information. A duplicate returns
`ROB_ERR_NOT_READY` with a diagnostic, without allocating or modifying the
original. Keep the reservation through dequeue and local send completion,
then release it; release immediately on construction/admission failure.
QoS statistics resets must not clear the reservation.

Different peers and reasons remain independent. HIR replies do not reserve or
clear HI requests and retain their existing normal/important admission policy.
Other media retain their existing policy. Each selected medium owns a separate
payload and result-state allocation, including when an earlier medium times out.

Publish QUEUED before admitting an item: a worker can finish and free the item
immediately after admission unlocks, before the sending call returns. Never
overwrite that completion afterward or access the admitted item again.
Result codes occupy bytes 1-2 of the three-byte state, for success and failure;
byte 0 is published last.

Regressions cover a blocked queue with twelve duplicate attempts, retained
ordinary/reply traffic, the important reserve, reconnect/drain, renewed requests,
separate peers/reasons/media, allocation and admission failures, timeout while
running, completion before admission returns, completion at the timeout boundary,
state-buffer canaries and exact-once frees. These are deterministic host
interleavings, not a hardware association or parallel-task stress test.
Queue capacities, presentation timeouts and radio/retry policies are unchanged.
