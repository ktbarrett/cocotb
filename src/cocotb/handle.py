# Copyright (c) 2013 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Potential Ventures Ltd,
#       SolarFlare Communications Inc nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL POTENTIAL VENTURES LTD BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import enum
import logging
import re
from abc import ABC, abstractmethod
from functools import lru_cache
from logging import Logger
from typing import (
    Any,
    Callable,
    Dict,
    Generic,
    Iterable,
    Iterator,
    NoReturn,
    Optional,
    Sequence,
    Tuple,
    Type,
    TypeVar,
    Union,
    cast,
)

from cocotb import simulator
from cocotb._deprecation import deprecated
from cocotb._gpi_triggers import FallingEdge, RisingEdge, ValueChange
from cocotb._py_compat import cached_property
from cocotb._utils import cached_method
from cocotb.types import Array, Logic, LogicArray, Range


def _write_now(
    _: "ValueObjectBase[Any, Any]", f: Callable[..., None], args: Any
) -> None:
    f(*args)


class _Limits(enum.IntEnum):
    SIGNED_NBIT = 1
    UNSIGNED_NBIT = 2
    VECTOR_NBIT = 3


@lru_cache(maxsize=None)
def _value_limits(n_bits: int, limits: _Limits) -> Tuple[int, int]:
    """Calculate min/max for given number of bits and limits class"""
    if limits == _Limits.SIGNED_NBIT:
        min_val = -(2 ** (n_bits - 1))
        max_val = 2 ** (n_bits - 1) - 1
    elif limits == _Limits.UNSIGNED_NBIT:
        min_val = 0
        max_val = 2**n_bits - 1
    else:
        min_val = -(2 ** (n_bits - 1))
        max_val = 2**n_bits - 1

    return min_val, max_val


class SimHandleBase(ABC):
    """Base class for all simulation objects.

    All simulation objects are hashable and equatable by identity.

    .. code-block:: python

        a = dut.clk
        b = dut.clk
        assert a == b

    .. versionchanged:: 2.0
        ``get_definition_name()`` and ``get_definition_file()`` were removed in favor of :meth:`_def_name` and :meth:`_def_file`, respectively.
    """

    @abstractmethod
    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        self._handle = handle
        self._path: str = self._name if path is None else path
        """The path to this handle, or its name if this is the root handle.

        :meta public:
        """

    @cached_property
    def _name(self) -> str:
        """The name of an object.

        :meta public:
        """
        return self._handle.get_name_string()

    @cached_property
    def _type(self) -> str:
        """The type of an object as a string.

        :meta public:
        """
        return self._handle.get_type_string()

    @cached_property
    def _log(self) -> Logger:
        return logging.getLogger(f"cocotb.{self._name}")

    @cached_property
    def _def_name(self) -> str:
        """The name of a GPI object's definition.

        This is the value of ``vpiDefName`` for VPI, ``vhpiNameP`` for VHPI,
        and ``mti_GetPrimaryName`` for FLI.
        Support for this depends on the specific object type and simulator used.

        :meta public:
        """
        return self._handle.get_definition_name()

    @cached_property
    def _def_file(self) -> str:
        """The name of the file that sources the object's definition.

        This is the value of ``vpiDefFile`` for VPI, ``vhpiFileNameP`` for VHPI,
        and ``mti_GetRegionSourceName`` for FLI.
        Support for this depends on the specific object type and simulator used.

        :meta public:
        """
        return self._handle.get_definition_file()

    def __hash__(self) -> int:
        return hash(self._handle)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, SimHandleBase):
            return NotImplemented
        return self._handle == other._handle

    def __repr__(self) -> str:
        desc = self._path
        defname = self._def_name
        if defname:
            desc += " with definition " + defname
            deffile = self._def_file
            if deffile:
                desc += " (at " + deffile + ")"
        return type(self).__qualname__ + "(" + desc + ")"

    def __bool__(self) -> NoReturn:
        raise TypeError(
            "This object cannot be cast to bool or used in conditionals. Use `obj is not None` check in conditionals."
        )


class RangeableObjectMixin(SimHandleBase):
    """Base class for simulation objects that have a range."""

    @cached_property
    def range(self) -> Range:
        """Return a :class:`~cocotb.types.Range` over the indexes of the array/vector."""
        left, right, direction = self._handle.get_range()
        if direction == simulator.RANGE_NO_DIR:
            raise RuntimeError("Expected range to have a direction but got none!")
        return Range(left, "to" if direction == simulator.RANGE_UP else "downto", right)

    @property
    def left(self) -> int:
        """Return the leftmost index in the array/vector."""
        return self.range.left

    @property
    def direction(self) -> str:
        """Return the direction (``"to"``/``"downto"``) of indexes in the array/vector."""
        return self.range.direction

    @property
    def right(self) -> int:
        """Return the rightmost index in the array/vector."""
        return self.range.right

    def __len__(self) -> int:
        return len(self.range)


#: Type of keys (name or index) in HierarchyObjectBase.
KeyType = TypeVar("KeyType")


class HierarchyObjectBase(SimHandleBase, Generic[KeyType]):
    """Base class for hierarchical simulation objects.

    Hierarchical objects don't have values, they are just scopes/namespaces of other objects.
    This includes array-like hierarchical structures like "generate loops"
    and named hierarchical structures like "generate blocks" or "module"/"entity" instantiations.

    This base class defines logic to discover, cache, and inspect child objects.
    It provides a :class:`dict`-like interface for doing so.

    :meth:`_keys`, :meth:`_values`, and :meth:`_items` mimic their :class:`dict` counterparts.
    You can also iterate over an object, which returns child objects, not keys like in :class:`dict`;
    and can check the :func:`len`.

    See :class:`HierarchyObject` and :class:`HierarchyArrayObject` for examples.
    """

    @abstractmethod
    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)
        self._sub_handles: Dict[KeyType, SimHandleBase] = {}
        self._discovered = False

    def _keys(self) -> Iterable[KeyType]:
        """Iterate over the keys (name or index) of the child objects.

        :meta public:
        """
        self._discover_all()
        return self._sub_handles.keys()

    def _values(self) -> Iterable[SimHandleBase]:
        """Iterate over the child objects.

        :meta public:
        """
        self._discover_all()
        return self._sub_handles.values()

    def _items(self) -> Iterable[Tuple[KeyType, SimHandleBase]]:
        """Iterate over ``(key, object)`` tuples of child objects.

        :meta public:
        """
        self._discover_all()
        return self._sub_handles.items()

    def _discover_all(self) -> None:
        """When iterating or performing IPython tab completion, we run through ahead of
        time and discover all possible children, populating the :any:`_sub_handles`
        mapping. Hierarchy can't change after elaboration so we only have to
        do this once.
        """
        if self._discovered:
            return

        for thing in self._handle.iterate(simulator.OBJECTS):
            name = thing.get_name_string()

            # translate HDL name into a consistent key name
            try:
                key = self._sub_handle_key(name)
            except ValueError:
                self._log.exception(
                    "Unable to translate handle >%s< to a valid _sub_handle key",
                    name,
                )
                continue

            # compute a full path using the key name
            path = self._child_path(key)

            # attempt to create the child object
            try:
                hdl = SimHandle(thing, path)
            except NotImplementedError:
                self._log.exception(
                    "Unable to construct a SimHandle object for %s", path
                )
                continue

            # add to cache
            self._sub_handles[key] = hdl

        self._discovered = True

    def __getitem__(self, key: KeyType) -> SimHandleBase:
        # try to use cached value
        try:
            return self._sub_handles[key]
        except KeyError:
            pass

        # try to get value from GPI
        new_handle = self._get_handle_by_key(key)
        if not new_handle:
            raise KeyError(f"{self._path} contains no child object named {key}")

        # if successful, construct and cache
        sub_handle = SimHandle(new_handle, self._child_path(key))
        self._sub_handles[key] = sub_handle

        return sub_handle

    @abstractmethod
    def _get_handle_by_key(self, key: KeyType) -> Optional[simulator.gpi_sim_hdl]:
        """Get child object by key from the simulator.

        Args:
            key: The key of the child object.

        Returns:
            A raw simulator handle for the child object at the given key, or ``None``.
        """

    @abstractmethod
    def _child_path(self, key: KeyType) -> str:
        """Compute the path string of a child object at the given key.

        Args:
            key: The key of the child object.

        Returns:
            A path string of the child object at the a given key.
        """

    @abstractmethod
    def _sub_handle_key(self, name: str) -> KeyType:
        """Translate a discovered child object name into a key.

        Args:
            name: The GPI name of the child object.

        Returns:
            A unique key for the child object.

        Raises:
            ValueError: if unable to translate handle to a valid _sub_handle key.
        """

    def __iter__(self) -> Iterator[SimHandleBase]:
        return iter(self._values())

    def __len__(self) -> int:
        self._discover_all()
        return len(self._sub_handles)

    def __dir__(self) -> Iterable[str]:
        """Permits IPython tab completion and debuggers to work."""
        self._discover_all()
        return set(super().__dir__()) | {str(k) for k in self._keys()}


class HierarchyObject(HierarchyObjectBase[str]):
    r"""A simulation object that is a name-indexed collection of hierarchical simulation objects.

    This class is used for named hierarchical structures, such as "generate blocks" or "module"/"entity" instantiations.

    Children under this structure are found by using the name of the child with either the attribute syntax or index syntax.
    For example, if in your :envvar:`COCOTB_TOPLEVEL` entity/module you have a signal/net named ``count``, you could do either of the following.

    .. code-block:: python

        dut.count  # attribute syntax
        dut["count"]  # index syntax

    Attribute syntax is usually shorter and easier to read, and is more common.
    However, it has limitations:

    - the name cannot start with a number
    - the name cannot start with a ``_`` character
    - the name can only contain ASCII letters, numbers, and the ``_`` character

    Any possible name of an object is supported with the index syntax,
    but it can be more verbose.

    Accessing a non-existent child with attribute syntax results in an :class:`AttributeError`,
    and accessing a non-existent child with index syntax results in a :class:`KeyError`.

    .. note::
        If you need to access signals/nets that start with ``_``,
        or use escaped identifier (Verilog) or extended identifier (VHDL) characters,
        you have to use the index syntax.
        Accessing escaped/extended identifiers requires enclosing the name
        with leading and trailing double backslashes (``\\``).

        .. code-block:: python

            dut["_underscore_signal"]
            dut["\\%extended !ID\\"]

    Iteration yields all child objects in no particular order.
    The :func:`len` function can be used to find the number of children.

    .. code-block:: python

        # discover all children in 'some_module'
        total = 0
        for handle in dut.some_module:
            cocotb.log("Found %r", handle._path)
            total += 1

        # make sure we found them all
        assert len(dut.some_module) == total
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def __setattr__(self, name: str, value: Any) -> None:
        # private attributes pass through directly
        if name.startswith("_"):
            return object.__setattr__(self, name, value)

        raise AttributeError(f"{self._name} contains no object named {name}")

    def __getattr__(self, name: str) -> SimHandleBase:
        if name.startswith("_"):
            return object.__getattribute__(self, name)  # type: ignore[no-any-return]  # this will always AttributeError

        try:
            return self[name]
        except KeyError as e:
            raise AttributeError(str(e)) from None

    @deprecated(
        "Use `handle[child_name]` syntax instead. If extended identifiers are needed simply add a '\\' character before and after the name."
    )
    def _id(self, name: str, extended: bool = True) -> SimHandleBase:
        """Query the simulator for an object with the specified *name*.

        If *extended* is ``True``, run the query only for VHDL extended identifiers.
        For Verilog, only ``extended=False`` is supported.

        :meta public:

        Args:
            name: The child object by name.
            extended: If ``True``, treat the *name* as an extended identifier.

        Returns:
            The child object.

        Raises:
            AttributeError: If the child object is not found.

        .. deprecated:: 2.0
            Use ``handle[child_name]`` syntax instead.
            If extended identifiers are needed simply add a ``\\`` character before and after the name.

        """
        if extended:
            name = "\\" + name + "\\"

        try:
            return self[name]
        except KeyError as e:
            raise AttributeError(str(e)) from None

    def _child_path(self, key: str) -> str:
        delimiter = "::" if self._type == "GPI_PACKAGE" else "."
        return f"{self._path}{delimiter}{key}"

    def _sub_handle_key(self, name: str) -> str:
        return name

    def _get_handle_by_key(self, key: str) -> Optional[simulator.gpi_sim_hdl]:
        return self._handle.get_handle_by_name(key)


class HierarchyArrayObject(HierarchyObjectBase[int], RangeableObjectMixin):
    """A simulation object that is an array of hierarchical simulation objects.

    This class is used for array-like hierarchical structures like "generate loops".

    Children of this object are found by supplying a numerical index using index syntax.
    For example, if you have a design with a generate loop ``gen_pipe_stages`` from the range ``0`` to ``7``:

    .. code-block:: python

        block_0 = dut.gen_pipe_stages[0]
        block_7 = dut.gen_pipe_stages[7]

    Accessing a non-existent child results in an :class:`IndexError`.

    Iteration yields all child objects in order.

    .. code-block:: python

        # set all 'reg's in each pipe stage to 0
        for pipe_stage in dut.gen_pipe_stages:
            pipe_stage.reg.value = 0

    Use the :meth:`range` property if you want to iterate over the indexes.
    The :func:`len` function can be used to find the number of elements.

    .. code-block:: python

        # set all 'reg's in each pipe stage to 0
        for idx in dut.gen_pipe_stages.range:
            dut.gen_pipe_stages[idx].reg.value = 0

        # make sure we have all the pipe stages
        assert len(dut.gen_pipe_stage) == len(dut.gen_pipe_stages.range)
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _sub_handle_key(self, name: str) -> int:
        # This is slightly hacky, but we need to extract the index from the name
        # See also GEN_IDX_SEP_* in VhpiImpl.h for the VHPI separators.
        #
        # FLI and VHPI:       _name(X) where X is the index
        # VHPI(ALDEC):        _name__X where X is the index
        # VPI:                _name[X] where X is the index
        result = re.match(rf"{re.escape(self._name)}__(?P<index>\d+)$", name)
        if not result:
            result = re.match(
                rf"{re.escape(self._name)}\((?P<index>\d+)\)$", name, re.IGNORECASE
            )
        if not result:
            result = re.match(rf"{re.escape(self._name)}\[(?P<index>\d+)\]$", name)

        if result:
            return int(result.group("index"))
        else:
            raise ValueError(f"Unable to match an index pattern: {name}")

    def _child_path(self, key: int) -> str:
        return f"{self._path}[{key}]"

    def _get_handle_by_key(self, key: int) -> Optional[simulator.gpi_sim_hdl]:
        return self._handle.get_handle_by_index(key)

    def __getitem__(self, key: int) -> SimHandleBase:
        if isinstance(key, slice):
            raise TypeError("Slice indexing is not supported")
        try:
            return super().__getitem__(key)
        except KeyError as e:
            raise IndexError(str(e)) from None

    # ideally `__len__` could be implemented in terms of `range`, but `range` doesn't work universally.

    def __iter__(self) -> Iterator[SimHandleBase]:
        # must use `sorted(self._keys())` instead of the range because `range` doesn't work universally.
        for i in sorted(self._keys()):
            yield self[i]


class _GPISetAction(enum.IntEnum):
    DEPOSIT = 0
    FORCE = 1
    RELEASE = 2
    NO_DELAY = 3


#: The type of the value a :class:`Deposit` or :class:`Force` action contains.
ValueT = TypeVar("ValueT")


class Deposit(Generic[ValueT]):
    r""":term:`Inertially deposits <inertial deposit>` the given value on a simulator object.

    If another :term:`deposit` comes after this deposit, the newer deposit overwrites the old value.
    If an HDL process is :term:`driving` the signal/net/register where a deposit from cocotb is made,
    the deposited value will be overwritten at the end of the next delta cycle,
    essentially causing a single delta cycle "glitch" in the waveform.
    """

    def __init__(self, value: ValueT) -> None:
        self.value = value


class Force(Generic[ValueT]):
    r""":term:`Force <force>` the given value on a simulator object immediately.

    Further :term:`deposits <deposit>` from cocotb or :term:`drives <drive>` from HDL processes
    do not cause the value to change until the handle is :term:`released <release>` by cocotb or HDL code.
    Further :term:`forces <force>` will overwrite the value and leave the value forced.
    """

    def __init__(self, value: ValueT) -> None:
        self.value = value


class Freeze:
    r""":term:`Force <force>` the simulator object with its current value.

    Useful if you have done a :term:`deposit` and later decide to lock the value from changing.
    Does not change the current value of the simulation object.
    See :class:`Force` for information on behavior after this write completes.
    """


class Release:
    """:term:`Release <release>` a :term:`forced <force>` simulation object.

    Does not change the current value of the simulation object.
    See :class:`Deposit` for information on behavior after this write completes.
    """


class Immediate(Generic[ValueT]):
    """:term:`Deposit <no-delay deposit>` a value on a simulator object without delay.

    The value of the signal will be changed immediately
    and should be able to be read back immediately following the write.
    Otherwise, behaves like :class:`Deposit`.
    """

    def __init__(self, value: ValueT) -> None:
        self.value = value


def _map_action_obj_to_value_action_enum_pair(
    handle: "ValueObjectBase[Any, Any]",
    value: Union[
        ValueT, Deposit[ValueT], Force[ValueT], Freeze, Release, Immediate[ValueT]
    ],
) -> Tuple[ValueT, _GPISetAction]:
    if isinstance(value, Deposit):
        return value.value, _GPISetAction.DEPOSIT
    elif isinstance(value, Force):
        return value.value, _GPISetAction.FORCE
    elif isinstance(value, Freeze):
        return handle.value, _GPISetAction.FORCE
    elif isinstance(value, Release):
        return handle.value, _GPISetAction.RELEASE
    elif isinstance(value, Immediate):
        return value.value, _GPISetAction.NO_DELAY
    else:
        return value, _GPISetAction.DEPOSIT


#: Type returned by the :attr:`~ValueObjectBase.value` getter and returned by the :meth:`~ValueObjectBase.get` method.
ValueGetT = TypeVar("ValueGetT")


#: Type accepted by the :attr:`~ValueObjectBase.value` setter and the :meth:`~ValueObjectBase.set` and :meth:`~ValueObjectBase.setimmediatevalue` methods.
ValueSetT = TypeVar("ValueSetT")


class ValueObjectBase(SimHandleBase, Generic[ValueGetT, ValueSetT]):
    """Abstract base class for simulation objects that have a value."""

    @property
    def value(self) -> ValueGetT:
        """Get or set the value of the simulation object.

        :getter: Return the current value of the simulation object.

        :setter:
            Set the value of the simulation object.

            See :class:`Deposit`, :class:`Force`, :class:`Freeze`, :class:`Release`, and :class:`Immediate`
            for additional actions that can be taken when setting a value.
            These are used like so:

            .. code-block:: python
                dut.handle.value = 1  # default Deposit action
                dut.handle.value = Deposit(2)
                dut.handle.value = Force(3)
                dut.handle.value = Freeze()
                dut.handle.value = Release()
                dut.handle.value = Immediate(4)
        """
        return self.get()

    @value.setter
    def value(self, value: ValueSetT) -> None:
        self.set(value)

    @abstractmethod
    def get(self) -> ValueGetT:
        """Return the current value of the simulation object."""

    def set(
        self,
        value: Union[
            ValueSetT,
            Deposit[ValueSetT],
            Force[ValueSetT],
            Freeze,
            Release,
            Immediate[ValueSetT],
        ],
    ) -> None:
        """Set the value of the simulation object.

        See :class:`Deposit`, :class:`Force`, :class:`Freeze`, :class:`Release`, and :class:`Immediate`
        for additional actions that can be taken when setting a value.

        Args:
            value: The value to set the simulation object to. This may include type conversion.
            action: How to set the value. See :class:`Action` for more details.

        Raises:
            TypeError: If the *value* is of a type that cannot be converted to a simulation value,
                or if the simulation object is immutable.
            ValueError: If the *value* is of the correct type, but the value fails to convert.
        """
        if self.is_const:
            raise TypeError(f"{self._path} is constant")

        value_, action = _map_action_obj_to_value_action_enum_pair(self, value)

        import cocotb._write_scheduler

        self._set_value(value_, action, cocotb._write_scheduler.schedule_write)

    @deprecated(
        "Use `handle.set(Immediate(...))` or `handle.value = Immediate(...)` instead."
    )
    def setimmediatevalue(
        self,
        value: Union[
            ValueSetT,
            Deposit[ValueSetT],
            Force[ValueSetT],
            Freeze,
            Release,
            Immediate[ValueSetT],
        ],
    ) -> None:
        r"""Set the value of the simulation object immediately.

        See :class:`Deposit`, :class:`Force`, :class:`Freeze`, :class:`Release`, and :class:`Immediate`
        for additional actions that can be taken when setting a value.

        Passing :class:`Deposit`\ s and unwrapped values is equivalent to passing an :class:`Immediate` to :meth:`set`.

        .. deprecated:: 2.0
            "Use `handle.set(Immediate(...))` or `handle.value = Immediate(...)` instead.
        """
        if self.is_const:
            raise TypeError(f"{self._path} is constant")

        value_, action = _map_action_obj_to_value_action_enum_pair(self, value)
        if action == _GPISetAction.DEPOSIT:
            action = _GPISetAction.NO_DELAY

        self._set_value(value_, action, _write_now)

    @cached_property
    def is_const(self) -> bool:
        """``True`` if the simulator object is immutable, e.g. a Verilog parameter or VHDL constant or generic."""
        return self._handle.get_const()

    @abstractmethod
    def _set_value(
        self,
        value: ValueSetT,
        action: _GPISetAction,
        schedule_write: Callable[
            ["ValueObjectBase[Any, Any]", Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        """Schedule a write of the given value to a simulator object.

        Conversion from multiple Python types into a type understood by the simulator is expected.
        This is used to implement the :attr:`value` property setter, :meth:`setimmediatevalue`, and :meth:`set`.
        Implementations can assume that handle isn't :meth:`const <is_const>`
        and the Scheduler is not in the :data:`ReadOnly <cocotb.SimPhase.READ_ONLY>` phase.

        Args:
            value: A value used to set the handle.
            action: Whether to deposit, force, or release the value on the handle.
            schedule_write: A function which takes ``(handle, callback, args)`` to schedule the writes.
        """


#: Type of value of each element in an :class:`ArrayObject`.
ElemValueT = TypeVar("ElemValueT")

#: Subtype of :class:`ValueObjectBase` returned when iterating or indexing a :class:`ArrayObject`.
ChildObjectT = TypeVar("ChildObjectT", bound=ValueObjectBase[Any, Any])


class ArrayObject(
    ValueObjectBase[Array[ElemValueT], Union[Array[ElemValueT], Sequence[ElemValueT]]],
    RangeableObjectMixin,
    Generic[ElemValueT, ChildObjectT],
):
    """A simulation object that is an array of value-having simulation objects.

    With Verilog simulation objects, unpacked vectors are mapped to this type.
    Packed vectors are typically mapped to :class:`LogicArrayObject`.

    With VHDL simulation objects, all arrayed objects that aren't ``std_(u)logic``,
    ``sfixed``, ``ufixed``, ``unsigned``, ``signed``, and ``string`` are mapped to this type.

    These objects can be iterated over to yield child objects:

    .. code-block:: python

        for child in dut.array_object:
            print(child._path)

    A particular child can be retrieved using its index:

    .. code-block:: python

        child = dut.array_object[0]

        # reversed iteration over children
        for child_idx in reversed(dut.array_object.range):
            dut.array_object[child_idx]
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)
        self._sub_handles: Dict[int, ChildObjectT] = {}

    def get(self) -> Array[ElemValueT]:
        """Return the current value as an :class:`~cocotb.types.Array`.

        Given the HDL array ``arr``, getting the value is equivalent to:

        +--------------+---------------------+--------------------------------------------------------------------------------------------------+
        | Verilog      | VHDL                | ``arr.get()`` is equivalent to                                                                   |
        +==============+=====================+==================================================================================================+
        | ``arr[4:7]`` | ``arr(4 to 7)``     | ``Array([arr[4].value, arr[5].value, arr[6].value, arr[7].value], range=Range(4, 'to', 7))``     |
        +--------------+---------------------+--------------------------------------------------------------------------------------------------+
        | ``arr[7:4]`` | ``arr(7 downto 4)`` | ``Array([arr[7].value, arr[6].value, arr[5].value, arr[4].value], range=Range(7, 'downto', 4))`` |
        +--------------+---------------------+--------------------------------------------------------------------------------------------------+
        """
        return Array((self[i].value for i in self.range), range=self.range)

    def set(
        self,
        value: Union[
            Union[Array[ElemValueT], Sequence[ElemValueT]],
            Deposit[Union[Array[ElemValueT], Sequence[ElemValueT]]],
            Force[Union[Array[ElemValueT], Sequence[ElemValueT]]],
            Freeze,
            Release,
            Immediate[Union[Array[ElemValueT], Sequence[ElemValueT]]],
        ],
    ) -> None:
        """Set the value using an :class:`.Array`-like value.

        The simulation object is set, element-by-element, left-to-right, using the corresponding element of *value*.
        The indexes of *value* and the simulation object are not taken into account, only position.

        .. warning::
            Assigning a value to a sub-handle:

            - **Wrong**: ``dut.some_array.value[0] = 1`` (gets value as an Array, updates index 0, then throws it away)
            - **Correct**: ``dut.some_array[0].value = 1``

        Args:
            value: The value to set the signal to. This may include type conversion.

        Raises:
            TypeError: If *value* is of a type that can't be assigned to the simulation object.

            .. warning::
                Exceptions from array element :meth:`.ValueObjectBase.set` calls will be propagated up,
                so the actual set of exceptions possible is greater than this list.
        """
        super().set(value)

    def _set_value(
        self,
        value: Union[Array[ElemValueT], Sequence[ElemValueT]],
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        if len(value) != len(self):
            raise ValueError(
                f"Assigning list of length {len(value)} to object {self._name} of length {len(self)}"
            )
        for elem, self_idx in zip(value, self.range):
            self[self_idx]._set_value(elem, action, schedule_write)

    def __getitem__(self, index: int) -> ChildObjectT:
        if isinstance(index, slice):
            raise IndexError("Slicing is not supported")
        if index in self._sub_handles:
            return self._sub_handles[index]
        new_handle = self._handle.get_handle_by_index(index)
        if not new_handle:
            raise IndexError(f"{self._path} contains no object at index {index}")
        path = self._path + "[" + str(index) + "]"
        self._sub_handles[index] = cast(ChildObjectT, SimHandle(new_handle, path))
        return self._sub_handles[index]

    def __iter__(self) -> Iterable[ChildObjectT]:
        for i in self.range:
            yield self[i]


class NonIndexableValueObjectBase(ValueObjectBase[ValueGetT, ValueSetT]):
    """ValueObject that is treated as a single object in the GPI.

    NonArrayValueObjects support :meth:`value_change` triggers.
    """

    @cached_property
    def value_change(self) -> ValueChange:
        """A trigger which fires whenever the value changes."""
        return ValueChange._make(self)


class LogicObject(NonIndexableValueObjectBase[Logic, Union[Logic, int, str]]):
    """A scalar logic simulation object.

    Verilog data types that map to this object:

        * ``logic``
        * ``bit``

    VHDL types that map to this object:

        * ``std_logic``
        * ``std_ulogic``
        * ``bit``
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: Union[Logic, int, str],
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        value_: str
        if isinstance(value, (int, str)):
            value_ = str(Logic(value))

        elif isinstance(value, LogicArray):
            if len(value) != 1:
                raise ValueError(
                    f"Cannot assign value of length {len(value)} to handle of length 1"
                )
            value_ = str(value)

        elif isinstance(value, Logic):
            value_ = str(value)

        else:
            raise TypeError(
                f"Unsupported type for value assignment: {type(value)} ({value!r})"
            )

        schedule_write(self, self._handle.set_signal_val_binstr, (action, value_))

    def get(self) -> Logic:
        """Return the current value of the simulation object as a :class:.Logic`."""
        binstr = self._handle.get_signal_val_binstr()
        return Logic(binstr)

    def set(
        self,
        value: Union[
            Union[Logic, int, str],
            Deposit[Union[Logic, int, str]],
            Force[Union[Logic, int, str]],
            Freeze,
            Release,
            Immediate[Union[Logic, int, str]],
        ],
    ) -> None:
        """Set the value of the simulation object using a :class:`.Logic`-like value.

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is of a type that can't be assigned to the simulation object, or readily converted into a type that can.
            ValueError: If *value* would not fit in the bounds of the simulation object.
        """
        super().set(value)

    @cached_property
    def rising_edge(self) -> RisingEdge:
        """A trigger which fires whenever the value changes to a ``1``."""
        return RisingEdge._make(self)

    @cached_property
    def falling_edge(self) -> FallingEdge:
        """A trigger which fires whenever the value changes to a ``0``."""
        return FallingEdge._make(self)

    @deprecated(
        "`len(handle)` of scalar objects is redundant. This method will be removed."
    )
    def __len__(self) -> int:
        return 1

    @deprecated(
        "`int(handle)` casts have been deprecated. Use `int(handle.value)` instead."
    )
    def __int__(self) -> int:
        return int(self.value)

    @deprecated(
        "`str(handle)` casts have been deprecated. Use `str(handle.value)` instead."
    )
    def __str__(self) -> str:
        return str(self.value)


class LogicArrayObject(
    NonIndexableValueObjectBase[LogicArray, Union[LogicArray, Logic, int, str]],
    RangeableObjectMixin,
):
    """A logic array simulation object.

    Verilog types that map to this object:

        * packed any-dimensional vectors of ``logic`` or ``bit``
        * packed any-dimensional vectors of packed structures

    VHDL types that map to this object:

        * ``std_logic_vector`` and ``std_ulogic_vector``
        * ``unsigned``
        * ``signed``
        * ``ufixed``
        * ``sfixed``
        * ``float``
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: Union[LogicArray, Logic, int, str],
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        value_: str
        if isinstance(value, int):
            min_val, max_val = _value_limits(len(self), _Limits.VECTOR_NBIT)
            if min_val <= value <= max_val:
                if len(self) <= 32:
                    schedule_write(
                        self, self._handle.set_signal_val_int, (action, value)
                    )
                    return

                # LogicArray used for checking
                if value < 0:
                    value_ = str(
                        LogicArray.from_signed(
                            value,
                            Range(len(self) - 1, "downto", 0),
                        )
                    )
                else:
                    value_ = str(
                        LogicArray.from_unsigned(
                            value,
                            Range(len(self) - 1, "downto", 0),
                        )
                    )
            else:
                raise OverflowError(
                    f"Int value ({value!r}) out of range for assignment of {len(self)!r}-bit signal ({self._name!r})"
                )

        elif isinstance(value, str):
            # LogicArray used for checking
            value_ = str(LogicArray(value, self.range))

        elif isinstance(value, LogicArray):
            if len(self) != len(value):
                raise ValueError(
                    f"cannot assign value of length {len(value)} to handle of length {len(self)}"
                )
            value_ = str(value)

        elif isinstance(value, Logic):
            if len(self) != 1:
                raise ValueError(
                    f"cannot assign value of length 1 to handle of length {len(self)}"
                )
            value_ = str(value)

        else:
            raise TypeError(
                f"Unsupported type for value assignment: {type(value)} ({value!r})"
            )

        schedule_write(self, self._handle.set_signal_val_binstr, (action, value_))

    def get(self) -> LogicArray:
        """Return the current value of the simulation object as a :class:`.LogicArray`."""
        binstr = self._handle.get_signal_val_binstr()
        return LogicArray._from_handle(binstr)

    def set(
        self,
        value: Union[
            Union[LogicArray, Logic, int, str],
            Deposit[Union[LogicArray, Logic, int, str]],
            Force[Union[LogicArray, Logic, int, str]],
            Freeze,
            Release,
            Immediate[Union[LogicArray, Logic, int, str]],
        ],
    ) -> None:
        """Set the value of the simulation object using a :class:`.LogicArray`-like value.

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is of a type that can't be assigned to the simulation object, or readily converted into a type that can.
            ValueError: If *value* would not fit in the bounds of the simulation object.
            OverflowError:
                If :class:`int` *value* is out of the range that can be represented by the target:
                -2**(Nbits - 1) <= value <= 2**Nbits - 1

        .. versionchanged:: 2.0
            Using :class:`ctypes.Structure` objects to set values was removed.
            Convert the struct object to a :class:`~cocotb.types.LogicArray` before assignment using
            ``LogicArray("".join(format(int(byte), "08b") for byte in bytes(struct_obj)))`` instead.

        .. versionchanged:: 2.0
            Using :class:`dict` objects to set values was removed.
            Convert the dictionary to an integer before assignment using
            ``sum(v << (d['bits'] * i) for i, v in enumerate(d['values']))`` instead.
        """
        super().set(value)

    @deprecated(
        "`int(handle)` casts have been deprecated. Use `int(handle.value)` instead."
    )
    def __int__(self) -> int:
        return int(self.value)

    @deprecated(
        "`str(handle)` casts have been deprecated. Use `str(handle.value)` instead."
    )
    def __str__(self) -> str:
        return str(self.value)

    @cached_method
    def __len__(self) -> int:
        # can't use `range` to get length because `range` is for outer-most dimension only
        # and this object needs to support multi-dimensional packed arrays.
        return self._handle.get_num_elems()


class RealObject(NonIndexableValueObjectBase[float, float]):
    """A floating point simulation object.

    This type is used when a ``real`` object in VHDL or ``float`` object in Verilog is seen.
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: float,
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        if not isinstance(value, (float, int)):
            raise TypeError(
                f"Unsupported type for real value assignment: {type(value)} ({value!r})"
            )

        schedule_write(self, self._handle.set_signal_val_real, (action, value))

    def get(self) -> float:
        """Return the current value of the simulation object as a :class:`float`."""
        return self._handle.get_signal_val_real()

    def set(
        self,
        value: Union[
            float,
            Deposit[float],
            Force[float],
            Freeze,
            Release,
            Immediate[float],
        ],
    ) -> None:
        """Set the value of the simulation object using a :class:`float` value.

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is any type other than :class:`float`.
        """
        super().set(value)

    @deprecated(
        "`float(handle)` casts have been deprecated. Use `float(handle.value)` instead."
    )
    def __float__(self) -> float:
        return self.value


class EnumObject(NonIndexableValueObjectBase[int, int]):
    """An enumeration simulation object.

    This type is used when an enumerated-type simulation object is seen that aren't a "logic" or similar type.
    The value of this object is represented with an :class:`int`.

    For VHDL objects, the value being represented is the enumeration value at the integer index into the original ``type`` declaration,
    as if it were a 1-based array.

    For Verilog objects, enumerations are little more than named integer values.
    There may be many enumeration values that a given :class:`int` value represents.
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: int,
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        if not isinstance(value, int):
            raise TypeError(
                f"Unsupported type for enum value assignment: {type(value)} ({value!r})"
            )

        min_val, max_val = _value_limits(32, _Limits.UNSIGNED_NBIT)
        if min_val <= value <= max_val:
            schedule_write(self, self._handle.set_signal_val_int, (action, value))
        else:
            raise OverflowError(
                f"Int value ({value!r}) out of range for assignment of enum signal ({self._name!r})"
            )

    def get(self) -> int:
        """Return the current value of the simulation object as an :class:`int`.

        See :class:`EnumObject` for details on what :class:`int` values correspond to which enumeration values.
        """
        return self._handle.get_signal_val_long()

    def set(
        self,
        value: Union[
            int,
            Deposit[int],
            Force[int],
            Freeze,
            Release,
            Immediate[int],
        ],
    ) -> None:
        """Set the value of the simulation object using an :class:`int`.

        See :class:`EnumObject` for details on what :class:`int` values correspond to which enumeration values.

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is any type other than :class:`int`.
            OverflowError: If *value* would not fit in a 32-bit signed integer.
        """
        super().set(value)

    @deprecated(
        "`int(handle)` casts have been deprecated. Use `int(handle.value)` instead."
    )
    def __int__(self) -> int:
        return int(self.value)


class IntegerObject(NonIndexableValueObjectBase[int, int]):
    """An integer simulation object.

    Verilog types that map to this object:

        * ``byte``
        * ``shortint``
        * ``int``
        * ``longint``

    This type should not be used for the 4-state integer types ``integer`` and ``time``.

    VHDL types that map to this object:

        * ``integer``
        * ``natural``
        * ``positive``

    Objects that use this type are assumed to be two's complement 32-bit integers with 2-state (``0`` and ``1``) bits.
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: int,
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        if not isinstance(value, int):
            raise TypeError(
                f"Unsupported type for integer value assignment: {type(value)} ({value!r})"
            )

        min_val, max_val = _value_limits(32, _Limits.SIGNED_NBIT)
        if min_val <= value <= max_val:
            schedule_write(self, self._handle.set_signal_val_int, (action, value))
        else:
            raise OverflowError(
                f"Int value ({value!r}) out of range for assignment of integer signal ({self._name!r})"
            )

    def get(self) -> int:
        """Return the current value of the simulation object as an :class:`int`."""
        return self._handle.get_signal_val_long()

    def set(
        self,
        value: Union[
            int,
            Deposit[int],
            Force[int],
            Freeze,
            Release,
            Immediate[int],
        ],
    ) -> None:
        """Set the the value of the simulation object using an :class:`int` value.

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is any type other than :class:`int`.
            OverflowError: If *value* would not fit in a 32-bit signed integer.
        """
        super().set(value)

    @deprecated(
        "`int(handle)` casts have been deprecated. Use `int(handle.value)` instead."
    )
    def __int__(self) -> int:
        return self.value


class StringObject(
    NonIndexableValueObjectBase[bytes, bytes],
    RangeableObjectMixin,
):
    """A string simulation object.

    This type is used when a ``string`` (VHDL or Verilog) simulation object is seen.
    """

    def __init__(self, handle: simulator.gpi_sim_hdl, path: Optional[str]) -> None:
        super().__init__(handle, path)

    def _set_value(
        self,
        value: bytes,
        action: _GPISetAction,
        schedule_write: Callable[
            [ValueObjectBase[Any, Any], Callable[..., None], Sequence[Any]], None
        ],
    ) -> None:
        if not isinstance(value, (bytes, bytearray)):
            raise TypeError(
                f"Unsupported type for string value assignment: {type(value)} ({value!r})"
            )

        schedule_write(self, self._handle.set_signal_val_str, (action, value))

    def get(self) -> bytes:
        """Return the current value of the simulation object as a :class:`bytes`."""
        return self._handle.get_signal_val_str()

    def set(
        self,
        value: Union[
            bytes,
            Deposit[bytes],
            Force[bytes],
            Freeze,
            Release,
            Immediate[bytes],
        ],
    ) -> None:
        """Set the value of the simulation object with a :class:`bytes` or :class:`bytearray` value.

        When *value*'s length is less than the simulation object's,
        the value is padded with NUL (``'\0'``) characters up to the appropriate length.
        When the value's length is greater than the simulation object's,
        the value is truncated without a NUL terminator to the appropriate length,
        without warning.

        Strings in both Verilog and VHDL are byte arrays without any particular encoding.
        Encoding must be done to turn Python strings into byte arrays.
        Because `there are many encodings <https://docs.python.org/3/library/codecs.html#standard-encodings>`_,
        this step is left up to the user.

        An example of how encoding and decoding could be accomplished using an ASCII string.

        .. code-block:: python

            # lowercase a string
            value = dut.string_handle.value.decode("ascii")
            value = value.lower()
            dut.string_handle.value = value.encode("ascii")

        Args:
            value: The value to set the simulation object to.

        Raises:
            TypeError: If *value* is any type other than :class:`bytes`.

        .. versionchanged:: 1.4
            Takes :class:`bytes` instead of :class:`str`.
            Users are now expected to choose an encoding when using these objects.
        """
        super().set(value)

    @deprecated(
        '`str(handle)` casts have been deprecated. Use `handle.value.decode("ascii")` instead.'
    )
    def __str__(self) -> str:
        return self.value.decode("ascii")


_ConcreteHandleTypes = Union[
    HierarchyObject,
    HierarchyArrayObject,
    LogicObject,
    LogicArrayObject,
    ArrayObject[Any, ValueObjectBase[Any, Any]],
    RealObject,
    IntegerObject,
    EnumObject,
    StringObject,
]


_handle2obj: Dict[
    simulator.gpi_sim_hdl,
    _ConcreteHandleTypes,
] = {}

_type2cls: Dict[int, Type[_ConcreteHandleTypes]] = {
    simulator.MODULE: HierarchyObject,
    simulator.STRUCTURE: HierarchyObject,
    simulator.PACKED_STRUCTURE: LogicArrayObject,
    simulator.LOGIC: LogicObject,
    simulator.LOGIC_ARRAY: LogicArrayObject,
    simulator.NETARRAY: ArrayObject[Any, ValueObjectBase[Any, Any]],
    simulator.REAL: RealObject,
    simulator.INTEGER: IntegerObject,
    simulator.ENUM: EnumObject,
    simulator.STRING: StringObject,
    simulator.GENARRAY: HierarchyArrayObject,
    simulator.PACKAGE: HierarchyObject,
}


def SimHandle(
    handle: simulator.gpi_sim_hdl, path: Optional[str] = None
) -> SimHandleBase:
    """Factory function to create the correct type of `SimHandle` object.

    Args:
        handle: The GPI handle to the simulator object.
        path: Path to this handle.

    Returns:
        An appropriate :class:`SimHandleBase` object.

    Raises:
        NotImplementedError: If no matching object for GPI type could be found.
    """

    # Enforce singletons since it's possible to retrieve handles avoiding
    # the hierarchy by getting driver/load information
    try:
        return _handle2obj[handle]
    except KeyError:
        pass

    t = handle.get_type()
    if t not in _type2cls:
        raise NotImplementedError(
            f"Couldn't find a matching object for GPI type {handle.get_type_string()}({t}) (path={path})"
        )
    obj = _type2cls[t](handle, path)
    _handle2obj[handle] = obj
    return obj
