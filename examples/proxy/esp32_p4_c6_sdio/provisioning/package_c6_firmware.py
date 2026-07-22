import argparse
import hashlib
import struct
from pathlib import Path


MAGIC = b"RBSTC6FW"
FORMAT_VERSION = 1
HEADER_SIZE = 64


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--size", required=True, type=int)
    parser.add_argument("--sha256", required=True)
    arguments = parser.parse_args()

    payload = arguments.input.read_bytes()
    digest = hashlib.sha256(payload).digest()
    if len(payload) != arguments.size:
        raise ValueError(
            f"C6 firmware size {len(payload)} does not match {arguments.size}"
        )
    if digest != bytes.fromhex(arguments.sha256):
        raise ValueError("C6 firmware SHA-256 does not match the manifest")

    header = struct.pack(
        "<8sIII12x32s",
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        len(payload),
        digest,
    )
    if len(header) != HEADER_SIZE:
        raise RuntimeError(
            f"Container header size is {len(header)}, expected {HEADER_SIZE}"
        )
    arguments.output.write_bytes(header + payload)


if __name__ == "__main__":
    main()