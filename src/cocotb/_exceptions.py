# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause


from typing import Sequence


class InternalError(BaseException):
    """An error internal to scheduler. If you see this, report a bug!"""


TestFailures: Sequence[BaseException] = [
    AssertionError,
]

try:
    import pytest
except ModuleNotFoundError:
    pass
else:
    # pytest.raises.Exception is the exception raised by
    # pytest.raises and pytest.warns.
    TestFailures.append(pytest.raises.Exception)

TestFailures = tuple(TestFailures)
