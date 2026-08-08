#!/usr/bin/env python3
"""
Send text to the LilyGo e-paper display over HTTP.

Usage:
    ./display.py "Hello World"
    echo "some text" | ./display.py
    ./display.py < somefile.txt
    cat <<EOF | ./display.py
    Line 1
    Line 2
    EOF

Environment:
    LILYGO_IP  IP address of the display (default: ask mDNS or prompt)
"""

import sys
import json
import os
import urllib.request
import urllib.error

DEFAULT_IP = os.environ.get("LILYGO_IP", "192.168.1.200")
URL = f"http://{DEFAULT_IP}/display"


def send_text(text: str) -> bool:
    payload = json.dumps({"text": text}).encode()
    req = urllib.request.Request(
        URL,
        data=payload,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status == 200:
                print(f"✓ Displayed to http://{DEFAULT_IP}", file=sys.stderr)
                return True
            else:
                print(f"✗ Server returned {resp.status}", file=sys.stderr)
                return False
    except urllib.error.URLError as e:
        print(f"✗ Could not reach {DEFAULT_IP}: {e.reason}", file=sys.stderr)
        print(f"  Is LILYGO_IP correct? Try: LILYGO_IP=<ip> {sys.argv[0]} ...", file=sys.stderr)
        return False


def main():
    # Read text from:
    #   1. Command-line args  →  " ".join(args)
    #   2. stdin  (if pipe / not a tty)
    #   3. Default message
    if len(sys.argv) > 1:
        text = " ".join(sys.argv[1:])
    elif not sys.stdin.isatty():
        text = sys.stdin.read()
    else:
        text = (
            "Display:\n"
            "-------\n"
            "No input provided.\n"
            "Usage: ./display.py \"Hello World\"\n"
            "       echo hi | ./display.py\n"
        )

    send_text(text)


if __name__ == "__main__":
    main()
