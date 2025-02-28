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

"""A collection of triggers which a testbench can :keyword:`await`."""

import logging
import warnings
from abc import abstractmethod
from typing import (
    Any,
    AsyncContextManager,
    Awaitable,
    Callable,
    Generator,
    List,
    Optional,
    TypeVar,
)

from cocotb._deprecation import deprecated
from cocotb._py_compat import cached_property
from cocotb._utils import Deck, pointer_str


class CallbackHandle:
    """A cancellable handle to a callback registered with a Trigger."""

    def __init__(
        self, trigger: "Trigger", func: Callable[..., Any], *args: Any
    ) -> None:
        self._func = func
        self._args = args
        self._trigger = trigger

    def cancel(self) -> None:
        self._trigger.deregister(self)

    def _run(self) -> None:
        self._func(*self._args)


Self = TypeVar("Self", bound="Trigger")


class Trigger(Awaitable["Trigger"]):
    """An :term:`awaitable <await>` event."""

    def __init__(self) -> None:
        # OrderedDict gives us O(1) append, pop, and random removal
        self._callbacks = Deck[CallbackHandle]()

    @cached_property
    def _log(self) -> logging.Logger:
        return logging.getLogger(f"cocotb.{type(self).__qualname__}.0x{id(self):x}")

    @abstractmethod
    def _prime(self) -> None:
        """Setup the underlying trigger mechanism.

        This should set the underlying trigger mechanism to call :meth:`_react`.
        """

    @abstractmethod
    def _unprime(self) -> None:
        """Disable and clean up the underlying trigger mechanism before it fires."""

    @abstractmethod
    def _cleanup(self) -> None:
        """Cleanup any state related to the underlying trigger mechanism after it fires."""

    def register(self, cb: Callable[..., None], *args: Any) -> CallbackHandle:
        """Register the given callback to be called when the Trigger fires.

        Calls :meth:`_prime` to register the underlying Trigger mechanism if a callback is added.

        Returns:
            A cancellable handle to the given callback.

        .. warning::
            Only intended for internal use.
        """
        cb_handle = CallbackHandle(self, cb, *args)
        self._callbacks.append(cb_handle)
        # _prime must come after adding to _cb_handles in case _prime calls _react
        self._prime()
        return cb_handle

    def deregister(self, cb_handle: CallbackHandle) -> None:
        """Prevent the given callback from being called once the Trigger fires.

        Calls :meth:`_unprime` to deregister the underlying Trigger mechanism if all callbacks are removed.

        Args:
            cb_handle: The Handle to the callback previously registered.

        .. warning::
            Only intended for internal use.
        """
        self._callbacks.remove(cb_handle)
        if not self._callbacks:
            self._unprime()

    def _react(self) -> None:
        """Call all registered callbacks when the Trigger fires."""
        while self._callbacks:
            handle = self._callbacks.pop()
            handle._run()
        self._cleanup()

    def __await__(self: Self) -> Generator[Self, None, Self]:
        yield self
        return self


class _Event(Trigger):
    """Trigger friend object used by an Event object."""

    def __init__(self, parent: "Event") -> None:
        super().__init__()
        self._parent = parent
        self._primed = False

    def _prime(self) -> None:
        if self._primed:
            # TODO Is this true? Can we remove this?
            raise RuntimeError(
                "Event.wait() result can only be used by one task at a time"
            )
        self._primed = True

        if self._parent.is_set():
            self._react()

    def _unprime(self) -> None:
        self._primed = False

    def _cleanup(self) -> None:
        self._primed = False

    def __repr__(self) -> str:
        return f"<{self._parent!r}.wait() at {pointer_str(self)}>"


class Event:
    r"""A way to signal an event across :class:`~cocotb.task.Task`\ s.

    :keyword:`await`\ ing the result of :meth:`wait()` will block the :keyword:`await`\ ing :class:`~cocotb.task.Task`
    until :meth:`set` is called.

    Args:
        name: Name for the Event.

    Usage:
        .. code-block:: python

            e = Event()


            async def task1():
                await e.wait()
                print("resuming!")


            cocotb.start_soon(task1())
            # do stuff
            e.set()
            await NullTrigger()  # allows task1 to execute
            # resuming!

    .. versionremoved:: 2.0

        Removed the undocumented *data* attribute and argument to :meth:`set`.
    """

    def __init__(self, name: Optional[str] = None) -> None:
        self.name: Optional[str] = name
        self._fired: bool = False
        self._data: Any = None
        self._event = _Event(self)

    @property
    @deprecated("The data field will be removed in a future release.")
    def data(self) -> Any:
        """The data associated with the Event.

        .. deprecated:: 2.0
            The data field will be removed in a future release.
            Use a separate variable to store the data instead.
        """
        return self._data

    @data.setter
    @deprecated("The data field will be removed in a future release.")
    def data(self, new_data: Any) -> None:
        self._data = new_data

    def set(self, data: Optional[Any] = None) -> None:
        """Set the Event and unblock all Tasks blocked on this Event."""
        self._fired = True
        if data is not None:
            warnings.warn(
                "The data field will be removed in a future release.",
                DeprecationWarning,
            )
        self._data = data
        self._event._react()

    def wait(self) -> Trigger:
        """Block the current Task until the Event is set.

        If the event has already been set, the trigger will fire immediately.

        To set the Event call :meth:`set`.
        To reset the Event (and enable the use of :meth:`wait` again),
        call :meth:`clear`.
        """
        return self._event

    def clear(self) -> None:
        """Clear this event that has been set.

        Subsequent calls to :meth:`~cocotb.triggers.Event.wait` will block until
        :meth:`~cocotb.triggers.Event.set` is called again.
        """
        self._fired = False

    def is_set(self) -> bool:
        """Return ``True`` if event has been set."""
        return self._fired

    def __repr__(self) -> str:
        if self.name is None:
            fmt = "<{0} at {2}>"
        else:
            fmt = "<{0} for {1} at {2}>"
        return fmt.format(type(self).__qualname__, self.name, pointer_str(self))


class _InternalEvent(Trigger):
    """Event used internally for triggers that need cross-:class:`~cocotb.task.Task` synchronization.

    This Event can only be waited on once, by a single :class:`~cocotb.task.Task`.

    Provides transparent :func`repr` pass-through to the :class:`Trigger` using this event,
    providing a better debugging experience.
    """

    def __init__(self, parent: object) -> None:
        super().__init__()
        self._parent = parent
        self.fired: bool = False
        self._primed: bool = False

    def _prime(self) -> None:
        if self._primed:
            raise RuntimeError("This Trigger may only be awaited once")
        self._primed = True
        if self.fired:
            self._react()

    def _unprime(self) -> None:
        pass

    def _cleanup(self) -> None:
        pass

    def set(self) -> None:
        """Wake up coroutine blocked on this event."""
        self.fired = True
        self._react()

    def is_set(self) -> bool:
        """Return true if event has been set."""
        return self.fired

    def __await__(
        self: Self,
    ) -> Generator[Any, Any, Self]:
        if self._callbacks:
            raise RuntimeError("Only one Task may await this Trigger")
        yield self
        return self

    def __repr__(self) -> str:
        return repr(self._parent)


class _Lock(Trigger):
    """Unique instance used by the Lock object.

    One created for each attempt to acquire the Lock so that the scheduler
    can maintain a unique mapping of triggers to tasks.
    """

    def __init__(self, parent: "Lock") -> None:
        super().__init__()
        self._parent = parent
        self._primed: bool = False

    def _prime(self) -> None:
        if self._primed:
            raise RuntimeError("This Trigger may only be awaited once")
        self._primed = True
        self._parent._prime_lock(self)

    def _unprime(self) -> None:
        self._parent._unprime_lock(self)
        self._primed = False

    def _cleanup(self) -> None:
        self._primed = False

    def __repr__(self) -> str:
        return f"<{self._parent!r}.acquire() at {pointer_str(self)}>"


class Lock(AsyncContextManager[None]):
    """A mutual exclusion lock.

    Guarantees fair scheduling.
    Lock acquisition is given in order of attempted lock acquisition.

    Usage:
        By directly calling :meth:`acquire` and :meth:`release`.

        .. code-block:: python

            await lock.acquire()
            try:
                # do some stuff
                ...
            finally:
                lock.release()

        Or...

        .. code-block:: python

            async with lock:
                # do some stuff
                ...

    .. versionchanged:: 1.4

        The lock can be used as an asynchronous context manager in an
        :keyword:`async with` statement
    """

    def __init__(self, name: Optional[str] = None) -> None:
        self._pending_primed: List[_Lock] = []
        self.name: Optional[str] = name
        self._locked: bool = False

    def locked(self) -> bool:
        """Return ``True`` if the lock has been acquired.

        .. versionchanged:: 2.0
            This is now a method to match :meth:`asyncio.Lock.locked`, rather than an attribute.
        """
        return self._locked

    def _acquire_and_fire(self, lock: _Lock) -> None:
        self._locked = True
        lock._react()

    def _prime_lock(self, lock: _Lock) -> None:
        if not self._locked:
            self._acquire_and_fire(lock)
        else:
            self._pending_primed.append(lock)

    def _unprime_lock(self, lock: _Lock) -> None:
        if lock in self._pending_primed:
            self._pending_primed.remove(lock)

    def acquire(self) -> Trigger:
        """Produce a trigger which fires when the lock is acquired."""
        trig = _Lock(self)
        return trig

    def release(self) -> None:
        """Release the lock."""
        if not self._locked:
            raise RuntimeError(f"Attempt to release an unacquired Lock {str(self)}")

        self._locked = False

        # nobody waiting for this lock
        if not self._pending_primed:
            return

        lock = self._pending_primed.pop(0)
        self._acquire_and_fire(lock)

    def __repr__(self) -> str:
        if self.name is None:
            fmt = "<{0} [{2} waiting] at {3}>"
        else:
            fmt = "<{0} for {1} [{2} waiting] at {3}>"
        return fmt.format(
            type(self).__qualname__,
            self.name,
            len(self._pending_primed),
            pointer_str(self),
        )

    async def __aenter__(self) -> None:
        await self.acquire()

    async def __aexit__(self, *args: Any) -> None:
        self.release()


class NullTrigger(Trigger):
    """Fires immediately.

    This is primarily for forcing the current Task to be rescheduled after all currently pending Tasks.

    .. versionremoved:: 2.0
        The *outcome* parameter was removed. There is no alternative.
    """

    def __init__(self, name: Optional[str] = None) -> None:
        super().__init__()
        self.name = name

    def _prime(self) -> None:
        self._react()

    def _unprime(self) -> None:
        pass

    def _cleanup(self) -> None:
        pass

    def __repr__(self) -> str:
        if self.name is None:
            fmt = "<{0} at {2}>"
        else:
            fmt = "<{0} for {1} at {2}>"
        return fmt.format(type(self).__qualname__, self.name, pointer_str(self))
