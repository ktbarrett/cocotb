# Copyright cocotb contributors
# Copyright (c) 2013 Potential Ventures Ltd
# Copyright (c) 2013 SolarFlare Communications Inc
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
import ast
import logging
import os
import random
import sys
import time
import warnings
from pathlib import Path
from types import SimpleNamespace
from typing import List, cast

import cocotb
import cocotb._profiling
import cocotb.handle
import cocotb.logging
from cocotb import simulator
from cocotb._shutdown import register_shutdown_callback, run_shutdown_callbacks

log: logging.Logger


def init_package(argv: List[str]) -> None:
    """Initialize the cocotb package from any context."""

    simulator.set_sim_event_callback(run_shutdown_callbacks)

    # From https://www.python.org/dev/peps/pep-0565/#recommended-filter-settings-for-test-runners
    # If the user doesn't want to see these, they can always change the global
    # warning settings in their test module.
    if not sys.warnoptions:
        warnings.simplefilter("default")

    # sys.path normally includes "" (the current directory), but does not appear to when python is embedded.
    # Add it back because users expect to be able to import files in their test directory.
    sys.path.insert(0, "")

    # setup loggers so they can be used later during initialization
    cocotb.logging._init()
    _setup_loggers()

    # setup all non-simulator variables in package
    cocotb.argv = argv
    _process_plusargs()
    _setup_random_seed()
    _start_user_coverage()
    cocotb._profiling.initialize()

    log.info(
        "Initialized cocotb v%s from %s",
        cocotb.__version__,
        Path(__file__).parent.absolute(),
    )


def _setup_loggers() -> None:
    cocotb.log = logging.getLogger("test")
    cocotb.log.setLevel(logging.INFO)

    global log
    log = logging.getLogger("cocotb")


def init_package_from_simulation(argv: List[str]) -> None:
    """Initialize the cocotb package from a simulation context."""
    init_package(argv)

    # setup simulator variables
    cocotb.is_simulation = True
    cocotb.SIM_NAME = simulator.get_simulator_product().strip()
    cocotb.SIM_VERSION = simulator.get_simulator_version().strip()
    _setup_root_handle()
    _process_packages()

    log.info("Running on %s version %s", cocotb.SIM_NAME, cocotb.SIM_VERSION)


def _process_plusargs() -> None:
    cocotb.plusargs = {}

    for option in cocotb.argv:
        if option.startswith("+"):
            if option.find("=") != -1:
                (name, value) = option[1:].split("=", 1)
                cocotb.plusargs[name] = value
            else:
                cocotb.plusargs[option[1:]] = True


def _process_packages() -> None:
    pkg_dict = {}

    pkgs = simulator.package_iterate()
    if pkgs is None:
        cocotb.packages = SimpleNamespace()
        return

    for pkg in pkgs:
        handle = cast(
            "cocotb.handle.HierarchyObject", cocotb.handle._make_sim_object(pkg)
        )
        name = handle._name

        # Icarus doesn't support named access to package objects:
        # https://github.com/steveicarus/iverilog/issues/1038
        # so we cannot lazily create handles
        if cocotb.SIM_NAME == "Icarus Verilog":
            handle._discover_all()
        pkg_dict[name] = handle

    cocotb.packages = SimpleNamespace(**pkg_dict)


def _start_user_coverage() -> None:
    coverage_envvar = os.getenv("COCOTB_USER_COVERAGE")
    if coverage_envvar is None:
        coverage_envvar = os.getenv("COVERAGE")
        if coverage_envvar is not None:
            warnings.warn(
                "COVERAGE is deprecated in favor of COCOTB_USER_COVERAGE",
                DeprecationWarning,
                stacklevel=2,
            )
    if coverage_envvar:
        try:
            import coverage  # noqa: PLC0415
        except ImportError:
            raise RuntimeError(
                "Coverage collection requested but coverage module not available. Install it using `pip install coverage`."
            ) from None
        else:
            config_filepath = os.getenv("COCOTB_COVERAGE_RCFILE")
            if config_filepath is None:
                config_filepath = os.getenv("COVERAGE_RCFILE")
                if config_filepath is not None:
                    warnings.warn(
                        "COVERAGE_RCFILE is deprecated in favor of COCOTB_COVERAGE_RCFILE",
                        DeprecationWarning,
                        stacklevel=2,
                    )
            if config_filepath is None:
                # Exclude cocotb itself from coverage collection.
                log.info(
                    "Collecting coverage of user code. No coverage config file supplied via COCOTB_COVERAGE_RCFILE."
                )
                cocotb_package_dir = Path(__file__).parent.absolute()
                user_coverage = coverage.coverage(
                    branch=True, omit=[f"{cocotb_package_dir}/*"]
                )
            else:
                log.info(
                    "Collecting coverage of user code. Coverage config file supplied."
                )
                # Allow the config file to handle all configuration
                user_coverage = coverage.coverage(config_file=config_filepath)
            user_coverage.start()

            def stop_user_coverage(_: object) -> None:
                user_coverage.stop()
                log.debug("Writing user coverage data")
                user_coverage.save()

            register_shutdown_callback(stop_user_coverage)


def _setup_random_seed() -> None:
    seed_envvar = os.getenv("COCOTB_RANDOM_SEED")
    if seed_envvar is None:
        seed_envvar = os.getenv("RANDOM_SEED")
        if seed_envvar is not None:
            warnings.warn(
                "RANDOM_SEED is deprecated in favor of COCOTB_RANDOM_SEED",
                DeprecationWarning,
                stacklevel=2,
            )
    if seed_envvar is None:
        if "ntb_random_seed" in cocotb.plusargs:
            plusarg_seed = cocotb.plusargs["ntb_random_seed"]
            if not isinstance(plusarg_seed, str):
                raise TypeError("ntb_random_seed plusarg is not a valid seed value.")
            seed = ast.literal_eval(plusarg_seed)
            if not isinstance(seed, int):
                raise TypeError("ntb_random_seed plusargs is not a valid seed value.")
            cocotb.RANDOM_SEED = seed
        elif "seed" in cocotb.plusargs:
            plusarg_seed = cocotb.plusargs["seed"]
            if not isinstance(plusarg_seed, str):
                raise TypeError("seed plusarg is not a valid seed value.")
            seed = ast.literal_eval(plusarg_seed)
            if not isinstance(seed, int):
                raise TypeError("seed plusargs is not a valid seed value.")
            cocotb.RANDOM_SEED = seed
        else:
            cocotb.RANDOM_SEED = int(time.time())
        log.info("Seeding Python random module with %d", cocotb.RANDOM_SEED)
    else:
        cocotb.RANDOM_SEED = ast.literal_eval(seed_envvar)
        log.info(
            "Seeding Python random module with supplied seed %d", cocotb.RANDOM_SEED
        )

    random.seed(cocotb.RANDOM_SEED)


def _setup_root_handle() -> None:
    root_name = os.getenv("COCOTB_TOPLEVEL")
    if root_name is not None:
        root_name = root_name.strip()
        if root_name == "":
            root_name = None
        elif "." in root_name:
            # Skip any library component of the toplevel
            root_name = root_name.split(".", 1)[1]

    handle = simulator.get_root_handle(root_name)
    if not handle:
        raise RuntimeError(f"Can not find root handle {root_name!r}")

    cocotb.top = cocotb.handle._make_sim_object(handle)
