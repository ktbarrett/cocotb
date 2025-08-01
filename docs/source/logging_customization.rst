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
This file is loaded via :func:`logging.config.fileConfig`, so it follows the :ref:`logging-config-fileformat`.

First create a configuration file with your desired configuration:

.. code-block::
    :caption: log.conf



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

.. code-block::
    :caption: rotating_log.conf

    [handlers]
    keys=rotatingfile,stdout

    [formatters]
    keys=simlog

    [handler_rotatingfile]
    class=handlers.RotatingFileHandler
    formatter=
    args=('python.log', 'w')

    [formatter_simlog]
    class=

cocotb 1.x Extended Log Format
==============================


def _configure(_: object) -> None:
    log_config_envvar = os.environ.get("COCOTB_LOG_CONFIG")
    if log_config_envvar is not None:
        module_str, func_str = log_config_envvar.split(":", 1)
        module = importlib.import_module(module_str)
        log_config: Callable[[], object] = operator.attrgetter(func_str)(module)
        log_config()
    else:
        default_config()
