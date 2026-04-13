*************************
Advanced Testing Features
*************************

.. change name?

We saw in :doc:`first_steps` how to write a simple test.
This section covers more advanced testing features of cocotb, such as:

* Parametrizing tests to generate multiple test cases from a single test function.
* Expecting failures.
* Skipping tests based on conditions.
* Test timeouts.

This document will go over the features provided by the :class:`.RegressionManager` test runner,
which is the default test runner for cocotb when using
no pytest plugin features, which are documented in :doc:`pytest`.
