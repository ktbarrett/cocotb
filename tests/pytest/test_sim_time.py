# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
from cocotb.sim_time import SimTime
import pytest


def test_construction():
    SimTime(10, 'ns')
    SimTime(100, 'sec')
    with pytest.raises(TypeError):
        SimTime(object(), 'ps')
    with pytest.raises(ValueError):
        SimTime(-67, 'ns')
    with pytest.raises(TypeError):
        SimTime(100, 10)
    with pytest.raises(ValueError):
        SimTime(12, 'ks')
    SimTime.from_steps(100, unit='ns')


def test_attributes():
    a = SimTime(value=456, unit='ps')
    assert a.value == 456
    assert a.unit == 'ps'

    b = SimTime(value=0.456, unit='ns')
    assert a == b
    assert b != SimTime(10, 'us')
    assert 9 != a

    assert eval(repr(a)) == a
    assert str(a) == "SimTime(456, 'ps')"


def test_conversions():
    assert SimTime(78, 'ns').steps == 78e6  # may change depending on what the default precision is
    a = SimTime(123, 'ps').fs
    assert pytest.approx(a.value, 123000)
    assert a.unit == 'fs'

    a = SimTime(0.25, 'ns').ps
    assert pytest.approx(a.value, 250)
    assert a.unit == 'ps'

    a = SimTime(1, 'sec').ns
    assert pytest.approx(a.value, 1e9)
    assert a.unit == 'ns'

    a = SimTime(67843, 'ns').us
    assert pytest.approx(a.value, 67.843)
    assert a.unit == 'us'

    a = SimTime(1, 'sec').ms
    assert pytest.approx(a.value, 1000)
    assert a.unit == 'ms'

    a = SimTime(1, 'fs').sec
    assert pytest.approx(a.value, 1e-15)
    assert a.unit == 'sec'


def test_combinations():

    assert (SimTime(1, 'ns') + SimTime(100, 'ps')) == SimTime(1.1, 'ns')
    with pytest.raises(TypeError):
        SimTime(1, 'ns') + 9

    assert (SimTime(10, 'ns') - SimTime(1, 'ns')) == SimTime(9, 'ns')
    with pytest.raises(TypeError):
        SimTime(1, 'ns') - 43

    assert (SimTime(10, 'ps') * 100) == SimTime(1, 'ns')
    assert (100 * SimTime(10, 'ps')) == SimTime(1, 'ns')
    with pytest.raises(TypeError):
        SimTime(67, 'sec') * object()

    assert (SimTime(10000, 'sec') / 100) == SimTime(100000, 'ms')
    with pytest.raises(TypeError):
        SimTime(1, 'ns') / {}
