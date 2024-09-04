# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause


# Debug mode controlled by environment variables
import cProfile
import os
import pstats
from typing import Union

_profile: Union[cProfile.Profile, None] = None


if "COCOTB_ENABLE_PROFILING" in os.environ:
    _profile = cProfile.Profile()

    def finalize() -> None:
        ps = pstats.Stats(_profile).sort_stats("cumulative")
        ps.dump_stats("cocotb.pstat")

    enable = _profile.enable

    disable = _profile.disable

else:

    def finalize() -> None:
        pass

    def enable() -> None:
        pass

    def disable() -> None:
        pass
