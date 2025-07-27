# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
from typing import Callable, List

_shutdown_callbacks: List[Callable[[str], None]] = []
"""List of callbacks to be called when cocotb shuts down."""


def register_shutdown_callback(cb: Callable[[str], None]) -> None:
    """Register a callback to be called when cocotb shuts down."""
    _shutdown_callbacks.append(cb)


def remove_shutdown_callback(cb: Callable[[str], None]) -> None:
    """Remove a previously registered shutdown callback."""


def run_shutdown_callbacks(reason: str) -> None:
    """Call all registered shutdown callbacks in reverse registering order."""
    while _shutdown_callbacks:
        cb = _shutdown_callbacks.pop()
        cb(reason)
