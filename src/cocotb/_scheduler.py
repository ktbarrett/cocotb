#!/usr/bin/env python

# Copyright (c) 2013, 2018 Potential Ventures Ltd
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

"""Task scheduler.

FIXME: We have a problem here. If a task schedules a read-only but we
also have pending writes we have to schedule the ReadWrite callback before
the ReadOnly (and this is invalid, at least in Modelsim).
"""

import logging
import os
import threading
from collections import OrderedDict
from typing import Any, Callable, Set

from cocotb._exceptions import InternalError
from cocotb._outcomes import Error, Outcome, Value, capture
from cocotb.task import Task
from cocotb.triggers import Event

# Sadly the Python standard logging module is very slow so it's better not to
# make any calls by testing a boolean flag first
_debug = "COCOTB_SCHEDULER_DEBUG" in os.environ


class external_state:
    INIT = 0
    RUNNING = 1
    PAUSED = 2
    EXITED = 3


class external_waiter:
    def __init__(self):
        self._outcome = None
        self.thread = None
        self.event = Event()
        self.state = external_state.INIT
        self.cond = threading.Condition()
        self._log = logging.getLogger(f"cocotb.bridge.{self.thread}.0x{id(self):x}")

    @property
    def result(self):
        return self._outcome.get()

    def _propagate_state(self, new_state):
        with self.cond:
            if _debug:
                self._log.debug(
                    f"Changing state from {self.state} -> {new_state} from {threading.current_thread()}"
                )
            self.state = new_state
            self.cond.notify()

    def thread_done(self):
        if _debug:
            self._log.debug(f"Thread finished from {threading.current_thread()}")
        self._propagate_state(external_state.EXITED)

    def thread_suspend(self):
        self._propagate_state(external_state.PAUSED)

    def thread_start(self):
        if self.state > external_state.INIT:
            return

        if not self.thread.is_alive():
            self._propagate_state(external_state.RUNNING)
            self.thread.start()

    def thread_resume(self):
        self._propagate_state(external_state.RUNNING)

    def thread_wait(self):
        if _debug:
            self._log.debug(
                f"Waiting for the condition lock {threading.current_thread()}"
            )

        with self.cond:
            while self.state == external_state.RUNNING:
                self.cond.wait()

            if _debug:
                if self.state == external_state.EXITED:
                    self._log.debug(
                        f"Thread {self.thread} has exited from {threading.current_thread()}"
                    )
                elif self.state == external_state.PAUSED:
                    self._log.debug(
                        f"Thread {self.thread} has called yield from {threading.current_thread()}"
                    )
                elif self.state == external_state.RUNNING:
                    self._log.debug(
                        f"Thread {self.thread} is in RUNNING from {threading.current_thread()}"
                    )

            if self.state == external_state.INIT:
                raise Exception(
                    f"Thread {self.thread} state was not allowed from {threading.current_thread()}"
                )

        return self.state


class Scheduler:
    """The main Task scheduler.

    How It Generally Works:
        Tasks are `queued` to run in the scheduler with :meth:`_queue`.
        Queueing adds the Task and an Outcome value to :attr:`_pending_tasks`.
        The main scheduling loop is located in :meth:`_event_loop` and loops over the queued Tasks and `schedules` them.
        :meth:`_schedule` schedules a Task -
        continuing its execution from where it previously yielded control -
        by injecting the Outcome value associated with the Task from the queue.
        The Task's body will run until it finishes or reaches the next ``await`` statement.
        If a Task reaches an ``await``, :meth:`_schedule` will convert the value yielded from the Task into a Trigger with :meth:`_trigger_from_any` and its friend methods.
        Triggers are then `primed` (with :meth:`~cocotb.triggers.Trigger._prime`)
        with a `react` function (:meth:`_sim_react` or :meth:`_react)
        so as to wake up Tasks waiting for that Trigger to `fire` (when the event encoded by the Trigger occurs).
        This is accomplished by :meth:`_resume_task_upon`.
        :meth:`_resume_task_upon` also associates the Trigger with the Task waiting on it to fire by adding them to the :attr:`_trigger2tasks` map.
        If, instead of reaching an ``await``, a Task finishes, :meth:`_schedule` will cause the :class:`~cocotb.triggers.Join` trigger to fire.
        Once a Trigger fires it calls the react function which queues all Tasks waiting for that Trigger to fire.
        Then the process repeats.

        When a Task is cancelled (:meth:`_unschedule`), it is removed from the Task queue if it is currently queued.
        Also, the Task and Trigger are deassociated in the :attr:`_trigger2tasks` map.
        If the cancelled Task is the last Task waiting on a Trigger, that Trigger is `unprimed` to prevent it from firing.

    Simulator Phases:
        All GPITriggers (triggers that are fired by the simulator) go through :meth:`_sim_react`
        which looks at the fired GPITriggers to determine and track the current simulator phase cocotb is executing in.

        Normal phase:
            Corresponds to all non-ReadWrite and non-ReadOnly phases.
            Any writes are cached for the next ReadWrite phase and do not happen immediately.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are valid.

        ReadWrite phase:
            Corresponds to ``cbReadWriteSynch`` (VPI) or ``vhpiCbRepLastKnownDeltaCycle`` (VHPI).
            At the start of scheduling in this phase we play back all the *previously* cached write updates.
            Any writes are cached for the next ReadWrite phase and do not happen immediately.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are valid.
            One caveat is that scheduling a :class:`~cocotb.triggers.ReadWrite` while in this phase may not be valid.
            If there were no writes applied at the beginning of this phase, there will be no more events in this time step,
            and there will not be another ReadWrite phase in this time step.
            Simulators generally handle this caveat gracefully by leaving you in the ReadWrite phase of the next time step.

        ReadOnly phase
            Corresponds to ``cbReadOnlySynch`` (VPI) or ``vhpiCbRepEndOfTimeStep`` (VHPI).
            In this state we are not allowed to perform writes.
            Scheduling :class:`~cocotb.triggers.ReadWrite` and :class:`~cocotb.triggers.ReadOnly` are *not* valid.

    Caveats and Special Cases:
        The scheduler treats Tests specially.
        If a Test finishes or a Task ends with an Exception, the scheduler is put into a `terminating` state.
        All currently queued Tasks are cancelled and all pending Triggers are unprimed.
        This is currently spread out between :meth:`_handle_termination` and :meth:`_cleanup`.
        In that mix of functions, the :attr:`_test_complete_cb` callback is called to inform whomever (the regression_manager) the test finished.
        The scheduler also is responsible for starting the next Test in the Normal phase by priming a ``Timer(1)`` with the second half of test completion handling.

        The scheduler is currently where simulator time phase is tracked.
        This is mostly because this is where :meth:`_sim_react` is most conveniently located.
        The scheduler can't currently be made independent of simulator-specific code because of the above special cases which have to respect simulator phasing.

        Currently Task cancellation is accomplished with :meth:`Task.kill() <cocotb.task.Task.kill>`.
        This function immediately cancels the Task by re-entering the scheduler.
        This can cause issues if you are trying to cancel the Test Task or the currently executing Task.

        TODO: There are attributes and methods for dealing with "externals", but I'm not quite sure how it all works yet.
    """

    # Singleton events, recycled to avoid spurious object creation
    _none_outcome = Value(None)

    def __init__(self, test_complete_cb: Callable[[], None]) -> None:
        self._test_complete_cb = test_complete_cb

        self.log = logging.getLogger("cocotb.scheduler")
        if _debug:
            self.log.setLevel(logging.DEBUG)

        self._pending_tasks: Set[Task[Any]] = {}
        self._scheduled_tasks: OrderedDict[Task[Any], Outcome] = OrderedDict()
        self._pending_threads = []
        self._pending_events = []  # Events we need to call set on once we've unwound

        self._terminate = False
        self._main_thread = threading.current_thread()

        self._current_task = None

    def _handle_termination(self) -> None:
        """
        Handle a termination that causes us to move onto the next test.
        """
        if _debug:
            self.log.debug("Scheduler terminating...")

        # cleanup triggers and tasks
        self._cleanup()

        # clear state
        self._terminate = False

        # call complete cb, may schedule another test
        self._test_complete_cb()

    def _event_loop(self) -> None:
        """Run the main event loop.

        This should only be started by:
        * The beginning of a test, when there is no trigger to react to
        * A GPI trigger
        """

        while self._scheduled_tasks and not self._terminate:
            task, outcome = self._scheduled_tasks.popitem(last=False)

            if _debug:
                self.log.debug(f"Scheduling task {task}")
            self._resume_task(task, outcome)
            if _debug:
                self.log.debug(f"Scheduled task {task}")

            # remove our reference to the objects at the end of each loop,
            # to try and avoid them being destroyed at a weird time (as
            # happened in gh-957)
            del task

            # Schedule may have queued up some events so we'll burn through those
            while self._pending_events:
                if _debug:
                    self.log.debug(
                        f"Scheduling pending event {self._pending_events[0]}"
                    )
                self._pending_events.pop(0).set()

        # no more pending tasks
        if self._terminate:
            self._handle_termination()
        elif _debug:
            self.log.debug("All tasks scheduled, handing control back to simulator")

    def _unschedule(self, task: Task[Any]) -> None:
        """Unschedule a task and unprime dangling pending triggers.

        Also:
          * enters the scheduler termination state if the Test Task is unscheduled.
          * creates and fires a :class:`~cocotb.triggers.Join` trigger.
          * forcefully ends the Test if a Task ends with an exception.
        """

        # remove task from queue
        if task in self._scheduled_tasks:
            self._scheduled_tasks.pop(task)
        elif task in self._pending_tasks:
            self._pending_tasks.pop(task)
        else:
            raise RuntimeError("Unregistered Task that was never registered")

    def _queue(self, task: Task[Any], outcome: Outcome[Any] = _none_outcome) -> None:
        """Queue *task* for scheduling.

        It is an error to attempt to queue a task that has already been queued.
        """
        # Don't queue the same task more than once (gh-2503)
        if task in self._scheduled_tasks:
            raise InternalError("Task was queued more than once.")
        self._scheduled_tasks[task] = outcome

    def _queue_function(self, task):
        """Queue a task for execution and move the containing thread
        so that it does not block execution of the main thread any longer.
        """
        # We should be able to find ourselves inside the _pending_threads list
        matching_threads = [
            t for t in self._pending_threads if t.thread == threading.current_thread()
        ]
        if len(matching_threads) == 0:
            raise RuntimeError("queue_function called from unrecognized thread")

        # Raises if there is more than one match. This can never happen, since
        # each entry always has a unique thread.
        (t,) = matching_threads

        async def wrapper():
            # This function runs in the scheduler thread
            try:
                _outcome = Value(await task)
            except BaseException as e:
                _outcome = Error(e)
            event.outcome = _outcome
            # Notify the current (scheduler) thread that we are about to wake
            # up the background (`@external`) thread, making sure to do so
            # before the background thread gets a chance to go back to sleep by
            # calling thread_suspend.
            # We need to do this here in the scheduler thread so that no more
            # tasks run until the background thread goes back to sleep.
            t.thread_resume()
            event.set()

        event = threading.Event()
        self._queue(Task(wrapper()))
        # The scheduler thread blocks in `thread_wait`, and is woken when we
        # call `thread_suspend` - so we need to make sure the task is
        # queued before that.
        t.thread_suspend()
        # This blocks the calling `@external` thread until the task finishes
        event.wait()
        return event.outcome.get()

    def _run_in_executor(self, func, *args, **kwargs):
        """Run the task in a separate execution thread
        and return an awaitable object for the caller.
        """
        # Create a thread
        # Create a trigger that is called as a result of the thread finishing
        # Create an Event object that the caller can await on
        # Event object set when the thread finishes execution, this blocks the
        # calling task (but not the thread) until the external completes

        def execute_external(func, _waiter):
            _waiter._outcome = capture(func, *args, **kwargs)
            if _debug:
                self.log.debug(
                    f"Execution of external routine done {threading.current_thread()}"
                )
            _waiter.thread_done()

        async def wrapper():
            waiter = external_waiter()
            thread = threading.Thread(
                group=None,
                target=execute_external,
                name=func.__qualname__ + "_thread",
                args=([func, waiter]),
                kwargs={},
            )

            waiter.thread = thread
            self._pending_threads.append(waiter)

            await waiter.event.wait()

            return waiter.result  # raises if there was an exception

        return wrapper()

    def _resume_task(self, task: Task, outcome: Outcome[Any]) -> None:
        """Schedule *task* with *outcome*.

        Args:
            task: The task to schedule.
            outcome: The outcome to inject into the *task*.

        Scheduling runs *task* until it either finishes or reaches the next ``await`` statement.
        If *task* completes, it is unscheduled.
        """
        if self._current_task is not None:
            raise InternalError("_schedule() called while another Task is executing")
        try:
            self._current_task = task

            task._advance(outcome=outcome)

            if task.done():
                if _debug:
                    self.log.debug(f"{task} completed with {task._outcome}")
                self._unschedule(task)
            else:
                self._pending_tasks.add(task)

            # We do not return from here until pending threads have completed, but only
            # from the main thread, this seems like it could be problematic in cases
            # where a sim might change what this thread is.

            if self._main_thread is threading.current_thread():
                for ext in self._pending_threads:
                    ext.thread_start()
                    if _debug:
                        self.log.debug(
                            f"Blocking from {threading.current_thread()} on {ext.thread}"
                        )
                    state = ext.thread_wait()
                    if _debug:
                        self.log.debug(
                            f"Back from wait on self {threading.current_thread()} with newstate {state}"
                        )
                    if state == external_state.EXITED:
                        self._pending_threads.remove(ext)
                        self._pending_events.append(ext.event)
        finally:
            self._current_task = None

    def _cleanup(self) -> None:
        """Clear up all our state.

        Unprime all pending triggers and kill off any tasks, stop all externals.
        """
        while self._pending_tasks:
            task = self._pending_tasks.pop()
            task.kill()

        # Kill any queued coroutines.
        # We use a while loop because task.kill() calls _unschedule(), which will remove the task from _scheduled_tasks.
        # If that happens a for loop will stop early and then the assert will fail.
        while self._scheduled_tasks:
            task, _ = self._scheduled_tasks.popitem(last=False)
            task.kill()

        if self._main_thread is not threading.current_thread():
            raise Exception("Cleanup() called outside of the main thread")

        for ext in self._pending_threads:
            # TODO stop externals???
            self.log.warning(f"Waiting for {ext.thread} to exit")

    def shutdown_soon(self) -> None:
        self._terminate = True
