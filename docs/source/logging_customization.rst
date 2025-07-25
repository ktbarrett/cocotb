*******************
Customizing Logging
*******************

Python's :mod:`logging` module offers functionality for customizing logging, from writing to different outputs, filtering messages, and customizing the formatting of log messages.
cocotb uses this functionality to present more meaning logging messages to the user,
but this format isn't for everyone.
Rather than have cocotb attempt to solve each user's problem, it provides a way to leverage the existing features in the :mod:`!logging` module.

File Configuration
==================

cocotb 2.0 adds :envvar:`COCOTB_LOG_CONFIG` which allows the user to create a logging configuration file for cocotb to load in place of using the :func:`~cocotb.logging.default_config`.
This file is loaded via :func:`logging.config.fileConfig`,
so it follows the :ref:`logging-config-fileformat`.

.. _rotating-logger:

Rotating Log Files
==================

The following is an example of how to support rotation of log files.
It will keep the newest 3 files which are at most 5 MiB in size.

See also :ref:`logging-reference-section`,
and the Python documentation for :class:`logging.handlers.RotatingFileHandler`.

.. code-block:: python

    from logging.handlers import RotatingFileHandler
    from cocotb.logging import SimLogFormatter

    root_logger = logging.getLogger()

    # undo the setup cocotb did
    for handler in root_logger.handlers:
        root_logger.removeHandler(handler)
        handler.close()

    # do whatever configuration you want instead
    file_handler = RotatingFileHandler("rotating.log", maxBytes=(5 * 1024 * 1024), backupCount=2)
    file_handler.setFormatter(SimLogFormatter())
    root_logger.addHandler(file_handler)

cocotb 1.x Extended Log Format
==============================
