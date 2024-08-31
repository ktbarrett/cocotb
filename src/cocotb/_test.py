# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
import functools
import inspect
import os
import time
from typing import (
    Any,
    Callable,
    Coroutine,
    List,
    Optional,
    Sequence,
    Type,
    TypeVar,
    Union,
)

import cocotb
from cocotb._exceptions import InternalError, SimFailure
from cocotb._outcomes import Error, Outcome
from cocotb.result import TestSuccess
from cocotb.task import Task
from cocotb.triggers import SimTimeoutError, with_timeout
from cocotb.utils import get_sim_time

PDB_ON_EXCEPTION = "COCOTB_PDB_ON_EXCEPTION" in os.environ

T = TypeVar("T")


class Test:
    """A cocotb test in a regression.

    Args:
        func:
            The test function object.

        name:
            The name of the test function.
            Defaults to ``func.__qualname__`` (the dotted path to the test function in the module).

        module:
            The name of the module containing the test function.
            Defaults to ``func.__module__`` (the name of the module containing the test function).

        doc:
            The docstring for the test.
            Defaults to ``func.__doc__`` (the docstring of the test function).

        timeout_time:
            Simulation time duration before the test is forced to fail with a :exc:`~cocotb.triggers.SimTimeoutError`.

        timeout_unit:
            Units of ``timeout_time``, accepts any units that :class:`~cocotb.triggers.Timer` does.

        expect_fail:
            If ``True`` and the test fails a functional check via an ``assert`` statement, :pytest:class:`pytest.raises`,
            :pytest:class:`pytest.warns`, or :pytest:class:`pytest.deprecated_call`, the test is considered to have passed.
            If ``True`` and the test passes successfully, the test is considered to have failed.

        expect_error:
            Mark the result as a pass only if one of the given exception types is raised in the test.

        skip:
            Don't execute this test as part of the regression.
            The test can still be run manually by setting :envvar:`COCOTB_TESTCASE`.

        stage:
            Order tests logically into stages.
            Tests from earlier stages are run before tests from later stages.
    """

    def __init__(
        self,
        *,
        func: Callable[..., Coroutine[Any, Any, None]],
        name: Optional[str] = None,
        module: Optional[str] = None,
        doc: Optional[str] = None,
        timeout_time: Optional[float] = None,
        timeout_unit: str = "step",
        expect_fail: bool = False,
        expect_error: Union[Type[Exception], Sequence[Type[Exception]]] = (),
        skip: bool = False,
        stage: int = 0,
        _expect_sim_failure: bool = False,
    ) -> None:
        if timeout_time is not None:
            co = func  # must save ref because we overwrite variable "func"

            @functools.wraps(func)
            async def func(*args, **kwargs):
                running_co = Task(co(*args, **kwargs))

                try:
                    res = await with_timeout(
                        running_co, self.timeout_time, self.timeout_unit
                    )
                except SimTimeoutError:
                    running_co.kill()
                    raise
                else:
                    return res

        self.func = func
        self.timeout_time = timeout_time
        self.timeout_unit = timeout_unit
        self.expect_fail = expect_fail
        if isinstance(expect_error, BaseException):
            expect_error = (expect_error,)
        if _expect_sim_failure:
            expect_error += (SimFailure,)
        self.expect_error = expect_error
        self._expect_sim_failure = _expect_sim_failure
        self.skip = skip
        self.stage = stage
        self.name = self.func.__qualname__ if name is None else name
        self.module = self.func.__module__ if module is None else module
        self.doc = self.func.__doc__ if doc is None else doc
        if self.doc is not None:
            # cleanup docstring using `trim` function from PEP257
            self.doc = inspect.cleandoc(self.doc)
        self.fullname = f"{self.module}.{self.name}"

        self.task: Task[None]
        self.start_time: float
        self.start_sim_time: float

        self.outcome: Union[Outcome[Any], None]
        self.tasks: List[Task] = []

    def init(self) -> None:
        self.task = TestTask(self.func(cocotb.top), self.name)

    def start(self) -> None:
        cocotb._scheduler_inst._queue(self._test_task)
        self.start_sim_time = get_sim_time("ns")
        self.start_time = time.time()
        cocotb._scheduler_inst._event_loop()

    def start_task(self, task: Task[T]) -> Task[T]:
        task._add_done_callback(self._task_done_callback)
        self.tasks.append(Task)
        cocotb._scheduler_inst._queue(task)
        return task

    def _task_done_callback(self) -> None:
        join = cocotb.triggers._Join(self)
        if join in cocotb._scheduler_inst._trigger2tasks:
            return
        if self.cancelled():
            return
        exc = self.exception()
        if exc is None:
            return
        elif isinstance(exc, (TestSuccess, AssertionError)):
            self.log.info("Test stopped by this task")
            self.abort(exc)
        else:
            self.log.error("Exception raised by this task")
            self.abort(exc)

    def abort(self, exc: Exception) -> None:
        """Force this test to end early, without executing any cleanup.

        This happens when a background task fails, and is consistent with
        how the behavior has always been. In future, we may want to behave
        more gracefully to allow the test body to clean up.

        `exc` is the exception that the test should report as its reason for
        aborting.
        """
        if self.outcome is not None:  # pragma: no cover
            raise InternalError("Outcome already has a value, but is being set again.")
        outcome = Error(exc)
        self.outcome = outcome
        # handle test failure
        # cleanup scheduler


class TestTask(Task[None]):
    """Specialized version of Task that includes Test specific overloads.

    * Changes ``__name__`` to show "Test" instead of "Task".
    """

    def __init__(self, inst: Coroutine[Any, Any, None], name: str) -> None:
        super().__init__(inst)
        self.__name__ = f"Test {name}"
        self.__qualname__ = self.__name__

    def _shutdown(self) -> None:
        cocotb._scheduler_inst.shutdown_soon()
        return super()._shutdown()
