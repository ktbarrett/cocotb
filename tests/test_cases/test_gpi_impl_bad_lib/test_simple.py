from __future__ import annotations

import cocotb
from cocotb.triggers import Timer


@cocotb.test()
async def test_name_error(_):
    # GPI implementation loading will fail, so the file contents don't matter.
    await Timer(100, "ns")
