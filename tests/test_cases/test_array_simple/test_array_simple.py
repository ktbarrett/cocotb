# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
"""Test getting and setting values of arrays"""
import contextlib
import logging

import cocotb
from cocotb._sim_versions import RivieraVersion
from cocotb.clock import Clock
from cocotb.triggers import Timer

tlog = logging.getLogger("cocotb.test")


# GHDL unable to put values on nested array types (gh-2588)
@cocotb.test(
    expect_error=Exception if cocotb.SIM_NAME.lower().startswith("ghdl") else ()
)
async def test_1dim_array_handles(dut):
    """Test getting and setting array values using the handle of the full array."""

    cocotb.start_soon(Clock(dut.clk, 1000, "ns").start())

    dut.array_7_downto_4.value = [0xF0, 0xE0, 0xD0, 0xC0]
    dut.array_4_to_7.value = [0xB0, 0xA0, 0x90, 0x80]
    dut.array_3_downto_0.value = [0x70, 0x60, 0x50, 0x40]
    dut.array_0_to_3.value = [0x30, 0x20, 0x10, 0x00]

    await Timer(1000, "ns")

    assert [e.integer for e in dut.array_7_downto_4.value] == [
        0xF0,
        0xE0,
        0xD0,
        0xC0,
    ]
    assert [e.integer for e in dut.array_4_to_7.value] == [0xB0, 0xA0, 0x90, 0x80]
    assert [e.integer for e in dut.array_3_downto_0.value] == [
        0x70,
        0x60,
        0x50,
        0x40,
    ]
    assert [e.integer for e in dut.array_0_to_3.value] == [0x30, 0x20, 0x10, 0x00]


# GHDL unable to put values on nested array types (gh-2588)
# iverilog flattens multi-dimensional unpacked arrays (gh-2595)
# Verilator doesn't support multi-dimensional unpacked arrays (gh-3611)
@cocotb.test(
    expect_error=Exception
    if cocotb.SIM_NAME.lower().startswith(("icarus", "ghdl"))
    else AttributeError
    if cocotb.SIM_NAME.lower().startswith("verilator")
    else ()
)
async def test_ndim_array_handles(dut):
    """Test getting and setting multi-dimensional array values using the handle of the full array."""

    cocotb.start_soon(Clock(dut.clk, 1000, "ns").start())

    dut.array_2d.value = [[0xF0, 0xE0, 0xD0, 0xC0], [0xB0, 0xA0, 0x90, 0x80]]

    await Timer(1000, "ns")

    assert [[e.integer for e in a] for a in dut.array_2d.value] == [
        [0xF0, 0xE0, 0xD0, 0xC0],
        [0xB0, 0xA0, 0x90, 0x80],
    ]


# GHDL unable to put values on nested array types (gh-2588)
@cocotb.test(
    expect_error=Exception if cocotb.SIM_NAME.lower().startswith("ghdl") else ()
)
async def test_1dim_array_indexes(dut):
    """Test getting and setting values of array indexes."""

    cocotb.start_soon(Clock(dut.clk, 1000, "ns").start())

    dut.array_7_downto_4.value = [0xF0, 0xE0, 0xD0, 0xC0]
    dut.array_4_to_7.value = [0xB0, 0xA0, 0x90, 0x80]
    dut.array_3_downto_0.value = [0x70, 0x60, 0x50, 0x40]
    dut.array_0_to_3.value = [0x30, 0x20, 0x10, 0x00]

    await Timer(1000, "ns")

    # Check indices
    assert dut.array_7_downto_4[7].value.integer == 0xF0
    assert dut.array_7_downto_4[4].value.integer == 0xC0
    assert dut.array_4_to_7[4].value.integer == 0xB0
    assert dut.array_4_to_7[7].value.integer == 0x80
    assert dut.array_3_downto_0[3].value.integer == 0x70
    assert dut.array_3_downto_0[0].value.integer == 0x40
    assert dut.array_0_to_3[0].value.integer == 0x30
    assert dut.array_0_to_3[3].value.integer == 0x00
    assert dut.array_0_to_3[1].value.integer == 0x20

    # Get sub-handles through NonHierarchyIndexableObject.__getitem__
    dut.array_7_downto_4[7].value = 0xDE
    dut.array_4_to_7[4].value = 0xFC
    dut.array_3_downto_0[0].value = 0xAB
    dut.array_0_to_3[1].value = 0x7A
    dut.array_0_to_3[3].value = 0x42

    await Timer(1000, "ns")

    assert dut.array_7_downto_4[7].value.integer == 0xDE
    assert dut.array_4_to_7[4].value.integer == 0xFC
    assert dut.array_3_downto_0[0].value.integer == 0xAB
    assert dut.array_0_to_3[1].value.integer == 0x7A
    assert dut.array_0_to_3[3].value.integer == 0x42


# GHDL unable to put values on nested array types (gh-2588)
# iverilog flattens multi-dimensional unpacked arrays (gh-2595)
# Verilator doesn't support multi-dimensional unpacked arrays (gh-3611)
@cocotb.test(
    expect_error=Exception
    if cocotb.SIM_NAME.lower().startswith(("icarus", "ghdl"))
    else AttributeError
    if cocotb.SIM_NAME.lower().startswith("verilator")
    else ()
)
async def test_ndim_array_indexes(dut):
    """Test getting and setting values of multi-dimensional array indexes."""

    cocotb.start_soon(Clock(dut.clk, 1000, "ns").start())

    dut.array_2d.value = [[0xF0, 0xE0, 0xD0, 0xC0], [0xB0, 0xA0, 0x90, 0x80]]

    await Timer(1000, "ns")

    # Check indices
    assert [e.integer for e in dut.array_2d[1].value] == [0xB0, 0xA0, 0x90, 0x80]
    assert dut.array_2d[0][31].value.integer == 0xF0
    assert dut.array_2d[1][29].value.integer == 0x90
    assert dut.array_2d[1][28].value.integer == 0x80

    # Get sub-handles through NonHierarchyIndexableObject.__getitem__
    dut.array_2d[1].value = [0xDE, 0xAD, 0xBE, 0xEF]
    dut.array_2d[0][31].value = 0x0F

    await Timer(1000, "ns")

    assert dut.array_2d[0][31].value.integer == 0x0F
    assert dut.array_2d[0][29].value.integer == 0xD0
    assert dut.array_2d[1][30].value.integer == 0xAD
    assert dut.array_2d[1][28].value.integer == 0xEF


# GHDL unable to access record signals (gh-2591)
# Icarus doesn't support structs (gh-2592)
# Verilator doesn't support structs (gh-1275)
# Riviera-PRO 2022.10 and newer does not discover inout_if correctly over VPI (gh-3587)
@cocotb.test(
    expect_error=AttributeError
    if cocotb.SIM_NAME.lower().startswith(("icarus", "ghdl", "verilator"))
    or (
        cocotb.SIM_NAME.lower().startswith("riviera")
        and RivieraVersion(cocotb.SIM_VERSION) >= RivieraVersion("2022.10")
        and cocotb.LANGUAGE == "verilog"
    )
    else ()
)
async def test_struct(dut):
    """Test setting and getting values of structs."""
    cocotb.start_soon(Clock(dut.clk, 1000, "ns").start())
    dut.inout_if.a_in.value = 1
    await Timer(1000, "ns")
    assert dut.inout_if.a_in.value.integer == 1
    dut.inout_if.a_in.value = 0
    await Timer(1000, "ns")
    assert dut.inout_if.a_in.value.integer == 0


@contextlib.contextmanager
def assert_raises(exc_type):
    try:
        yield
    except exc_type as exc:
        tlog.info(f"   {exc_type.__name__} raised as expected: {exc}")
    else:
        raise AssertionError(f"{exc_type.__name__} was not raised")


@cocotb.test()
async def test_exceptions(dut):
    """Test that correct Exceptions are raised."""
    with assert_raises(TypeError):
        dut.array_7_downto_4.value = (0xF0, 0xE0, 0xD0, 0xC0)
    with assert_raises(TypeError):
        dut.array_4_to_7.value = Exception("Exception Object")
    with assert_raises(ValueError):
        dut.array_3_downto_0.value = [0x70, 0x60, 0x50]
    with assert_raises(ValueError):
        dut.array_0_to_3.value = [0x40, 0x30, 0x20, 0x10, 0x00]
