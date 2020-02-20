Custom Entry Point
==================

By default, cocotb starts the regression manager as the testing framework for cocotb.
However, there are many reasons why a user might want to replace the whole or a part of the existing Python framework with an alternative.
To support this, the environment variable :envvar:`COCOTB_ENTRY` can be used.
This allows the user to specify the location of an alternative testing framework to run at initialization.

.. versionadded:: 1.4

How It Works
------------

After GPI implementations (VPI, VHPI, FLI, etc.) are registered,
the Python interface to the GPI (henceforth, PyGPI) does the following:
 - starts the Python interpreter
 - loads the ``cocotb`` module
 - gets references to necessary helper functions from the module
 - calls the ``_initialise_testbench`` function in the module
 - ``_initialise_testbench`` starts the regression manager.
The :envvar:`COCOTB_ENTRY` variable allows the user to specify a module and function other than ``cocotb._initialise_testbench`` that contains the alternative testing framework.
See the documentation for :envvar:`COCOTB_ENTRY` for details on the entry point specification.

Entry Point Interface Requirements
----------------------------------

.. currentmodule:: cocotb

PyGPI expects the following functions to be implemented in the entry module. The names are required.

.. autofunction:: _sim_event

Finally, there is the entry function itself. ``_initialise_testbench`` is being used as an example.
The entry function is not required to be named the same.
An entry module can contain multiple entry functions.

.. autofunction:: _initialise_testbench
