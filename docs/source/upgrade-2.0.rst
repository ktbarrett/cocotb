=======================
Upgrading to cocotb 2.0
=======================

****************************
Removed :func:`!cocotb.fork`
****************************

Change
======

:external+cocotb19:py:func:`cocotb.fork` was removed and replaced with :func:`cocotb.start_soon`.

How to Upgrade
==============

* Replace all instances of :func:`!cocotb.fork` with :func:`!cocotb.start_soon`.
* Run tests to check for any changes in behavior.

.. code-block:: python

    ### Old way with cocotb.fork() ###
    task = cocotb.fork(drive_clk())

    ### New way with cocotb.start_soon() ###
    task = cocotb.start_soon(drive_clk())

Rationale
=========

:func:`!cocotb.fork` would turn :term:`coroutine`\s into :class:`~cocotb.task.Task`\s that would run concurrently to the current :term:`task`.
However, it would immediately run the coroutine until the first :keyword:`await` was seen.
This made the scheduler re-entrant and caused a series of hard to diagnose bugs
and required extra state/sanity checking leading to runtime overhead.
For these reasons :func:`!cocotb.fork` was deprecated in cocotb 1.7 and replaced with :func:`!cocotb.start_soon`.
:func:`!cocotb.start_soon` does not start the coroutine immediately, but rather "soon",
preventing scheduler re-entrancy and sidestepping an entire class of bugs and runtime overhead.

`The cocotb blog post on this change <https://fossi-foundation.org/blog/2021-10-20-cocotb-1-6-0>`_
is very illustrative of how :func:`!cocotb.start_soon` and :func:`!cocotb.fork` are different.

Caveats
=======

Coroutines run immediately
--------------------------

There is a slight change in behavior due to :func:`!cocotb.start_soon` not running the given coroutine immediately.
This will not matter in most cases, but cases where it does matter are difficult to spot.

If you have a coroutine (the parent) which :func:`!cocotb.fork`\ s another coroutine (the child)
and expects the child coroutine to run to a point before allowing the parent to continue running,
you will have to add additional code to ensure that happens.

In general, the easiest way to fix this is to add an :class:`await NullTrigger() <cocotb.triggers.NullTrigger>` after the call to :func:`!cocotb.start_soon`.

.. code-block:: python

    async def hello_world():
        cocotb.log.info("Hello, world!")

    ### Behavior of the old cocotb.fork() ###

    cocotb.fork(hello_world())
    # "Hello, world!"

    ### Behavior of the new cocotb.start_soon() ###

    cocotb.start_soon(hello_world())
    # No print...
    await NullTrigger()
    # "Hello, world!"

One caveat of this approach is that :class:`!NullTrigger` also allows every other scheduled coroutine to run as well.
But this should generally not be an issue.

If you require the "runs immediately" behavior of :func:`!cocotb.fork`,
but are not calling it from a :term:`coroutine function`,
update the function to be a coroutine function and add an ``await NullTrigger``, if possible.
Otherwise, more serious refactorings will be necessary.


Exceptions before the first :keyword:`!await`
---------------------------------------------

Also worth noting is that with :func:`!cocotb.fork`, if there was an exception before the first :keyword:`!await`,
that exception would be thrown back to the caller of :func:`!cocotb.fork` and the ``Task`` object would not be successfully constructed.

.. code-block:: python

    async def has_exception():
        if variable_does_not_exit:  # throws NameError
            await Timer(1, 'ns')

    ### Behavior of the old cocotb.fork() ###

    try:
        task = cocotb.fork(has_exception())  # NameError comes out here
    except NameError:
        cocotb.log.info("Got expected NameError!")
    # no task object exists

    ### Behavior of the new cocotb.start_soon() ###

    task = cocotb.start_soon(has_exception())
    # no exception here
    try:
        await task  # NameError comes out here
    except NameError:
        cocotb.log.info("Got expected NameError!")


************************************
:deco:`!cocotb.coroutine` Coroutines
************************************

Change
======

Support for generator-based coroutines using the :external+cocotb19:py:class:`@cocotb.coroutine <cocotb.coroutine>` decorator
with Python :term:`generator functions <generator>` was removed.

How to Upgrade
==============

* Remove the :deco:`!cocotb.coroutine` decorator.
* Add :keyword:`!async` keyword directly before the :keyword:`def` keyword in the function definition.
* Replace any ``yield [triggers, ...]`` with :class:`await First(triggers, ...) <cocotb.triggers.First>`.
* Replace all ``yield``\ s in the function with :keyword:`await`\ s.
* Remove all imports of the :deco:`!cocotb.coroutine` decorator

.. code-block:: python

    ### Old way with @cocotb.coroutine ###

    @cocotb.coroutine
    def my_driver():
        yield [RisingEdge(dut.clk), FallingEdge(dut.areset_n)]
        yield Timer(random.randint(10), 'ns')

    ### New way with async/await ###

    async def my_driver():  # async instead of @cocotb.coroutine
        await First(RisingEdge(dut.clk), FallingEdge(dut.areset_n))  # await First() instead of yield [...]
        await Timer(random.randint(10), 'ns')  # await instead of yield

Rationale
=========

These existed to support defining coroutines in Python 2 and early versions of Python 3 before :term:`coroutine functions <coroutine function>`
using the :keyword:`!async`\ /:keyword:`!await` syntax was added in Python 3.5.
We no longer support versions of Python that don't support :keyword:`!async`\ /:keyword:`!await`,
Python coroutines are noticeably faster than :deco:`!cocotb.coroutine`'s implementation,
and the behavior of :deco:`!cocotb.coroutine` would have to be changed to support changes to the scheduler.
For all those reasons the :deco:`!cocotb.coroutine` decorator and generator-based coroutine support was removed.

********************************************************
:class:`!BinaryValue` replaced with :class:`!LogicArray`
********************************************************

Change
======

:external+cocotb19:py:class:`.BinaryValue` and :external+cocotb19:py:class:`.BinaryRepresentation` were removed and replaced with the existing :class:`.Logic` and :class:`.LogicArray`.

How to Upgrade
==============

* Change all constructions of :class:`!BinaryValue` to :class:`!LogicArray`.
* Remove passing of :class:`!BinaryRepresentation` to the constructor and setting of the :attr:`!BinaryValue.binaryRepresentation` attribute.
    * Replace construction from :class:`int` with :meth:`.LogicArray.from_unsigned` or :meth:`.LogicArray.from_signed`.
    * Replace all conversion from :class:`!LogicArray` to :class:`!int` with :meth:`.LogicArray.to_unsigned` or :class:`.LogicArray.to_signed`.
* Remove passing ``bigEndian`` argument to the constructor and setting of the :attr:`!BinaryValue.big_endian` attribute.
    * Replace construction from :class:`bytes` with :meth:`LogicArray.from_bytes` and pass the appropriate ``byteorder`` argument.
    * Replace conversion to :class:`!bytes` with :meth:`LogicArray.to_bytes` and pass the appropriate ``byteorder`` argument.
* Convert all objects to :class:`!int` as described above before doing any arithmetic operation, such as ``+``, ``-``, ``/``, ``//``, ``%``, ``**``, ``- (unary)``, ``+ (unary)``, ``abs(value)``, ``>>``, ``<<``.
* Change bit indexing and slicing to use the indexing provided by the ``range`` argument to the constructor.
    * Passing an :class:`!int` as the ``range`` argument will default the range to :class:`Range(range-1, "downto", 0) <cocotb.types.Range>`.
      This means index ``0`` will be the rightmost bit and not the leftmost bit like in :class:`BinaryValue`.
      Pass ``Range(0, range-1)`` when constructing :class:`!LogicArray` to retain the old indexing scheme, or update the indexing and slicing usage.

Rationale
=========

The :external+cocotb19:py:class:`.BinaryValue` class had some issues fundamental to it's design.
One was the fact many method's behavior was dependent upon mutable state,
which makes operating on these values with higher-order functions difficult or tedious.
Data types should *not* be stateful as much as possible.
Second was the confusing conflation of bit and byte endianness.
Third was the inconsistent reporting of warnings or errors.
Fourth was that these things combined to yield "correct", but unexpected results in some cases.
It was difficult to use correctly and easy to use incorrectly, which are the hallmarks of a bad API.

Unfortunately, a gradual change is not really possible with such core functionality,
so it had to be outright replaced in one step.

Caveats
=======
