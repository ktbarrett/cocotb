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
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL POTENTIAL VENTURES LTD BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

// Embed Python into the simulator using GPI

#include <Python.h>
#include <unistd.h>
#include <cocotb_utils.h>
#include "gpi.h"
#include "locale.h"

#if defined(_WIN32)
#include <windows.h>
#define sleep(n) Sleep(1000 * n)
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#endif
static PyThreadState *gtstate = NULL;

static wchar_t progname[] = L"cocotb";
static wchar_t *argv[] = { progname };

#if defined(_WIN32)
#if defined(__MINGW32__) || defined (__CYGWIN32__)
const char* PYTHON_INTERPRETER_PATH = "/Scripts/python";
#else
const char* PYTHON_INTERPRETER_PATH = "\\Scripts\\python";
#endif
#else
const char* PYTHON_INTERPRETER_PATH = "/bin/python";
#endif


static PyObject *pEventFn = NULL;
static PyObject *pLogHandler = NULL;
static PyObject *pLogFilter = NULL;


static void set_program_name_in_venv(void)
{
    static char venv_path[PATH_MAX];
    static wchar_t venv_path_w[PATH_MAX];

    const char *venv_path_home = getenv("VIRTUAL_ENV");
    if (!venv_path_home) {
        LOG_INFO("Did not detect Python virtual environment. Using system-wide Python interpreter");
        return;
    }

    strncpy(venv_path, venv_path_home, sizeof(venv_path)-1);
    if (venv_path[sizeof(venv_path) - 1]) {
        LOG_ERROR("Unable to set Python Program Name using virtual environment. Path to virtual environment too long");
        return;
    }

    strncat(venv_path, PYTHON_INTERPRETER_PATH, sizeof(venv_path) - strlen(venv_path) - 1);
    if (venv_path[sizeof(venv_path) - 1]) {
        LOG_ERROR("Unable to set Python Program Name using virtual environment. Path to interpreter too long");
        return;
    }

    wcsncpy(venv_path_w, Py_DecodeLocale(venv_path, NULL), sizeof(venv_path_w)/sizeof(wchar_t));

    if (venv_path_w[(sizeof(venv_path_w)/sizeof(wchar_t)) - 1]) {
        LOG_ERROR("Unable to set Python Program Name using virtual environment. Path to interpreter too long");
        return;
    }

    LOG_INFO("Using Python virtual environment interpreter at %ls", venv_path_w);
    Py_SetProgramName(venv_path_w);
}


/**
 * @name    Initialize the Python interpreter
 * @brief   Create and initialize the Python interpreter
 * @ingroup python_c_api
 *
 * GILState before calling: N/A
 *
 * GILState after calling: released
 *
 * Stores the thread state for cocotb in static variable gtstate
 */

void embed_init_python(void)
{

#ifndef PYTHON_SO_LIB
#error "Python version needs passing in with -DPYTHON_SO_VERSION=libpython<ver>.so"
#else
#define PY_SO_LIB xstr(PYTHON_SO_LIB)
#endif

    // Don't initialize Python if already running
    if (gtstate)
        return;

    void * lib_handle = utils_dyn_open(PY_SO_LIB);
    if (!lib_handle) {
        LOG_ERROR("Failed to find Python shared library\n");
    }

    to_python();
    set_program_name_in_venv();
    Py_Initialize();                    /* Initialize the interpreter */
    PySys_SetArgvEx(1, argv, 0);
    PyEval_InitThreads();               /* Create (and acquire) the interpreter lock */

    /* Swap out and return current thread state and release the GIL */
    gtstate = PyEval_SaveThread();
    to_simulator();

    /* Before returning we check if the user wants pause the simulator thread
       such that they can attach */
    const char *pause = getenv("COCOTB_ATTACH");
    if (pause) {
        unsigned long sleep_time = strtoul(pause, NULL, 10);
        /* This should check for out-of-range parses which returns ULONG_MAX and sets errno,
           as well as correct parses that would be sliced by the narrowing cast */
        if (errno == ERANGE || sleep_time >= UINT_MAX) {
            LOG_ERROR("COCOTB_ATTACH only needs to be set to ~30 seconds");
            return;
        }
        if ((errno != 0 && sleep_time == 0) ||
            (sleep_time <= 0)) {
            LOG_ERROR("COCOTB_ATTACH must be set to an integer base 10 or omitted");
            return;
        }

        LOG_ERROR("Waiting for %lu seconds - attach to PID %d with your debugger\n", sleep_time, getpid());
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
void embed_sim_cleanup(void* userdata)
{
    (void)userdata;

    // If initialization fails, this may be called twice:
    // Before the initial callback returns and in the final callback.
    // So we check if Python is still initialized before doing cleanup.
    if (Py_IsInitialized()) {
        to_python();
        PyGILState_Ensure();    // Don't save state as we are calling Py_Finalize
        Py_DecRef(pEventFn);
        pEventFn = NULL;
        Py_XDECREF(pLogHandler);
        pLogHandler = NULL;
        Py_XDECREF(pLogFilter);
        pLogFilter = NULL;
        Py_Finalize();
        to_simulator();
    }
}

/**
 * @name    Initialization
 * @brief   Called by the simulator on initialization. Load cocotb Python module
 * @ingroup python_c_api
 *
 * GILState before calling: Not held
 *
 * GILState after calling: Not held
 *
 * Makes one call to PyGILState_Ensure and one call to PyGILState_Release
 *
 * Loads the Python module called cocotb and calls the _initialise_testbench function
 */

int get_module_ref(const char *modname, PyObject **mod)
{
    PyObject *pModule = PyImport_ImportModule(modname);

    if (pModule == NULL) {
        PyErr_Print();
        LOG_ERROR("Failed to load Python module \"%s\"\n", modname);
        return -1;
    }

    *mod = pModule;
    return 0;
}


static void embed_sim_event(void*, gpi_event_t, const char*);

static void cocotb_log_v(
    void* userdata,
    const char *name,
    enum gpi_log_levels level,
    const char *pathname,
    const char *funcname,
    long lineno,
    const char *msg,
    va_list argp);

int embed_sim_init(int argc, char const* const* argv)
{
    gpi_register_sim_event_callback(embed_sim_event, NULL);
    gpi_register_sim_end_callback(embed_sim_cleanup, NULL);

    embed_init_python();

    int i;
    int ret = 0;

    /* Check that we are not already initialized */
    if (pEventFn)
        return ret;

    PyObject *cocotb_module = NULL;
    PyObject *cocotb_gpi_module = NULL;
    PyObject *simlog_func;
    PyObject *entry_tuple = NULL;
    PyObject *cocotb_init = NULL;
    PyObject *argv_list = NULL;
    PyObject *cocotb_retval = NULL;

    // Ensure that the current thread is ready to call the Python C API
    PyGILState_STATE gstate = PyGILState_Ensure();
    to_python();

    if (get_module_ref("cocotb._gpi_embed", &cocotb_gpi_module)) {
        goto cleanup;
    }

    // Obtain the function to use when logging from C code
    simlog_func = PyObject_GetAttrString(cocotb_gpi_module, "_log_from_c");      // New reference
    if (simlog_func == NULL) {
        PyErr_Print();
        LOG_ERROR("Failed to get the _log_from_c function");
        goto cleanup;
    }

    pLogHandler = simlog_func;                                          // Note: This global variable holds a reference to Python log handler

    // Obtain the function to check whether to call log function
    simlog_func = PyObject_GetAttrString(cocotb_gpi_module, "_filter_from_c");   // New reference
    if (simlog_func == NULL) {
        PyErr_Print();
        LOG_ERROR("Failed to get the _filter_from_c method");
        goto cleanup;
    }

    pLogFilter = simlog_func;                                           // Note: This global variable holds a reference to Python log filter

    gpi_set_log_handler(cocotb_log_v, NULL);

    entry_tuple = PyObject_CallMethod(cocotb_gpi_module, "_load_entry", NULL);
    if (entry_tuple == NULL) {
        PyErr_Print();
        LOG_ERROR("Unable to load entry point");
        goto cleanup;
    }
    if (!PyArg_ParseTuple(entry_tuple, "OOO", &cocotb_module, &cocotb_init, &pEventFn)) {
        PyErr_Print();
        LOG_ERROR("Bad output from cocotb._gpi_embed._load_entry: should return (entry module, entry function, sim event callback)");
        goto cleanup;
    }
    Py_INCREF(cocotb_module);
    Py_INCREF(cocotb_init);
    Py_INCREF(pEventFn);                                                // ParseTuple steals a reference, we need this to stick around...

    // Build argv for cocotb module
    argv_list = PyList_New(argc);                                       // New reference
    if (argv_list == NULL) {
        PyErr_Print();
        LOG_ERROR("Unable to create argv list");
        goto cleanup;
    }
    for (i = 0; i < argc; i++) {
        // Decode, embedding non-decodable bytes using PEP-383. This can only
        // fail with MemoryError or similar.
        PyObject *argv_item = PyUnicode_DecodeLocale(argv[i], "surrogateescape");   // New reference
        if (argv_item == NULL) {
            PyErr_Print();
            LOG_ERROR("Unable to convert command line argument %d to Unicode string.", i);
            goto cleanup;
        }
        PyList_SET_ITEM(argv_list, i, argv_item);                       // Note: This function steals the reference to argv_item
    }

    cocotb_retval = PyObject_CallFunctionObjArgs(cocotb_init, argv_list, NULL);
    if (cocotb_retval != NULL) {
        LOG_DEBUG("_initialise_testbench successful");
    } else {
        PyErr_Print();
        LOG_ERROR("cocotb initialization failed - exiting");
        goto cleanup;
    }

    goto ok;

cleanup:
    ret = -1;
ok:
    Py_XDECREF(cocotb_module);
    Py_XDECREF(cocotb_gpi_module);
    Py_XDECREF(entry_tuple);
    Py_XDECREF(cocotb_init);
    Py_XDECREF(argv_list);
    Py_XDECREF(cocotb_retval);

    PyGILState_Release(gstate);
    to_simulator();

    return ret;
}

void embed_sim_event(void* userdata, gpi_event_t level, const char *msg)
{
    /* Indicate to the upper layer a sim event occurred */

    (void)userdata;
    (void)msg;

    if (pEventFn) {
        PyGILState_STATE gstate;
        to_python();
        gstate = PyGILState_Ensure();

        if (msg == NULL) {
            msg = "No message provided";
        }

        PyObject* pValue = PyObject_CallFunction(pEventFn, "ls", level, msg);
        if (pValue == NULL) {
            PyErr_Print();
            LOG_ERROR("Passing event to upper layer failed");
        }
        Py_XDECREF(pValue);
        PyGILState_Release(gstate);
        to_simulator();
    }
}

/**
 * @name    Cocotb logging
 * @brief   Write a log message using cocotb SimLog class
 * @ingroup python_c_api
 *
 * GILState before calling: Unknown
 *
 * GILState after calling: Unknown
 *
 * Makes one call to PyGILState_Ensure and one call to PyGILState_Release
 *
 * If the Python logging mechanism is not initialised, dumps to `stderr`.
 *
 */
static void cocotb_log_v(
    void* userdata,
    const char *name,
    enum gpi_log_levels level,
    const char *pathname,
    const char *funcname,
    long lineno,
    const char *msg,
    va_list argp)
{
    (void)userdata;

    #ifndef COCOTB_LOG_SIZE
    #define COCOTB_LOG_SIZE 512
    #endif
    static char log_buff[COCOTB_LOG_SIZE];

    /* We first check that the log level means this will be printed
     * before going to the expense of formatting the variable arguments
     */
    if (!pLogHandler) {
        gpi_default_logger_handler(gpi_default_logger_userdata, name, level, pathname, funcname, lineno, msg, argp);
        return;
    }

    PyGILState_STATE gstate = PyGILState_Ensure();

    // Declared here in order to be initialized before any goto statements and refcount cleanup
    PyObject *logger_name_arg = NULL, *filename_arg = NULL, *lineno_arg = NULL, *msg_arg = NULL, *function_arg = NULL;

    PyObject *level_arg = PyLong_FromLong(level);                  // New reference
    if (level_arg == NULL) {
        goto error;
    }

    logger_name_arg = PyUnicode_FromString(name);      // New reference
    if (logger_name_arg == NULL) {
        goto error;
    }

    PyObject *filter_ret = PyObject_CallFunctionObjArgs(pLogFilter, logger_name_arg, level_arg, NULL);
    if (filter_ret == NULL) {
        goto error;
    }

    int is_enabled = PyObject_IsTrue(filter_ret);
    Py_DECREF(filter_ret);
    if (is_enabled < 0) {
        /* A python exception occured while converting `filter_ret` to bool */
        goto error;
    }

    if (!is_enabled) {
        goto ok;
    }

    // Ignore truncation
    {
        int n = vsnprintf(log_buff, COCOTB_LOG_SIZE, msg, argp);
        if (n < 0 || n >= COCOTB_LOG_SIZE) {
            fprintf(stderr, "Log message construction failed\n");
        }
    }

    filename_arg = PyUnicode_FromString(pathname);      // New reference
    if (filename_arg == NULL) {
        goto error;
    }

    lineno_arg = PyLong_FromLong(lineno);               // New reference
    if (lineno_arg == NULL) {
        goto error;
    }

    msg_arg = PyUnicode_FromString(log_buff);           // New reference
    if (msg_arg == NULL) {
        goto error;
    }

    function_arg = PyUnicode_FromString(funcname);      // New reference
    if (function_arg == NULL) {
        goto error;
    }

    // Log function args are logger_name, level, filename, lineno, msg, function
    PyObject *handler_ret = PyObject_CallFunctionObjArgs(pLogHandler, logger_name_arg, level_arg, filename_arg, lineno_arg, msg_arg, function_arg, NULL);
    if (handler_ret == NULL) {
        goto error;
    }
    Py_DECREF(handler_ret);

    goto ok;
error:
    /* Note: don't call the LOG_ERROR macro because that might recurse */
    gpi_default_logger_handler(gpi_default_logger_userdata, name, level, pathname, funcname, lineno, msg, argp);
    gpi_default_logger_log("cocotb.gpi", GPIError, __FILE__, __func__, __LINE__, "Error calling Python logging function from C while logging the above");
    PyErr_Print();
ok:
    Py_XDECREF(logger_name_arg);
    Py_XDECREF(level_arg);
    Py_XDECREF(filename_arg);
    Py_XDECREF(lineno_arg);
    Py_XDECREF(msg_arg);
    Py_XDECREF(function_arg);
    PyGILState_Release(gstate);
}
