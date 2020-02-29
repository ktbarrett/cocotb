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

/**
* @file   simulatormodule.c
* @brief Python extension to provide access to the simulator
*
* Uses GPI calls to interface to the simulator.
*/

#include <Python.h>
#include "gpi.h"
#include <cocotb_utils.h>     // COCOTB_UNUSED

static int takes = 0;
static int releases = 0;

static int sim_ending = 0;

struct module_state {
    PyObject *error;
};

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

typedef int (*gpi_function_t)(const void *);

PyGILState_STATE TAKE_GIL(void)
{
    PyGILState_STATE state = PyGILState_Ensure();
    takes ++;
    return state;
}

void DROP_GIL(PyGILState_STATE state)
{
    PyGILState_Release(state);
    releases++;
}

struct sim_time {
    uint32_t high;
    uint32_t low;
};

static struct sim_time cache_time;

#define COCOTB_ACTIVE_ID        0xC0C07B        // User data flag to indicate callback is active
#define COCOTB_INACTIVE_ID      0xDEADB175      // User data flag set when callback has been de-registered

#define MODULE_NAME "simulator"

// callback user data
typedef struct t_callback_data {
    PyThreadState *_saved_thread_state; // Thread state of the calling thread FIXME is this required?
    uint32_t id_value;                  // COCOTB_ACTIVE_ID or COCOTB_INACTIVE_ID
    PyObject *function;                 // Function to call when the callback fires
    PyObject *args;                     // The arguments to call the function with
    PyObject *kwargs;                   // Keyword arguments to call the function with
    gpi_sim_hdl cb_hdl;
} s_callback_data, *p_callback_data;

// Converter function for turning a Python long into a sim handle, such that it
// can be used by PyArg_ParseTuple format O&.
static int gpi_sim_hdl_converter(PyObject *o, gpi_sim_hdl *data)
{
    void *p = PyLong_AsVoidPtr(o);
    if ((p == NULL) && PyErr_Occurred()) {
        return 0;
    }
    if (p == NULL) {
        PyErr_SetString(PyExc_ValueError, "handle cannot be 0");
        return 0;
    }
    *data = (gpi_sim_hdl)p;
    return 1;
}

// Same as above, for an iterator handle.
static int gpi_iterator_hdl_converter(PyObject *o, gpi_iterator_hdl *data)
{
    void *p = PyLong_AsVoidPtr(o);
    if ((p == NULL) && PyErr_Occurred()) {
        return 0;
    }
    if (p == NULL) {
        PyErr_SetString(PyExc_ValueError, "handle cannot be 0");
        return 0;
    }
    *data = (gpi_iterator_hdl)p;
    return 1;
}

/**
 * @name    Callback Handling
 * @brief   Handle a callback coming from GPI
 * @ingroup python_c_api
 *
 * GILState before calling: Unknown
 *
 * GILState after calling: Unknown
 *
 * Makes one call to TAKE_GIL and one call to DROP_GIL
 *
 * Returns 0 on success or 1 on a failure.
 *
 * Handles a callback from the simulator, all of which call this function.
 *
 * We extract the associated context and find the Python function (usually
 * cocotb.scheduler.react) calling it with a reference to the trigger that
 * fired. The scheduler can then call next() on all the coroutines that
 * are waiting on that particular trigger.
 *
 * TODO:
 *  - Tidy up return values
 *  - Ensure cleanup correctly in exception cases
 *
 */
int handle_gpi_callback(void *user_data)
{
    int ret = 0;
    to_python();
    p_callback_data callback_data_p = (p_callback_data)user_data;

    if (callback_data_p->id_value != COCOTB_ACTIVE_ID) {
        fprintf(stderr, "Userdata corrupted!\n");
        ret = 1;
        goto err;
    }
    callback_data_p->id_value = COCOTB_INACTIVE_ID;

    /* Cache the sim time */
    gpi_get_sim_time(&cache_time.high, &cache_time.low);

    PyGILState_STATE gstate;
    gstate = TAKE_GIL();

    // Python allowed

    if (!PyCallable_Check(callback_data_p->function)) {
        fprintf(stderr, "Callback fired but function isn't callable?!\n");
        ret = 1;
        goto out;
    }

    // Call the callback
    PyObject *pValue = PyObject_Call(callback_data_p->function, callback_data_p->args, callback_data_p->kwargs);

    // If the return value is NULL a Python exception has occurred
    // The best thing to do here is shutdown as any subsequent
    // calls will go back to Python which is now in an unknown state
    if (pValue == NULL)
    {
        fprintf(stderr, "ERROR: called callback function returned NULL\n");
        if (PyErr_Occurred()) {
            fprintf(stderr, "Failed to execute callback due to Python exception\n");
            PyErr_Print();
        } else {
            fprintf(stderr, "Failed to execute callback\n");
        }

        gpi_sim_end();
        sim_ending = 1;
        ret = 0;
        goto out;
    }

    // Free up our mess
    Py_DECREF(pValue);

    // Callbacks may have been re-enabled
    if (callback_data_p->id_value == COCOTB_INACTIVE_ID) {
        Py_DECREF(callback_data_p->function);
        Py_DECREF(callback_data_p->args);

        // Free the callback data
        free(callback_data_p);
    }

out:
    DROP_GIL(gstate);

err:
    to_simulator();

    if (sim_ending) {
        // This is the last callback of a successful run,
        // so call the cleanup function as we'll never return
        // to Python
        gpi_cleanup();
    }
    return ret;
}


// Register a callback for read-only state of sim
// First argument is the function to call
// Remaining arguments are keyword arguments to be passed to the callback
static PyObject *register_readonly_callback(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);

    PyObject *fArgs;
    PyObject *function;
    gpi_sim_hdl hdl;

    p_callback_data callback_data_p;

    Py_ssize_t numargs = PyTuple_Size(args);

    if (numargs < 1) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register ReadOnly callback without enough arguments!\n");
        return NULL;
    }

    // Extract the callback function
    function = PyTuple_GetItem(args, 0);
    if (!PyCallable_Check(function)) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register ReadOnly without supplying a callback!\n");
        return NULL;
    }
    Py_INCREF(function);

    // Remaining args for function
    fArgs = PyTuple_GetSlice(args, 1, numargs);   // New reference
    if (fArgs == NULL) {
        return NULL;
    }

    callback_data_p = (p_callback_data)malloc(sizeof(s_callback_data));
    if (callback_data_p == NULL) {
        return PyErr_NoMemory();
    }

    // Set up the user data (no more Python API calls after this!)
    callback_data_p->_saved_thread_state = PyThreadState_Get();
    callback_data_p->id_value = COCOTB_ACTIVE_ID;
    callback_data_p->function = function;
    callback_data_p->args = fArgs;
    callback_data_p->kwargs = NULL;

    hdl = gpi_register_readonly_callback((gpi_function_t)handle_gpi_callback, callback_data_p);

    PyObject *rv = PyLong_FromVoidPtr(hdl);

    return rv;
}


static PyObject *register_rwsynch_callback(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);

    PyObject *fArgs;
    PyObject *function;
    gpi_sim_hdl hdl;

    p_callback_data callback_data_p;

    Py_ssize_t numargs = PyTuple_Size(args);

    if (numargs < 1) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register ReadWrite callback without enough arguments!\n");
        return NULL;
    }

    // Extract the callback function
    function = PyTuple_GetItem(args, 0);
    if (!PyCallable_Check(function)) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register ReadWrite without supplying a callback!\n");
        return NULL;
    }
    Py_INCREF(function);

    // Remaining args for function
    fArgs = PyTuple_GetSlice(args, 1, numargs);   // New reference
    if (fArgs == NULL) {
        return NULL;
    }

    callback_data_p = (p_callback_data)malloc(sizeof(s_callback_data));
    if (callback_data_p == NULL) {
        return PyErr_NoMemory();
    }

    // Set up the user data (no more Python API calls after this!)
    callback_data_p->_saved_thread_state = PyThreadState_Get();
    callback_data_p->id_value = COCOTB_ACTIVE_ID;
    callback_data_p->function = function;
    callback_data_p->args = fArgs;
    callback_data_p->kwargs = NULL;

    hdl = gpi_register_readwrite_callback((gpi_function_t)handle_gpi_callback, callback_data_p);

    PyObject *rv = PyLong_FromVoidPtr(hdl);

    return rv;
}


static PyObject *register_nextstep_callback(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);

    PyObject *fArgs;
    PyObject *function;
    gpi_sim_hdl hdl;

    p_callback_data callback_data_p;

    Py_ssize_t numargs = PyTuple_Size(args);

    if (numargs < 1) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register NextStep callback without enough arguments!\n");
        return NULL;
    }

    // Extract the callback function
    function = PyTuple_GetItem(args, 0);
    if (!PyCallable_Check(function)) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register NextStep without supplying a callback!\n");
        return NULL;
    }
    Py_INCREF(function);

    // Remaining args for function
    fArgs = PyTuple_GetSlice(args, 1, numargs);   // New reference
    if (fArgs == NULL) {
        return NULL;
    }

    callback_data_p = (p_callback_data)malloc(sizeof(s_callback_data));
    if (callback_data_p == NULL) {
        return PyErr_NoMemory();
    }

    // Set up the user data (no more Python API calls after this!)
    callback_data_p->_saved_thread_state = PyThreadState_Get();
    callback_data_p->id_value = COCOTB_ACTIVE_ID;
    callback_data_p->function = function;
    callback_data_p->args = fArgs;
    callback_data_p->kwargs = NULL;

    hdl = gpi_register_nexttime_callback((gpi_function_t)handle_gpi_callback, callback_data_p);

    PyObject *rv = PyLong_FromVoidPtr(hdl);

    return rv;
}


// Register a timed callback.
// First argument should be the time in picoseconds
// Second argument is the function to call
// Remaining arguments and keyword arguments are to be passed to the callback
static PyObject *register_timed_callback(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);

    PyObject *fArgs;
    PyObject *function;
    gpi_sim_hdl hdl;
    uint64_t time_ps;

    p_callback_data callback_data_p;

    Py_ssize_t numargs = PyTuple_Size(args);

    if (numargs < 2) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register timed callback without enough arguments!\n");
        return NULL;
    }

    {   // Extract the time
        PyObject *pTime = PyTuple_GetItem(args, 0);
        long long pTime_as_longlong = PyLong_AsLongLong(pTime);
        if (pTime_as_longlong == -1 && PyErr_Occurred()) {
            return NULL;
        } else if (pTime_as_longlong < 0) {
            PyErr_SetString(PyExc_ValueError, "Timer value must be a positive integer");
            return NULL;
        } else {
            time_ps = (uint64_t)pTime_as_longlong;
        }
    }

    // Extract the callback function
    function = PyTuple_GetItem(args, 1);
    if (!PyCallable_Check(function)) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register timed callback without passing a callable callback!\n");
        return NULL;
    }
    Py_INCREF(function);

    // Remaining args for function
    fArgs = PyTuple_GetSlice(args, 2, numargs);   // New reference
    if (fArgs == NULL) {
        return NULL;
    }

    callback_data_p = (p_callback_data)malloc(sizeof(s_callback_data));
    if (callback_data_p == NULL) {
        return PyErr_NoMemory();
    }

    // Set up the user data (no more Python API calls after this!)
    callback_data_p->_saved_thread_state = PyThreadState_Get();
    callback_data_p->id_value = COCOTB_ACTIVE_ID;
    callback_data_p->function = function;
    callback_data_p->args = fArgs;
    callback_data_p->kwargs = NULL;

    hdl = gpi_register_timed_callback((gpi_function_t)handle_gpi_callback, callback_data_p, time_ps);

    // Check success
    PyObject *rv = PyLong_FromVoidPtr(hdl);

    return rv;
}


// Register signal change callback
// First argument should be the signal handle
// Second argument is the function to call
// Remaining arguments and keyword arguments are to be passed to the callback
static PyObject *register_value_change_callback(PyObject *self, PyObject *args) //, PyObject *keywds)
{
    COCOTB_UNUSED(self);

    PyObject *fArgs;
    PyObject *function;
    gpi_sim_hdl sig_hdl;
    gpi_sim_hdl hdl;
    int edge;

    p_callback_data callback_data_p;

    Py_ssize_t numargs = PyTuple_Size(args);

    if (numargs < 3) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register value change callback without enough arguments!\n");
        return NULL;
    }

    PyObject *pSihHdl = PyTuple_GetItem(args, 0);
    if (!gpi_sim_hdl_converter(pSihHdl, &sig_hdl)) {
        return NULL;
    }

    // Extract the callback function
    function = PyTuple_GetItem(args, 1);
    if (!PyCallable_Check(function)) {
        PyErr_SetString(PyExc_TypeError, "Attempt to register value change callback without passing a callable callback!\n");
        return NULL;
    }
    Py_INCREF(function);

    PyObject *pedge = PyTuple_GetItem(args, 2);
    edge = (int)PyLong_AsLong(pedge);

    // Remaining args for function
    fArgs = PyTuple_GetSlice(args, 3, numargs);   // New reference
    if (fArgs == NULL) {
        return NULL;
    }


    callback_data_p = (p_callback_data)malloc(sizeof(s_callback_data));
    if (callback_data_p == NULL) {
        return PyErr_NoMemory();
    }

    // Set up the user data (no more Python API calls after this!)
    // Causes segfault?
    callback_data_p->_saved_thread_state = PyThreadState_Get();//PyThreadState_Get();
    callback_data_p->id_value = COCOTB_ACTIVE_ID;
    callback_data_p->function = function;
    callback_data_p->args = fArgs;
    callback_data_p->kwargs = NULL;

    hdl = gpi_register_value_change_callback((gpi_function_t)handle_gpi_callback,
                                             callback_data_p,
                                             sig_hdl,
                                             edge);

    // Check success
    PyObject *rv = PyLong_FromVoidPtr(hdl);

    return rv;
}


static PyObject *iterate(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    int type;
    gpi_iterator_hdl result;
    PyObject *res;

    if (!PyArg_ParseTuple(args, "O&i", gpi_sim_hdl_converter, &hdl, &type)) {
        return NULL;
    }

    result = gpi_iterate(hdl, (gpi_iterator_sel_t)type);

    res = PyLong_FromVoidPtr(result);

    return res;
}


static PyObject *next(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_iterator_hdl hdl;
    gpi_sim_hdl result;
    PyObject *res;

    if (!PyArg_ParseTuple(args, "O&", gpi_iterator_hdl_converter, &hdl)) {
        return NULL;
    }

    // It's valid for iterate to return a NULL handle, to make the Python
    // intuitive we simply raise StopIteration on the first iteration
    if (!hdl) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    result = gpi_next(hdl);

    // Raise StopIteration when we're done
    if (!result) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    res = PyLong_FromVoidPtr(result);

    return res;
}


static PyObject *get_signal_val_binstr(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    const char *result;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_value_binstr(hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}

static PyObject *get_signal_val_str(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    const char *result;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_value_str(hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}

static PyObject *get_signal_val_real(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    double result;
    PyObject *retval;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_value_real(hdl);
    retval = Py_BuildValue("d", result);

    return retval;
}


static PyObject *get_signal_val_long(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    long result;
    PyObject *retval;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_value_long(hdl);
    retval = Py_BuildValue("l", result);

    return retval;
}

static PyObject *set_signal_val_binstr(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    const char *binstr;
    gpi_set_action_t action;

    if (!PyArg_ParseTuple(args, "O&is", gpi_sim_hdl_converter, &hdl, &action, &binstr)) {
        return NULL;
    }

    gpi_set_signal_value_binstr(hdl, binstr, action);
    Py_RETURN_NONE;
}

static PyObject *set_signal_val_str(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    gpi_set_action_t action;
    const char *str;

    if (!PyArg_ParseTuple(args, "O&is", gpi_sim_hdl_converter, &hdl, &action, &str)) {
        return NULL;
    }

    gpi_set_signal_value_str(hdl, str, action);
    Py_RETURN_NONE;
}

static PyObject *set_signal_val_real(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    double value;
    gpi_set_action_t action;

    if (!PyArg_ParseTuple(args, "O&id", gpi_sim_hdl_converter, &hdl, &action, &value)) {
        return NULL;
    }

    gpi_set_signal_value_real(hdl, value, action);
    Py_RETURN_NONE;
}

static PyObject *set_signal_val_long(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    long value;
    gpi_set_action_t action;

    if (!PyArg_ParseTuple(args, "O&il", gpi_sim_hdl_converter, &hdl, &action, &value)) {
        return NULL;
    }

    gpi_set_signal_value_long(hdl, value, action);
    Py_RETURN_NONE;
}

static PyObject *get_definition_name(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char* result;
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_definition_name((gpi_sim_hdl)hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}

static PyObject *get_definition_file(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char* result;
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_definition_file((gpi_sim_hdl)hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}

static PyObject *get_handle_by_name(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *name;
    gpi_sim_hdl hdl;
    gpi_sim_hdl result;
    PyObject *res;

    if (!PyArg_ParseTuple(args, "O&s", gpi_sim_hdl_converter, &hdl, &name)) {
        return NULL;
    }

    result = gpi_get_handle_by_name((gpi_sim_hdl)hdl, name);

    res = PyLong_FromVoidPtr(result);

    return res;
}

static PyObject *get_handle_by_index(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    int32_t index;
    gpi_sim_hdl hdl;
    gpi_sim_hdl result;
    PyObject *value;

    if (!PyArg_ParseTuple(args, "O&i", gpi_sim_hdl_converter, &hdl, &index)) {
        return NULL;
    }

    result = gpi_get_handle_by_index((gpi_sim_hdl)hdl, index);

    value = PyLong_FromVoidPtr(result);

    return value;
}

static PyObject *get_root_handle(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *name;
    gpi_sim_hdl result;
    PyObject *value;

    if (!PyArg_ParseTuple(args, "z", &name)) {
        return NULL;
    }

    result = gpi_get_root_handle(name);
    if (NULL == result) {
       Py_RETURN_NONE;
    }


    value = PyLong_FromVoidPtr(result);

    return value;
}


static PyObject *get_name_string(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *result;
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_name_str((gpi_sim_hdl)hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}

static PyObject *get_type(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_objtype_t result;
    gpi_sim_hdl hdl;
    PyObject *pyresult;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_object_type((gpi_sim_hdl)hdl);
    pyresult = Py_BuildValue("i", (int)result);

    return pyresult;
}

static PyObject *get_const(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    int result;
    gpi_sim_hdl hdl;
    PyObject *pyresult;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_is_constant((gpi_sim_hdl)hdl);
    pyresult = Py_BuildValue("i", result);

    return pyresult;
}

static PyObject *get_type_string(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *result;
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    result = gpi_get_signal_type_str((gpi_sim_hdl)hdl);
    retstr = Py_BuildValue("s", result);

    return retstr;
}


// Returns a high, low, tuple of simulator time
// Note we can never log from this function since the logging mechanism calls this to annotate
// log messages with the current simulation time
static PyObject *get_sim_time(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    COCOTB_UNUSED(args);
    struct sim_time local_time;

    if (is_python_context) {
        gpi_get_sim_time(&local_time.high, &local_time.low);
    } else {
        local_time = cache_time;
    }

    PyObject *pTuple = PyTuple_New(2);
    PyTuple_SetItem(pTuple, 0, PyLong_FromUnsignedLong(local_time.high));       // Note: This function “steals” a reference to o.
    PyTuple_SetItem(pTuple, 1, PyLong_FromUnsignedLong(local_time.low));       // Note: This function “steals” a reference to o.

    return pTuple;
}

static PyObject *get_precision(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    COCOTB_UNUSED(args);
    int32_t precision;

    gpi_get_sim_precision(&precision);

    PyObject *retint = Py_BuildValue("i", precision);


    return retint;
}

static PyObject *get_num_elems(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    int elems = gpi_get_num_elems((gpi_sim_hdl)hdl);
    retstr = Py_BuildValue("i", elems);

    return retstr;
}

static PyObject *get_range(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;
    PyObject *retstr;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    int indexable = gpi_is_indexable((gpi_sim_hdl)hdl);
    int rng_left  = gpi_get_range_left((gpi_sim_hdl)hdl);
    int rng_right = gpi_get_range_right((gpi_sim_hdl)hdl);

    if (indexable)
        retstr = Py_BuildValue("(i,i)", rng_left, rng_right);
    else
        retstr = Py_BuildValue("");

    return retstr;
}

static PyObject *stop_simulator(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    COCOTB_UNUSED(args);
    gpi_sim_end();
    sim_ending = 1;
    Py_RETURN_NONE;
}


static PyObject *deregister_callback(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    gpi_sim_hdl hdl;

    if (!PyArg_ParseTuple(args, "O&", gpi_sim_hdl_converter, &hdl)) {
        return NULL;
    }

    gpi_deregister_callback(hdl);

    Py_RETURN_NONE;
}

static void add_module_constants(PyObject* simulator)
{
    // Make the GPI constants accessible from the C world
    int rc = 0;
    rc |= PyModule_AddIntConstant(simulator, "UNKNOWN",       GPI_UNKNOWN);
    rc |= PyModule_AddIntConstant(simulator, "MEMORY",        GPI_MEMORY);
    rc |= PyModule_AddIntConstant(simulator, "MODULE",        GPI_MODULE);
    rc |= PyModule_AddIntConstant(simulator, "NET",           GPI_NET);
    rc |= PyModule_AddIntConstant(simulator, "PARAMETER",     GPI_PARAMETER);
    rc |= PyModule_AddIntConstant(simulator, "REG",           GPI_REGISTER);
    rc |= PyModule_AddIntConstant(simulator, "NETARRAY",      GPI_ARRAY);
    rc |= PyModule_AddIntConstant(simulator, "ENUM",          GPI_ENUM);
    rc |= PyModule_AddIntConstant(simulator, "STRUCTURE",     GPI_STRUCTURE);
    rc |= PyModule_AddIntConstant(simulator, "REAL",          GPI_REAL);
    rc |= PyModule_AddIntConstant(simulator, "INTEGER",       GPI_INTEGER);
    rc |= PyModule_AddIntConstant(simulator, "STRING",        GPI_STRING);
    rc |= PyModule_AddIntConstant(simulator, "GENARRAY",      GPI_GENARRAY);
    rc |= PyModule_AddIntConstant(simulator, "OBJECTS",       GPI_OBJECTS);
    rc |= PyModule_AddIntConstant(simulator, "DRIVERS",       GPI_DRIVERS);
    rc |= PyModule_AddIntConstant(simulator, "LOADS",         GPI_LOADS);

    if (rc != 0)
        fprintf(stderr, "Failed to add module constants!\n");
}

static PyObject *error_out(PyObject *m, PyObject *args)
{
    COCOTB_UNUSED(args);
    struct module_state *st = GETSTATE(m);
    PyErr_SetString(st->error, "something bad happened");
    return NULL;
}

static PyObject *get_simulator_product(PyObject *m, PyObject *args)
{
    COCOTB_UNUSED(m);
    COCOTB_UNUSED(args);
    const char *product_string = gpi_get_simulator_product();
    PyObject *result = Py_BuildValue("s", product_string);
    return result;
}

static PyObject *get_simulator_version(PyObject *m, PyObject *args)
{
    COCOTB_UNUSED(m);
    COCOTB_UNUSED(args);
    const char *version_string = gpi_get_simulator_version();
    PyObject *result = Py_BuildValue("s", version_string);
    return result;
}

static PyObject *log_msg_native(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *name;
    int level;
    const char *path;
    const char *funcname;
    int lineno;
    const char *msg;

    if (!PyArg_ParseTuple(args, "sissis", &name, &level, &path, &funcname, &lineno, &msg))
        return NULL;

    gpi_native_logger_log(name, level, path, funcname, lineno, msg);

    Py_RETURN_NONE;
}

static PyObject *set_log_level_native(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    int new_level;

    if (!PyArg_ParseTuple(args, "i", &new_level))
        return NULL;

    int old_level = gpi_native_logger_set_level(new_level);

    return Py_BuildValue("i", old_level);
}

static PyObject *log_msg(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    const char *name;
    int level;
    const char *path;
    const char *funcname;
    int lineno;
    const char *msg;

    if (!PyArg_ParseTuple(args, "sissis", &name, &level, &path, &funcname, &lineno, &msg))
        return NULL;

    gpi_log(name, level, path, funcname, lineno, msg);

    Py_RETURN_NONE;
}

/**
 * @name    GPI logging
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
static void cocotb_logger(void* python_log_func, const char *name, int level, const char *pathname, const char *funcname, long lineno, const char *msg, va_list argp)
{

    PyGILState_STATE gstate = PyGILState_Ensure();

    // Declared here in order to be initialized before any goto statements and refcount cleanup
    PyObject *logger_name_arg = NULL, *filename_arg = NULL, *lineno_arg = NULL, *msg_arg = NULL, *function_arg = NULL;

    PyObject *level_arg = PyLong_FromLong(level);                               // New reference
    if (level_arg == NULL) {
        goto error;
    }

    logger_name_arg = PyUnicode_FromString(name);                               // New reference
    if (logger_name_arg == NULL) {
        goto error;
    }

    filename_arg = PyUnicode_FromString(pathname);                              // New reference
    if (filename_arg == NULL) {
        goto error;
    }

    lineno_arg = PyLong_FromLong(lineno);                                       // New reference
    if (lineno_arg == NULL) {
        goto error;
    }

    #ifndef GPI_LOG_SIZE
    #define GPI_LOG_SIZE 512
    #endif
    static char log_buff[GPI_LOG_SIZE];
    int n = vsnprintf(log_buff, GPI_LOG_SIZE, msg, argp);
    if (n < 0 || n >= GPI_LOG_SIZE) {
        fprintf(stderr, "Log message construction failed\n");
    }

    msg_arg = PyUnicode_FromString(log_buff);                                   // New reference
    if (msg_arg == NULL) {
        goto error;
    }

    function_arg = PyUnicode_FromString(funcname);                              // New reference
    if (function_arg == NULL) {
        goto error;
    }

    // Log function args are logger_name, level, filename, lineno, msg, function
    PyObject *handler_ret = PyObject_CallFunctionObjArgs(python_log_func, logger_name_arg, level_arg, filename_arg, lineno_arg, msg_arg, function_arg, NULL);
    if (handler_ret == NULL) {
        goto error;
    }
    Py_DECREF(handler_ret);

    goto ok;
error:
    /* Note: don't call the LOG_ERROR macro because that might recurse */
    gpi_native_logger_log_v(name, level, pathname, funcname, lineno, msg, argp);
    gpi_native_logger_log("cocotb.gpi", GPIError, __FILE__, __func__, __LINE__, "Error calling Python logging function from C while logging the above");
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

static PyObject *python_log_func = NULL;

static PyObject *set_log_handler(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);

    if (!PyArg_ParseTuple(args, "O", &python_log_func))
        return NULL;
    Py_INCREF(python_log_func);

    gpi_set_log_handler(cocotb_logger, python_log_func);

    Py_RETURN_NONE;
}

static PyObject *clear_log_handler(PyObject *self, PyObject *args)
{
    COCOTB_UNUSED(self);
    COCOTB_UNUSED(args);

    gpi_clear_log_handler();
    Py_DECREF(python_log_func);

    Py_RETURN_NONE;
}

static int simulator_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int simulator_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static PyMethodDef SimulatorMethods[] = {
    {"get_signal_val_long", get_signal_val_long, METH_VARARGS, "Get the value of a signal as a long"},
    {"get_signal_val_str", get_signal_val_str, METH_VARARGS, "Get the value of a signal as an ASCII string"},
    {"get_signal_val_binstr", get_signal_val_binstr, METH_VARARGS, "Get the value of a signal as a binary string"},
    {"get_signal_val_real", get_signal_val_real, METH_VARARGS, "Get the value of a signal as a double precision float"},
    {"set_signal_val_long", set_signal_val_long, METH_VARARGS, "Set the value of a signal using a long"},
    {"set_signal_val_str", set_signal_val_str, METH_VARARGS, "Set the value of a signal using an NUL-terminated 8-bit string"},
    {"set_signal_val_binstr", set_signal_val_binstr, METH_VARARGS, "Set the value of a signal using a string with a character per bit"},
    {"set_signal_val_real", set_signal_val_real, METH_VARARGS, "Set the value of a signal using a double precision float"},
    {"get_definition_name", get_definition_name, METH_VARARGS, "Get the name of a GPI object's definition"},
    {"get_definition_file", get_definition_file, METH_VARARGS, "Get the file that sources the object's definition"},
    {"get_handle_by_name", get_handle_by_name, METH_VARARGS, "Get handle of a named object"},
    {"get_handle_by_index", get_handle_by_index, METH_VARARGS, "Get handle of a object at an index in a parent"},
    {"get_root_handle", get_root_handle, METH_VARARGS, "Get the root handle"},
    {"get_name_string", get_name_string, METH_VARARGS, "Get the name of an object as a string"},
    {"get_type_string", get_type_string, METH_VARARGS, "Get the type of an object as a string"},
    {"get_type", get_type, METH_VARARGS, "Get the type of an object, mapped to a GPI enumeration"},
    {"get_const", get_const, METH_VARARGS, "Get a flag indicating whether the object is a constant"},
    {"get_num_elems", get_num_elems, METH_VARARGS, "Get the number of elements contained in the handle"},
    {"get_range", get_range, METH_VARARGS, "Get the range of elements (tuple) contained in the handle, returns None if not indexable"},
    {"register_timed_callback", register_timed_callback, METH_VARARGS, "Register a timed callback"},
    {"register_value_change_callback", register_value_change_callback, METH_VARARGS, "Register a signal change callback"},
    {"register_readonly_callback", register_readonly_callback, METH_VARARGS, "Register a callback for the read-only section"},
    {"register_nextstep_callback", register_nextstep_callback, METH_VARARGS, "Register a callback for the NextSimTime callback"},
    {"register_rwsynch_callback", register_rwsynch_callback, METH_VARARGS, "Register a callback for the read-write section"},
    {"stop_simulator", stop_simulator, METH_VARARGS, "Instruct the attached simulator to stop"},
    {"iterate", iterate, METH_VARARGS, "Get an iterator handle to loop over all members in an object"},
    {"next", next, METH_VARARGS, "Get the next object from the iterator"},
    {"get_sim_time", get_sim_time, METH_VARARGS, "Get the current simulation time as an int tuple"},
    {"get_precision", get_precision, METH_VARARGS, "Get the precision of the simulator"},
    {"deregister_callback", deregister_callback, METH_VARARGS, "De-register a callback"},
    {"error_out", error_out, METH_NOARGS, NULL},
    {"get_simulator_product", get_simulator_product, METH_NOARGS, "Simulator product information"},
    {"get_simulator_version", get_simulator_version, METH_NOARGS, "Simulator product version information"},
    {"log_msg_native", log_msg_native, METH_VARARGS, "Log a message using the native GPI logger"},
    {"set_log_level_native", set_log_level_native, METH_VARARGS, "Set the log level of the native GPI logger"},
    {"log_msg", log_msg, METH_VARARGS, "Log a message using the current GPI logger"},
    {"set_log_handler", set_log_handler, METH_VARARGS, "Set a custom GPI log handler"},
    {"clear_log_handler", clear_log_handler, METH_NOARGS, "Reset the GPI log handler back to the native logger"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    NULL,
    sizeof(struct module_state),
    SimulatorMethods,
    NULL,
    simulator_traverse,
    simulator_clear,
    NULL
};

PyMODINIT_FUNC PyInit_simulator(void)
{
    PyObject* simulator;

    simulator = PyModule_Create(&moduledef);

    if (simulator == NULL)
        return NULL;
    struct module_state *st = GETSTATE(simulator);

    st->error = PyErr_NewException(MODULE_NAME ".Error", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(simulator);
        return NULL;
    }

    add_module_constants(simulator);
    return simulator;
}
