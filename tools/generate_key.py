#!/usr/bin/env python3
"""Generate a 256-bit ET-Core network key using the operating system CSPRNG."""
import argparse
import os
from pathlib import Path
import secrets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Write a private file instead of printing; never overwrite an existing file")
    args = parser.parse_args()
    key = secrets.token_hex(32) + "\n"
    if args.output:
        try:
            fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except OSError as error:
            parser.exit(1, f"Cannot create key file: {error}\n")
        with os.fdopen(fd, "w", encoding="ascii") as output:
            output.write(key)
    else:
        print(key, end="")


if __name__ == "__main__":
    main()
