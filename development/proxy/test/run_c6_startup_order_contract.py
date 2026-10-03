#!/usr/bin/env python3

from pathlib import Path
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
C6_DIRECTORY = (
    REPOSITORY_ROOT
    / "components"
    / "robusto"
    / "proxy"
    / "transports"
    / "esp_sdio"
    / "src"
    / "c6"
)
assertions = 0


def require(condition: bool, message: str) -> None:
    global assertions
    assertions += 1
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    signature_offset = source.find(signature)
    require(signature_offset >= 0, f"missing function: {signature}")
    body_start = source.find("{", signature_offset)
    require(body_start >= 0, f"missing function body: {signature}")
    depth = 0
    for offset in range(body_start, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[body_start + 1 : offset]
    raise AssertionError(f"unterminated function body: {signature}")


def require_order(body: str, tokens: tuple[str, ...], context: str) -> None:
    offsets = tuple(body.find(token) for token in tokens)
    require(all(offset >= 0 for offset in offsets), f"{context}: missing ordered token")
    require(
        all(left < right for left, right in zip(offsets, offsets[1:])),
        f"{context}: startup order changed",
    )


def main() -> int:
    orchestration = (C6_DIRECTORY / "robusto_proxy_sdio_c6.c").read_text()
    device = (C6_DIRECTORY / "robusto_proxy_sdio_device.c").read_text()

    prepare = function_body(orchestration, "esp_err_t robusto_proxy_sdio_c6_prepare(void)")
    require_order(
        prepare,
        (
            "robusto_proxy_sdio_device_init()",
            "robusto_c6_control_frontend_init()",
            "robusto_c6_recovery_init()",
            "robusto_proxy_sdio_device_enable_transport()",
            "robusto_c6_proxy_service_register()",
            "init_robusto_checked()",
        ),
        "C6 preparation",
    )

    enable = function_body(
        device, "esp_err_t robusto_proxy_sdio_device_enable_transport(void)"
    )
    require("sdio_slave_start()" in enable, "transport enable must expose SDIO")
    require("xTaskCreate" not in enable, "transport enable must not dispatch requests")

    start = function_body(device, "esp_err_t robusto_proxy_sdio_device_start(void)")
    require("!transport_enabled" in start, "receive processing requires enabled transport")
    require("xTaskCreate" in start, "device start must create receive processing")
    require("sdio_slave_start()" not in start, "device start must not re-enable transport")

    print(f"C6 startup order contract: {assertions} assertions, 0 failures")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"C6 startup order contract failure: {error}", file=sys.stderr)
        raise SystemExit(1)
