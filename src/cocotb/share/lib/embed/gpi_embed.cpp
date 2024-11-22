/******************************************************************************
 * Copyright (c) 2013, 2018 Potential Ventures Ltd
 * Copyright (c) 2013 SolarFlare Communications Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of Potential Ventures Ltd,
 *       SolarFlare Communications Inc nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL POTENTIAL VENTURES LTD BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

// Embed Python into the simulator using GPI

#include <Python.h>
#include <cocotb_utils.h>  // DEFER
#include <exports.h>       // COCOTB_EXPORT
#include <gpi.h>           // gpi_event_t
#include <gpi_logging.h>   // LOG_* macros
#include <py_gpi_logging.h>  // py_gpi_logger_set_level, py_gpi_logger_initialize, py_gpi_logger_finalize

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "locale.h"

#if defined(_WIN32)
#include <windows.h>
#define sleep(n) Sleep(1000 * n)
#define getpid() GetCurrentProcessId()
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <unistd.h>
#endif

static PyObject *pEventFn = NULL;

static int python_init_called = 0;

/** Initialize the Python interpreter
 */
extern "C" COCOTB_EXPORT void _embed_init_python(void) {
    if (python_init_called) {
        LOG_ERROR("PyGPI library initialized again!");
        return;
    }
    python_init_called = 1;

    const char *log_level = getenv("COCOTB_LOG_LEVEL");
    if (log_level) {
        static const std::map<std::string, int> logStrToLevel = {
            {"CRITICAL", GPICritical}, {"ERROR", GPIError},
            {"WARNING", GPIWarning},   {"INFO", GPIInfo},
            {"DEBUG", GPIDebug},       {"TRACE", GPITrace}};
        auto it = logStrToLevel.find(log_level);
        if (it != logStrToLevel.end()) {
            py_gpi_logger_set_level(it->second);
        } else {
            LOG_ERROR("Invalid log level: %s", log_level);
        }
    }

    const char *python_bin_path = getenv("PYGPI_PYTHON_BIN");
    if (!python_bin_path) {
        LOG_ERROR(
            "PYGPI_PYTHON_BIN variable not set, can't initialize Python.");
        return;
    }
    LOG_INFO("Using Python interpreter at %s", python_bin_path);

    // Intialize the Python initialization Config object.
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    DEFER(PyConfig_Clear(&config));

    // Set program name to the Python interpreter at PYGPI_PYTHON_BIN.
    // This is necessary to set up sys.path as if the PyGPI user was invoked as
    // a Python script.
    {
        auto status = PyConfig_SetBytesString(&config, &config.program_name,
                                              python_bin_path);
        if (PyStatus_Exception(status)) {
            LOG_ERROR("Failed to initialize Python interpreter");
            Py_ExitStatusException(status);
            return;
        }
    }

    // Initialize Python from Config object.
    {
        auto status = Py_InitializeFromConfig(&config);
        if (PyStatus_Exception(status)) {
            LOG_ERROR("Failed to initialize Python interpreter");
            Py_ExitStatusException(status);
            return;
        }
    }

    /* Before returning we check if the user wants pause the simulator thread
       such that they can attach */
    const char *pause = getenv("COCOTB_ATTACH");
    if (pause) {
        unsigned long sleep_time = strtoul(pause, NULL, 10);
        /* This should check for out-of-range parses which returns ULONG_MAX and
           sets errno, as well as correct parses that would be sliced by the
           narrowing cast */
        if (errno == ERANGE || sleep_time >= UINT_MAX) {
            LOG_ERROR("COCOTB_ATTACH only needs to be set to ~30 seconds");
            return;
        }
        if ((errno != 0 && sleep_time == 0) || (sleep_time <= 0)) {
            LOG_ERROR(
                "COCOTB_ATTACH must be set to an integer base 10 or omitted");
            return;
        }

        LOG_ERROR(
            "Waiting for %lu seconds - attach to PID %d with your debugger",
            sleep_time, getpid());
        sleep((unsigned int)sleep_time);
    }
}

/**
 * @name    Simulator cleanup
 * @brief   Called by the simulator on shutdown.
 * @ingroup python_c_api
 *
 * GILState before calling: Not held
 *
 * GILState after calling: Not held
 *
 * Makes one call to PyGILState_Ensure and one call to Py_Finalize.
 *
 * Cleans up reference counts for Python objects and calls Py_Finalize function.
 */
extern "C" COCOTB_EXPORT void _embed_sim_cleanup(void) {
    // If initialization fails, this may be called twice:
    // Before the initial callback returns and in the final callback.
    // So we check if Python is still initialized before doing cleanup.
    if (Py_IsInitialized()) {
        to_python();
        PyGILState_Ensure();  // Don't save state as we are calling Py_Finalize
        Py_DecRef(pEventFn);
        pEventFn = NULL;
        py_gpi_logger_finalize();
        Py_Finalize();
        to_simulator();
    }
}

extern "C" COCOTB_EXPORT int _embed_sim_init(int argc,
                                             char const *const *_argv) {
    // Check that we are not already initialized
    if (pEventFn) {
        return 0;
    }

    // Ensure that the current thread is ready to call the Python C API
    auto gstate = PyGILState_Ensure();
    DEFER(PyGILState_Release(gstate));

    to_python();
    DEFER(to_simulator());

    auto entry_utility_module = PyImport_ImportModule("pygpi.entry");
    if (!entry_utility_module) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(Py_DECREF(entry_utility_module));

    auto entry_info_tuple =
        PyObject_CallMethod(entry_utility_module, "load_entry", NULL);
    if (!entry_info_tuple) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(Py_DECREF(entry_info_tuple));

    PyObject *entry_module;
    PyObject *entry_point;
    if (!PyArg_ParseTuple(entry_info_tuple, "OO", &entry_module,
                          &entry_point)) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    // Objects returned from ParseTuple are borrowed from tuple

    auto log_func = PyObject_GetAttrString(entry_module, "_log_from_c");
    if (!log_func) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(Py_DECREF(log_func));

    auto filter_func = PyObject_GetAttrString(entry_module, "_filter_from_c");
    if (!filter_func) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(Py_DECREF(filter_func));

    py_gpi_logger_initialize(log_func, filter_func);

    pEventFn = PyObject_GetAttrString(entry_module, "_sim_event");
    if (!pEventFn) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    // cocotb must hold _sim_event until _embed_sim_cleanup runs

    // Build argv for cocotb module
    auto argv_list = PyList_New(argc);
    if (argv_list == NULL) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    for (int i = 0; i < argc; i++) {
        // Decode, embedding non-decodable bytes using PEP-383. This can only
        // fail with MemoryError or similar.
        auto argv_item = PyUnicode_DecodeLocale(_argv[i], "surrogateescape");
        if (!argv_item) {
            // LCOV_EXCL_START
            PyErr_Print();
            return -1;
            // LCOV_EXCL_STOP
        }
        PyList_SetItem(argv_list, i, argv_item);
    }
    DEFER(Py_DECREF(argv_list))

    auto cocotb_retval =
        PyObject_CallFunctionObjArgs(entry_point, argv_list, NULL);
    if (!cocotb_retval) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    Py_DECREF(cocotb_retval);

    return 0;
}

extern "C" COCOTB_EXPORT void _embed_sim_event(const char *msg) {
    /* Indicate to the upper layer that a sim event occurred */

    if (pEventFn) {
        PyGILState_STATE gstate;
        to_python();
        gstate = PyGILState_Ensure();

        if (msg == NULL) {
            msg = "No message provided";
        }

        PyObject *pValue = PyObject_CallFunction(pEventFn, "s", msg);
        if (pValue == NULL) {
            PyErr_Print();
            LOG_ERROR("Passing event to upper layer failed");
        }
        Py_XDECREF(pValue);
        PyGILState_Release(gstate);
        to_simulator();
    }
}
