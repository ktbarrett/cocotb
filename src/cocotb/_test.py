# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
import functools
import inspect
import os
from typing import Any, Callable, Coroutine, Optional, Sequence, Type, Union

from cocotb._outcomes import Outcome
from cocotb.regression import current_test
from cocotb.result import SimFailure, TestSuccess
from cocotb.task import ResultType, Task
from cocotb.triggers import NullTrigger, SimTimeoutError, with_timeout

_Failed: Type[BaseException]
try:
    import pytest
except ModuleNotFoundError:
    _Failed = AssertionError
else:
    try:
        with pytest.raises(Exception):
            pass
    except BaseException as _raises_e:
        _Failed = type(_raises_e)
    else:
        assert False, "pytest.raises doesn't raise an exception when it fails"


# _timer1 = Timer(1)

# def _schedule_next_test(self) -> None:
#     cocotb._write_scheduler.start_write_scheduler()

#     self._test_task._add_done_callback(
#         lambda _: cocotb._scheduler_inst.shutdown_soon()
#     )
#     cocotb._scheduler_inst._schedule_task(self._test_task)
#     cocotb._scheduler_inst._event_loop()


class TestTask(Task[None]):
    """
    The result of calling a :class:`cocotb.test` decorated object.

    All this class does is change ``__name__`` to show "Test" instead of "Task".

    .. versionchanged:: 1.8.0
        Moved to the ``cocotb.task`` module.
    """

    def __init__(self, inst: Coroutine[Any, Any, None], name: str) -> None:
        super().__init__(inst)
        self.name = f"Test {name}"


_pdb_on_exception = "COCOTB_PDB_ON_EXCEPTION" in os.environ


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
            If ``True`` and the test fails a functional check via an ``assert`` statement, :func:`pytest.raises`,
            :func:`pytest.warns`, or :func:`pytest.deprecated_call`, the test is considered to have passed.
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
        expect_error: Union[Type[BaseException], Sequence[Type[BaseException]]] = (),
        skip: bool = False,
        stage: int = 0,
        _expect_sim_failure: bool = False,
    ) -> None:
        self.func: Callable[..., Coroutine[Any, Any, None]]
        if timeout_time is not None:

            @functools.wraps(func)
            async def f(*args, **kwargs):
                running_co = Task(func(*args, **kwargs))

                try:
                    res = await with_timeout(running_co, timeout_time, timeout_unit)
                except SimTimeoutError:
                    running_co.cancel()
                    raise
                else:
                    return res

            self.func = f
        else:
            self.func = func
        self._timeout_time = timeout_time
        self._timeout_unit = timeout_unit
        self._expect_fail = expect_fail
        self._expect_error: Sequence[Type[BaseException]]
        if isinstance(expect_error, type):
            self._expect_error = (expect_error,)
        else:
            self._expect_error = expect_error
        if _expect_sim_failure:
            self._expect_error = (*self._expect_error, SimFailure)
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

        self.filename = inspect.getfile(func)
        try:
            self.lineno = inspect.getsourcelines(func)[1]
        except OSError:
            self.lineno = 1

        self._test_task: Task[None]
        self._test_outcome: Union[None, Outcome[Any]]
        self._test_start_time: float
        self._test_start_sim_time: float

    def _abort_test(self, exc: BaseException) -> None: ...

    def _test_complete(self) -> None:
        """Callback given to the scheduler, to be called when the current test completes.

        Due to the way that simulation failure is handled,
        this function must be able to detect simulation failure and finalize the regression.
        """

        # compute test completion time
        wall_time_s = time.time() - self._test_start_time
        sim_time_ns = get_sim_time("ns") - self._test_start_sim_time
        test = self._test

        # clean up write scheduler
        cocotb._write_scheduler.stop_write_scheduler()

        # score test
        if self._test_outcome is not None:
            outcome = self._test_outcome
        else:
            assert self._test_task._result is not None
            outcome = self._test_task._result
        try:
            outcome.get()
        except BaseException as e:
            result = remove_traceback_frames(e, ["_test_complete", "get"])
        else:
            result = TestSuccess()

        if (
            isinstance(result, TestSuccess)
            and not test.expect_fail
            and not test.expect_error
        ):
            self._record_test_passed(
                wall_time_s=wall_time_s,
                sim_time_ns=sim_time_ns,
                result=None,
                msg=None,
            )

        elif isinstance(result, TestSuccess) and test.expect_error:
            self._record_test_failed(
                wall_time_s=wall_time_s,
                sim_time_ns=sim_time_ns,
                result=None,
                msg="passed but we expected an error",
            )

        elif isinstance(result, TestSuccess):
            self._record_test_failed(
                wall_time_s=wall_time_s,
                sim_time_ns=sim_time_ns,
                result=None,
                msg="passed but we expected a failure",
            )

        elif isinstance(result, (AssertionError, _Failed)) and test.expect_fail:
            self._record_test_passed(
                wall_time_s=wall_time_s,
                sim_time_ns=sim_time_ns,
                result=None,
                msg="failed as expected",
            )

        elif test.expect_error:
            if isinstance(result, test.expect_error):
                self._record_test_passed(
                    wall_time_s=wall_time_s,
                    sim_time_ns=sim_time_ns,
                    result=None,
                    msg="errored as expected",
                )
            else:
                self._record_test_failed(
                    wall_time_s=wall_time_s,
                    sim_time_ns=sim_time_ns,
                    result=result,
                    msg="errored with unexpected type",
                )

        else:
            self._record_test_failed(
                wall_time_s=wall_time_s,
                sim_time_ns=sim_time_ns,
                result=result,
                msg=None,
            )

            if _pdb_on_exception:
                pdb.post_mortem(result.__traceback__)

        # continue test loop, assuming sim failure or not
        return self._execute()


def start_soon(
    coro: "Union[Task[ResultType], Coroutine[Any, Any, ResultType]]",
) -> "Task[ResultType]":
    """
    Schedule a coroutine to be run concurrently.

    Note that this is not an ``async`` function,
    and the new task will not execute until the calling task yields control.

    Args:
        coro: A task or coroutine to be run.

    Returns:
        The :class:`~cocotb.task.Task` that is scheduled to be run.

    .. versionadded:: 1.6.0
    """
    task = create_task(coro)
    task._schedule_resume()
    return task


async def start(
    coro: "Union[Task[ResultType], Coroutine[Any, Any, ResultType]]",
) -> "Task[ResultType]":
    """
    Schedule a coroutine to be run concurrently, then yield control to allow pending tasks to execute.

    The calling task will resume execution before control is returned to the simulator.

    When the calling task resumes, the newly scheduled task may have completed,
    raised an Exception, or be pending on a :class:`~cocotb.triggers.Trigger`.

    Args:
        coro: A task or coroutine to be run.

    Returns:
        The :class:`~cocotb.task.Task` that has been scheduled and allowed to execute.

    .. versionadded:: 1.6.0
    """
    task = start_soon(coro)
    await NullTrigger()
    return task


def _task_done_callback(task: Task[Any]) -> None:
    e = task.exception()
    # there was a failure and no one is watching, fail test
    if isinstance(e, (TestSuccess, AssertionError)):
        task.log.info("Test stopped by this task")
        current_test()._abort_test(e)
    else:
        task.log.error("Exception raised by this task")
        current_test()._abort_test(e)


def create_task(
    coro: "Union[Task[ResultType], Coroutine[Any, Any, ResultType]]",
) -> "Task[ResultType]":
    """
    Construct a coroutine into a :class:`~cocotb.task.Task` without scheduling the task.

    The task can later be scheduled with :func:`cocotb.start` or :func:`cocotb.start_soon`.

    Args:
        coro: An existing task or a coroutine to be wrapped.

    Returns:
        Either the provided :class:`~cocotb.task.Task` or a new Task wrapping the coroutine.

    .. versionadded:: 1.6.0
    """
    if isinstance(coro, Task):
        return coro
    task = Task(coro)
    task._add_done_callback(_task_done_callback)
    return task
