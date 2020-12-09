# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
from typing import Tuple, Union
from cocotb._py_compat import cache
from cocotb.utils import _get_log_time_scale, get_sim_steps, get_time_from_sim_steps
from cocotb.triggers import GPITrigger, TriggerException
import cocotb.simulator


class SimTime(GPITrigger):

    _valid_time_units = {
        'steps', 'fs', 'ps', 'ns', 'us', 'ms', 'sec'
    }

    def __init__(self, value: Union[int, float], unit: str):
        super().__init__()
        self._value = value
        self._unit = unit
        if not isinstance(value, (int, float)):
            raise TypeError("'value' must be a Python int or float")
        if self._value <= 0:
            raise ValueError("Time values <= 0 are not permissible. Time only moves in one direction.")
        if not isinstance(unit, str):
            raise TypeError("'unit' must be a str")
        if self._unit not in self._valid_time_units:
            raise ValueError("Invalid unit {!r}".format(unit))

    @classmethod
    def from_steps(cls, steps: int, *, unit: str) -> 'SimTime':
        value = get_time_from_sim_steps(steps, unit)
        return cls(value=value, unit=unit)

    @property
    def value(self) -> Union[int, float]:
        return self._value

    @property
    def unit(self) -> str:
        return self._unit

    @property
    @cache
    def steps(self) -> int:
        return get_sim_steps(self.value, self.unit)

    @property
    @cache
    def fs(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'fs')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='fs')

    @property
    @cache
    def ps(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'ps')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='ps')

    @property
    @cache
    def ns(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'ns')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='ns')

    @property
    @cache
    def us(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'us')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='us')

    @property
    @cache
    def ms(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'ms')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='ms')

    @property
    @cache
    def sec(self) -> 'SimTime':
        num, denom, _ = self._normalize_unit(self.unit, 'sec')
        value = self.value * (num / denom)
        return type(self)(value=value, unit='sec')

    def __add__(self, other: 'SimTime') -> 'SimTime':
        if not isinstance(other, SimTime):
            return NotImplemented
        norm_self, norm_other, unit = self._normalize(other)
        value = norm_self + norm_other
        return type(self)(value=value, unit=unit)

    def __sub__(self, other: 'SimTime') -> 'SimTime':
        if not isinstance(other, SimTime):
            return NotImplemented
        norm_self, norm_other, unit = self._normalize(other)
        value = norm_self - norm_other
        return type(self)(value=value, unit=unit)

    def __mul__(self, other: Union[int, float]) -> 'SimTime':
        if not isinstance(other, (int, float)):
            return NotImplemented
        value = self.value * other
        return type(self)(value=value, unit=self.unit)

    def __rmul__(self, other: Union[int, float]) -> 'SimTime':
        return self * other

    def __truediv__(self, other: Union[int, float]) -> 'SimTime':
        if not isinstance(other, (int, float)):
            return NotImplemented
        value = self.value / other
        return type(self)(value=value, unit=self.unit)

    def __eq__(self, other: 'SimTime'):
        if not isinstance(other, SimTime):
            return NotImplemented
        norm_self, norm_other, unit = self._normalize(other)
        return norm_self == norm_other

    def _normalize(self, other: 'SimTime') -> Tuple[float, float, str]:
        self_unit_norm, other_unit_norm, unit = self._normalize_unit(self.unit, other.unit)
        self_value_norm = self.value * self_unit_norm
        other_value_norm = other.value * other_unit_norm
        return self_value_norm, other_value_norm, unit

    @classmethod
    @cache
    def _normalize_unit(cls, self_unit: str, other_unit: str) -> Tuple[float, float, str]:
        self_unit_log10 = _get_log_time_scale(self_unit)
        other_unit_log10 = _get_log_time_scale(other_unit)
        diff_log10 = self_unit_log10 - other_unit_log10
        if diff_log10 < 0:
            return 10**diff_log10, 1, other_unit
        else:
            return 1, 10**(-diff_log10), self_unit

    def __repr__(self):
        return "{}({!r}, {!r})".format(type(self).__qualname__, self.value, self.unit)

    __hash__ = object.__hash__

    def prime(self, callback):
        if self.cbhdl is None:
            self.cbhdl = cocotb.simulator.register_timed_callback(self.steps, callback, self)
            if self.cbhdl is None:
                raise TriggerException("Unable set up {!r} trigger".format(self))
        super().prime(self, callback)

    def __await__(self):
        self.steps()  # try to fail before the trigger goes to the scheduler
        return (yield self)
