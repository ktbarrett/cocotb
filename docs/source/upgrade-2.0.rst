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

Additional Details
==================

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
* Replace construction from :class:`int` with :meth:`.LogicArray.from_unsigned` or :meth:`.LogicArray.from_signed`.
* Replace construction from :class:`bytes` with :meth:`LogicArray.from_bytes` and pass the appropriate ``byteorder`` argument.

.. code-block:: python

    # Old way with BinaryValue
    BinaryValue(10, 10)
    BinaryValue("1010", n_bits=4)
    BinaryValue(-10, 8, binaryRepresentation=BinaryRepresentation.SIGNED)
    BinaryValue(b"1234", bigEndian=True)

    # New way with LogicArray
    LogicArray.from_unsigned(10, 10)
    LogicArray("1010")
    LogicArray.from_signed(-10, 8)
    BinaryValue.from_bytes(b"1234", byteorder="big")

* Replace usage of :external+cocotb19:py:meth:`.BinaryValue.integer` and :external+cocotb19:py:meth:`.BinaryValue.signed_integer`
  with :meth:`.LogicArray.to_unsigned` or :class:`.LogicArray.to_signed`, respectively.
* Replace usage of :external+cocotb19:py:meth:`.BinaryValue.binstr` with the :class:`str` cast (this works with :class:`!BinaryValue` as well).
* Replace conversion to :class:`!bytes` with :meth:`LogicArray.to_bytes` and pass the appropriate ``byteorder`` argument.

.. code-block:: python

    # Old way with BinaryValue
    b = BinaryValue(10, 4)
    assert b.integer == 10
    assert b.signed_integer == -6
    assert b.binstr == "1010"
    assert b.buff == b"\x0a"

    # New way with LogicArray
    b = LogicArray(10, 4)
    assert b.to_unsigned() == 10
    assert b.to_signed() == -6
    assert str(b) == "1010"
    assert b.to_bytes(byteorder="big") == b"\x0a"

* Remove setting of the :attr:`!BinaryValue.big_endian` attribute to change endianness.

.. code-block:: python

    # Old way with BinaryValue
    b = BinaryValue(b"12", bigEndian=True)
    assert b.buff == b"12"
    b.big_endian = False
    assert b.buff == b"21"

    # New way with LogicArray
    b = LogicArray.from_bytes(b"12", byteorder="big")
    assert b.to_bytes(byteorder="big") == b"12"
    assert b.to_bytes(byteorder="little") == b"21"

* Convert all objects to an unsigned :class:`!int` before doing any arithmetic operation, such as ``+``, ``-``, ``/``, ``//``, ``%``, ``**``, ``- (unary)``, ``+ (unary)``, ``abs(value)``, ``>>``, ``<<``.

.. code-block:: python

    # Old way with BinaryValue
    b = BinaryValue(12, 8)
    assert 8 * b == 96
    assert b << 2 == 48
    assert b / 6 == 2.0
    assert -b == -12
    # inplace modification
    b *= 3
    assert b == 36

    # New way with LogicArray
    b = LogicArray(12, 8)
    b_int = b.to_unsigned()
    assert 8 * b_int == 96
    assert b_int << 2 == 48
    assert b_int / 6 == 2.0
    assert -b_int == -12
    # inplace modification
    b[:] = b_int * 3
    assert b == 36


* Change bit indexing and slicing to use the indexing provided by the ``range`` argument to the constructor.
    * Passing an :class:`!int` as the ``range`` argument will default the range to :class:`Range(range-1, "downto", 0) <cocotb.types.Range>`.
      This means index ``0`` will be the rightmost bit and not the leftmost bit like in :class:`BinaryValue`.
      Pass ``Range(0, range-1)`` when constructing :class:`!LogicArray` to retain the old indexing scheme, or update the indexing and slicing usage.
    * Change all negative indexing to use positive indexing

.. code-block:: python

    # Old way with BinaryValue
    val = BinaryValue(10, 4)
    assert val[0] == 1
    assert val[3] == 0
    assert val[-2] == 1

    # New way with LogicArray, specifying an ascending range
    val = LogicArray(10, Range(0, 3))
    assert val[0] == 1
    assert val[3] == 0
    assert val[3] == 1

    # New way with LogicArray, changing indexing
    val = LogicArray(10, 4)
    assert val[3] == 1
    assert val[0] == 0
    assert val[1] == 1


.. note::
    You can also use the :attr:`.LogicArray.range` object to translate ``0`` to ``len()-1`` indexing to the one used by :class:`!LogicArray`,
    but this is rather inefficient.

    .. code-block:: python

        val = LogicArray("1010", Range(3, 0))
        assert val[0] == 0  # index 0 is right-most
        ind = val.range[0]  # 0th range value is 3
        assert val[ind] == "1"  # index 3 is left-most


* Change all uses of the :attr:`.LogicArray.binstr`, :attr:`.LogicArray.integer`, :attr:`.LogicArray.signed_integer`, and :attr:`.LogicArray.buff` setters,
  as well as calls to :external+cocotb19:py:meth:`.BinaryValue.assign`, to use :class:`!LogicArray`'s setitem syntax.

.. code-block:: python



Rationale
=========

In many cases :external+cocotb19:py:class:`.BinaryValue` would behave in unexpected ways that were often reported as errors.
These unexpected behaviors were either an unfortunately product of its design or done purposefully.
They could not necessarily be "fixed" and any fix would invariably break the API.
So rather than attempt to fix it, it was outright replaced.
Unfortunately, a gradual change is not really possible with such core functionality,
so it had to be outright replaced in one step.

Additional Details
==================

Using the setters for :attr:`.LogicArray.binstr`, :attr:`.LogicArray.integer`, :attr:`.LogicArray.signed_integer`, and :attr:`.LogicArray.buff`

* partial setting with binstr and how it worked with big_endian
* truncation with warnings
* but no warning if the value was the same
* integer setter would allow signed values if set to BinaryRepresentation.TWOS_COMPLEMENT
* changing binaryRepresentation did not actually change that
* not setting n_bits in constructor allowed the BinaryValue to take on any possible sized value
* otherwise it truncated implicitly on all setting operations
* indexing without setting n_bits was broken
* negative indexing
