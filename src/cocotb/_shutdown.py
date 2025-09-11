# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
import logging
from typing import Callable, List

log = logging.getLogger("cocotb")


_callbacks: List[Callable[[str], None]] = []
"""List of callbacks to be called when cocotb shuts down."""


def register(cb: Callable[[str], None]) -> None:
    """Register a callback to be called when cocotb shuts down."""
    _callbacks.append(cb)


def _shutdown(reason: str) -> None:
    """Call all registered shutdown callbacks."""
    while _callbacks:
        cb = _callbacks.pop()
        cb(reason)


def _init() -> None:
    from cocotb import simulator  # noqa: PLC0415

    simulator.set_sim_event_callback(_shutdown)
