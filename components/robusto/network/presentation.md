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

`$env:CC='python -m ziglang cc'; python development\halow\test\run_presentation_contract.py` compiles the production `robusto_presentation.c` with HaLow enabled and disabled and checks v0 byte-for-byte output, v1 high mask byte, v0/v1 parsing, malformed rejection and HaLow mapping preservation.
