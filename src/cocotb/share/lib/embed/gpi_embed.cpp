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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "cocotb_utils.h"  // DEFER
#include "exports.h"       // COCOTB_EXPORT
#include "gpi.h"
#include "gpi_logging.h"  // LOG_* macros
#include "locale.h"
#include "py_gpi_logging.h"  // py_gpi_logger_set_level, py_gpi_logger_initialize, py_gpi_logger_finalize

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
static bool python_init_called = 0;
static bool embed_init_called = 0;

static int get_interpreter_path(wchar_t *path, size_t path_size) {
    const char *path_c = getenv("PYGPI_PYTHON_BIN");
    if (!path_c) {
        // LCOV_EXCL_START
        LOG_ERROR(
            "PYGPI_PYTHON_BIN variable not set. Can't initialize Python "
            "interpreter!");
        return -1;
        // LCOV_EXCL_STOP
    }

    auto path_temp = Py_DecodeLocale(path_c, NULL);
    if (path_temp == NULL) {
        // LCOV_EXCL_START
        LOG_ERROR(
            "Unable to set Python Program Name. "
            "Decoding error in Python executable path.");
        LOG_INFO("Python executable path: %s", path_c);
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(PyMem_RawFree(path_temp));

    wcsncpy(path, path_temp, path_size / sizeof(wchar_t));
    if (path[(path_size / sizeof(wchar_t)) - 1]) {
        // LCOV_EXCL_START
        LOG_ERROR(
            "Unable to set Python Program Name. Path to interpreter too long");
        LOG_INFO("Python executable path: %s", path_c);
        return -1;
        // LCOV_EXCL_STOP
    }

    return 0;
}

static int m_argc;
static char const *const *m_argv;

/** Initialize the Python interpreter */
extern "C" COCOTB_EXPORT int _embed_init_python(int argc,
                                                char const *const *argv) {
    // LCOV_EXCL_START
    if (python_init_called) {
        LOG_ERROR("PyGPI library initialized again!");
        return -1;
    }
    // LCOV_EXCL_STOP
    python_init_called = 1;

    // Save argc and argv for later
    m_argc = argc;
    m_argv = argv;

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
            // LCOV_EXCL_START
            LOG_ERROR("Invalid log level: %s", log_level);
            // LCOV_EXCL_STOP
        }
    }

    // must set program name to Python executable before initialization, so
    // initialization can determine path from executable

    static wchar_t interpreter_path[PATH_MAX], sys_executable[PATH_MAX];

    auto err = get_interpreter_path(interpreter_path, sizeof(interpreter_path));
    // LCOV_EXCL_START
    if (err) {
        return -1;
    }
    // LCOV_EXCL_STOP
    LOG_INFO("Using Python interpreter at %ls", interpreter_path);

    static wchar_t progname[] = L"cocotb";
    static wchar_t *argv_[] = {progname};

#if PY_VERSION_HEX >= 0x3080000
    /* Use the new Python Initialization Configuration from Python 3.8. */
    PyConfig config;
    PyStatus status;

    PyConfig_InitPythonConfig(&config);
    DEFER(PyConfig_Clear(&config));

    PyConfig_SetString(&config, &config.program_name, interpreter_path);

    status = PyConfig_SetArgv(&config, 1, argv_);
    auto has_exc = PyStatus_Exception(status);
    // LCOV_EXCL_START
    if (has_exc) {
        LOG_ERROR("Failed to set ARGV during the Python initialization");
        if (status.err_msg != NULL) {
            LOG_ERROR("\terror: %s", status.err_msg);
        }
        if (status.func != NULL) {
            LOG_ERROR("\tfunction: %s", status.func);
        }
        return -1;
    }
    // LCOV_EXCL_STOP

    status = Py_InitializeFromConfig(&config);
    has_exc = PyStatus_Exception(status);
    // LCOV_EXCL_START
    if (has_exc) {
        LOG_ERROR("Failed to initialize Python");
        if (status.err_msg != NULL) {
            LOG_ERROR("\terror: %s", status.err_msg);
        }
        if (status.func != NULL) {
            LOG_ERROR("\tfunction: %s", status.func);
        }
        return -1;
    }
    // LCOV_EXCL_STOP
#else
    /* Use the old API. */
    Py_SetProgramName(interpreter_path);
    Py_Initialize();
    PySys_SetArgvEx(1, argv_, 0);
#endif

    /* Sanity check: make sure sys.executable was initialized to
     * interpreter_path. */

    PyObject *sys_executable_obj = PySys_GetObject("executable");
    // LCOV_EXCL_START
    if (sys_executable_obj == NULL) {
        LOG_ERROR("Failed to load sys.executable");
        return -1;
    }
    // LCOV_EXCL_STOP

    auto errl = PyUnicode_AsWideChar(sys_executable_obj, sys_executable,
                                     sizeof(sys_executable));
    // LCOV_EXCL_START
    if (errl == -1) {
        LOG_ERROR("Failed to convert sys.executable to wide string");
        return -1;
    }
    // LCOV_EXCL_STOP

    auto cmp = wcscmp(interpreter_path, sys_executable);
    // LCOV_EXCL_START
    if (cmp != 0) {
        LOG_ERROR("Unexpected sys.executable value (expected '%ls', got '%ls')",
                  interpreter_path, sys_executable);
        return -1;
    }
    // LCOV_EXCL_STOP

    /* Before returning we check if the user wants pause the simulator thread
       such that they can attach */
    const char *pause = getenv("COCOTB_ATTACH");
    if (pause) {
        unsigned long sleep_time = strtoul(pause, NULL, 10);

        // LCOV_EXCL_START
        /* This should check for out-of-range parses which returns ULONG_MAX and
           sets errno, as well as correct parses that would be sliced by the
           narrowing cast */
        if (errno == ERANGE || sleep_time >= UINT_MAX) {
            LOG_ERROR("COCOTB_ATTACH only needs to be set to ~30 seconds");
            return -1;
        }
        if ((errno != 0 && sleep_time == 0) || (sleep_time <= 0)) {
            LOG_ERROR(
                "COCOTB_ATTACH must be set to an integer base 10 or omitted");
            return -1;
        }
        // LCOV_EXCL_STOP

        LOG_INFO(
            "Waiting for %lu seconds - attach to PID %d with your debugger",
            sleep_time, getpid());
        sleep((unsigned int)sleep_time);
    }

    return 0;
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
        Py_XDECREF(pEventFn);
        pEventFn = NULL;
        py_gpi_logger_finalize();
        Py_Finalize();
        to_simulator();
    }
}

extern "C" COCOTB_EXPORT void _embed_sim_init(void) {
    // Check that we are not already initialized
    // LCOV_EXCL_START
    if (embed_init_called) {
        LOG_ERROR("PyGPI library initialized again! Ignoring.");
        return;
    }
    // LCOV_EXCL_STOP
    embed_init_called = 1;

    // Ensure that the current thread is ready to call the Python C API
    auto gstate = PyGILState_Ensure();
    DEFER(PyGILState_Release(gstate));

    to_python();
    DEFER(to_simulator());

    auto entry_utility_module = PyImport_ImportModule("pygpi.entry");
    // LCOV_EXCL_START
    if (!entry_utility_module) {
        PyErr_Print();
        gpi_end_sim();
        return;
    }
    DEFER(Py_DECREF(entry_utility_module));
    // LCOV_EXCL_STOP

    // Build argv for cocotb module
    auto argv_list = PyList_New(m_argc);
    // LCOV_EXCL_START
    if (argv_list == NULL) {
        PyErr_Print();
        gpi_end_sim();
        return;
    }
    // LCOV_EXCL_STOP
    DEFER(Py_DECREF(argv_list));

    for (int i = 0; i < m_argc; i++) {
        // Decode, embedding non-decodable bytes using PEP-383. This can only
        // fail with MemoryError or similar.
        auto argv_item = PyUnicode_DecodeLocale(m_argv[i], "surrogateescape");
        // LCOV_EXCL_START
        if (!argv_item) {
            PyErr_Print();
            gpi_end_sim();
            return;
        }
        // LCOV_EXCL_STOP
        PyList_SetItem(argv_list, i, argv_item);
    }

    auto cocotb_retval =
        PyObject_CallMethod(entry_utility_module, "load_entry", "O", argv_list);
    // LCOV_EXCL_START
    if (!cocotb_retval) {
        PyErr_Print();
        gpi_end_sim();
        return;
    }
    // LCOV_EXCL_STOP
    Py_DECREF(cocotb_retval);
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
        // LCOV_EXCL_START
        if (pValue == NULL) {
            PyErr_Print();
            LOG_ERROR("Passing event to upper layer failed");
        }
        // LCOV_EXCL_STOP
        Py_XDECREF(pValue);
        PyGILState_Release(gstate);
        to_simulator();
    }
}
