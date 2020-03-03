.. _gpi-users:

******************
GPI User Libraries
******************

GPI user libraries are dynamically loaded libraries that control simulators via the GPI.
They can be used to build alternative language bindings to the GPI,
or extending existing GPI users like cocotb.
The Python/GPI interface used by cocotb is an example of a GPI user library.
GPI users are specified via the :envvar:`GPI_USERS` environment variable.

.. warning:: This is intended for advanced users only

.. versionadded:: 1.4

How It Works
============

Users of the GPI do not directly run as an application, but instead they are libraries that are dynamically loaded by the simulator at startup (after elaboration).
User libraries will contain an entry function that looks a lot like the classic ``main`` function from C.
The GPI will dynamically load these user libraries and call into the entry function, checking for a non-``0`` return code.
The user library and entry function will be specified to the GPI via an environment variable: :envvar:`GPI_USERS`.

If any of the following errors occurs when attempting to load GPI users, the GPI will immediately cleanup and allow the simulator to exit.

 * Unable to parse :envvar:`GPI_USERS` correctly.
 * The :envvar:`GPI_USERS` is empty: there are no users.
 * Unable to load a user library.
 * Unable to locate the specified entry function.
 * An entry function returns a non-``0`` value.

How To Write And Load A User Library
====================================

Writing The Library
-------------------

The only requirement on user libraries is that they contain an entry function with the following signature. Note the function does not have to be called ``main``.

.. c:function:: int main(int argc, char const* const*);

The entry function can do anything it needs to set up the user; however, before simulation starts, it must return.
The entry function should return a ``0`` if there has been no error in setup.
Returning any other value implies a failure, and if any entry function fails, the GPI and simulator will shut down immediately.
Typically the entry function will use GPI functions like :any:`gpi_register_sim_event_callback` or :any:`gpi_register_timed_callback` to schedule reactions to simulator events.

Building The Library
--------------------

Users need to compile their libraries in such a way they can be dynamically loaded.
This varies by operating system.
Once your library is compiled, install it into your operating system's library search path,or modify your operating system's library search path to point to the library.

Using The Library
-----------------

