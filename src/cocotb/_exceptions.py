# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause


class InternalError(BaseException):
    """An error internal to scheduler. If you see this, report a bug!"""


# TODO remove SimFailure once we have functionality in place to abort the test without
# having to set an exception.
class SimFailure(Exception):
    """A Test failure due to simulator failure."""
