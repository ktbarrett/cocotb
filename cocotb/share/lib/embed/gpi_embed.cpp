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
#include <vector>            // std::vector
#include <string>            // std::string
#include <cocotb_utils.h>    // DEFER
#include <exports.h>         // COCOTB_EXPORT
#include <gpi.h>             // gpi_event_t
#include <gpi_logging.h>     // LOG_* macros
#include <py_gpi_logging.h>  // py_gpi_logger_set_level, py_gpi_logger_initialize, py_gpi_logger_finalize

#include <cassert>

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

static PyThreadState *gtstate = NULL;

#if defined(_WIN32)
#if defined(__MINGW32__) || defined(__CYGWIN32__)
constexpr char *PYTHON_INTERPRETER_PATH = "/Scripts/python";
#else
constexpr char *PYTHON_INTERPRETER_PATH = "\\Scripts\\python";
#endif
#else
constexpr char *PYTHON_INTERPRETER_PATH = "/bin/python";
#endif

static PyObject *pEventFn = NULL;

static void set_program_name(void) {

    const char *venv_path_home = getenv("VIRTUAL_ENV");
    if (!venv_path_home) {
        LOG_INFO(
            "Did not detect Python virtual environment. "
            "Using system-wide Python interpreter");
        return;
    }

    std::string venv_path = venv_path_home;
    venv_path.append(PYTHON_INTERPRETER_PATH);

    auto venv_path_w = Py_DecodeLocale(venv_path.c_str(), NULL);
    if (venv_path_w == NULL) {
        LOG_ERROR(
            "Unable to set Python Program Name using virtual environment. "
            "Virtual environment path decoding error."
        )
    }

    LOG_INFO("Using Python virtual environment interpreter at %ls",
             venv_path_w);
    Py_SetProgramName(venv_path_w);
    PyMem_RawFree(venv_path_w);
}

static void wait_for_attach(void)
{
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


extern "C" COCOTB_EXPORT int _embed_init_python(int argc, char const * const *argv) {
    assert(!gtstate);  // this function should not be called twice

    // we need to set the program name before initialization so Py_Initialize can set up the path based on the executable
    set_program_name();
    Py_Initialize();

    // set argv to the command argv once we have configured the interpreter
    std::vector<wchar_t*> wchar_argv;
    for (int i = 0; i < argc; i++) {
        auto arg = Py_DecodeLocale(argv[i], NULL);
        if (arg == NULL) {
            return -1;
        }
        wchar_argv.push_back(arg);
    }
    PySys_SetArgvEx(wchar_argv.size(), wchar_argv.data(), 0);
    for (auto arg : wchar_argv) {
        PyMem_RawFree(arg);
    }

    // Swap out and return current thread state and release the GIL
    gtstate = PyEval_SaveThread();

    wait_for_attach();

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
        Py_DecRef(pEventFn);
        pEventFn = NULL;
        py_gpi_logger_finalize();
        Py_Finalize();
        to_simulator();
    }
}

extern "C" COCOTB_EXPORT int _embed_sim_init(void) {
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

    auto cocotb_retval = PyObject_CallFunction(entry_point, NULL);
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
