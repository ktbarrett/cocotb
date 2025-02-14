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

#ifndef COCOTB_GPI_H_
#define COCOTB_GPI_H_

/** \file gpi.h

Generic Language Interface
==========================

This header file defines a Generic Language Interface into any simulator.
Implementations need to implement the underlying functions in `gpi_priv.h`.

The functions are essentially a limited subset of VPI/VHPI/FLI.

Implementation-specific notes
-----------------------------

By amazing coincidence, VPI and VHPI are strikingly similar which is obviously
reflected by this header file. Unfortunately, this means that proprietary,
non-standard, less featured language interfaces (for example Mentor FLI) may
have to resort to some hackery.

Because of the lack of ability to register a callback on event change using the
FLI, we have to create a process with the signal on the sensitivity list to
imitate a callback.
*/

#include <stdint.h>

#include "exports.h"

#ifdef GPI_EXPORTS
#define GPI_EXPORT COCOTB_EXPORT
#else
#define GPI_EXPORT COCOTB_IMPORT
#endif

#ifdef __cplusplus
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif

/*
 * Declare the handle types.
 *
 * We want these handles to be opaque pointers, since their layout is not
 * exposed to C. We do this by using incomplete types. The assumption being
 * made here is that `sizeof(some_cpp_class*) == sizeof(some_c_struct*)`, which
 * is true on all reasonable platforms.
 */
#ifdef __cplusplus
/* In C++, we use forward-declarations of the types in gpi_priv.h as our
 * incomplete types, as this avoids the need for any casting in GpiCommon.cpp.
 */
class GpiObjHdl;
class GpiCbHdl;
class GpiIterator;
typedef GpiObjHdl *gpi_sim_hdl;
typedef GpiCbHdl *gpi_cb_hdl;
typedef GpiIterator *gpi_iterator_hdl;
#else
/* In C, we declare some incomplete struct types that we never complete.
 * The names of these are irrelevant, but for simplicity they match the C++
 * names.
 */
struct GpiObjHdl;
struct GpiCbHdl;
struct GpiIterator;
typedef struct GpiObjHdl *gpi_sim_hdl;
typedef struct GpiCbHdl *gpi_cb_hdl;
typedef struct GpiIterator *gpi_iterator_hdl;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Functions for controlling/querying the simulation state

/**
 * Useful for checking if a simulator is running.
 *
 * @return `1` if there is a registered GPI implementation, `0` otherwise.
 */
GPI_EXPORT int gpi_has_registered_impl(void) NOEXCEPT;

/** Request the simulation to end. */
GPI_EXPORT void gpi_end_sim(void) NOEXCEPT;

/** Return simulation time in *precision* unit. */
GPI_EXPORT uint64_t gpi_get_sim_time(void) NOEXCEPT;

/** Return unit of simulation time.
 *
 * @return Simulation time unit in 10**(result) seconds.
 */
GPI_EXPORT int32_t gpi_get_sim_precision(void) NOEXCEPT;

/** Return a string with the running simulator product information.
 *
 * @return The simulator product string.
 */
GPI_EXPORT const char *gpi_get_simulator_product(void) NOEXCEPT;

/** Return a string with the running simulator version.
 *
 * @return The simulator version string.
 */
GPI_EXPORT const char *gpi_get_simulator_version(void) NOEXCEPT;

// Functions for extracting a gpi_sim_hdl to an object

/** Return a handle to a simulation object given a full path.
 *
 * @param name The full path to the simulation object, or NULL for the root
 * object.
 * @return A handle to the child object or NULL if it could not be found or an
 * error occurred.
 */
GPI_EXPORT gpi_sim_hdl gpi_get_root_handle(const char *name) NOEXCEPT;

/** Return a handle to a child simulation object given a parent handle and a
 * name.
 *
 * @param parent The existing simulation parent. Must be an object with named
 * children objects.
 * @param name The name of the child object.
 * @return A handle to the child object or NULL if it could not be found or an
 * error occurred.
 */
GPI_EXPORT gpi_sim_hdl gpi_get_handle_by_name(gpi_sim_hdl parent,
                                              const char *name) NOEXCEPT;

/** Return a handle to a child simulation object given a parent handle and a
 * name.
 *
 * @param parent The existing simulation parent. Must be an object with
 * integer-indexed children objects.
 * @param index The index of the child object.
 * @return A handle to the child object or NULL if it could not be found or an
 * error occurred.
 */
GPI_EXPORT gpi_sim_hdl gpi_get_handle_by_index(gpi_sim_hdl parent,
                                               int32_t index) NOEXCEPT;

/** Types of simulation objects. */
typedef enum gpi_objtype_e {
    GPI_UNKNOWN = 0,
    GPI_MEMORY = 1,
    GPI_MODULE = 2,
    GPI_ARRAY = 6,
    GPI_ENUM = 7,
    GPI_STRUCTURE = 8,
    GPI_REAL = 9,
    GPI_INTEGER = 10,
    GPI_STRING = 11,
    GPI_GENARRAY = 12,
    GPI_PACKAGE = 13,
    GPI_PACKED_STRUCTURE = 14,
    GPI_LOGIC = 15,
    GPI_LOGIC_ARRAY = 16,
} gpi_objtype;

/** Types of iteration over a simulation object. */
typedef enum gpi_iterator_sel_e {
    GPI_OBJECTS = 1,
    GPI_DRIVERS = 2,
    GPI_LOADS = 3,
    GPI_PACKAGE_SCOPES = 4,
} gpi_iterator_sel;

/** Actions to perform when setting the value of a simulation object. */
typedef enum gpi_set_action_e {
    GPI_DEPOSIT = 0,
    GPI_FORCE = 1,
    GPI_RELEASE = 2,
    GPI_NO_DELAY = 3,
} gpi_set_action;

/** Directions of indexes of an indexable simulation object. */
typedef enum gpi_range_dir_e {
    GPI_RANGE_DOWN = -1,
    GPI_RANGE_NO_DIR = 0,
    GPI_RANGE_UP = 1,
} gpi_range_dir;

/** Types of edges to look for when registering a value change callback. */
typedef enum gpi_edge_e {
    GPI_RISING,
    GPI_FALLING,
    GPI_VALUE_CHANGE,
} gpi_edge;

// Functions for iterating over entries of a handle

/** Create an iterator over a simulation objects for child objects.
 *
 * Currently there is no way to stop iteration once started without leaking the
 * iterator object. Always call `gpi_next` until it returns NULL to ensure the
 * iterator is exhausted and cleaned up.
 *
 * @param base The simulation object to iterate over.
 * @param type The type of iteration.
 * @return An iterator object which can be passed to `gpi_next` or NULL if the
 * arguments are not valid. Returns an iterator even if no child objects are
 * found.
 */
GPI_EXPORT gpi_iterator_hdl gpi_iterate(gpi_sim_hdl base,
                                        gpi_iterator_sel type) NOEXCEPT;

/**
 * @return The next simulation object from the iterator or NULL when there are
 * no more objects.
 */
GPI_EXPORT gpi_sim_hdl gpi_next(gpi_iterator_hdl iterator) NOEXCEPT;

/**
 * @return The number of objects in the collection of the handle.
 */
GPI_EXPORT int32_t gpi_get_num_elems(gpi_sim_hdl gpi_sim_hdl) NOEXCEPT;

/**
 * @return The left side of the range constraint.
 */
GPI_EXPORT int32_t gpi_get_range_left(gpi_sim_hdl gpi_sim_hdl) NOEXCEPT;

/**
 * @return The right side of the range constraint.
 */
GPI_EXPORT int32_t gpi_get_range_right(gpi_sim_hdl gpi_sim_hdl) NOEXCEPT;

/**
 * @return The direction of the range constraint: `+1` for ascending, `-1` for
 * descending, `0` if the object is not indexable or the direction couldn't be
 * determined.
 */
GPI_EXPORT gpi_range_dir gpi_get_range_dir(gpi_sim_hdl gpi_sim_hdl) NOEXCEPT;

// Functions for getting values from a handle

/** Get the value of a logic scalar or array as a binary string.
 *
 * @param The simulation object.
 * @return A NUL-terminated ASCII string representing the value of the
 * simulation object. Each character represents an element of the logic array.
 * Or NULL if an error occurred.
 */
GPI_EXPORT const char *gpi_get_signal_value_binstr(
    gpi_sim_hdl gpi_hdl) NOEXCEPT;

/** Get the value of a string simulation object.
 *
 * @param The simulation object.
 * @return An unencoded NUL-terminated byte string representing the value of the
 * simulation object or NULL if an error occurred.
 */
GPI_EXPORT const char *gpi_get_signal_value_str(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/** Get the value of a floating point simulation object.
 *
 * @param The simulation object.
 * @param A pointer to memory where to place the floating point value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_get_signal_value_real(gpi_sim_hdl gpi_hdl,
                                         double *value) NOEXCEPT;

/** Get the value of an integer simulation object.
 *
 * @param The simulation object.
 * @param A pointer to memory where to place the integer value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_get_signal_value_long(gpi_sim_hdl gpi_hdl,
                                         int64_t *value) NOEXCEPT;

/** Get the path of the simulation object.
 *
 * @param The simulation object.
 * @return An unencoded NUL-terminated byte string of the full path to the
 * simulator object or NULL if an error occurred.
 */
GPI_EXPORT const char *gpi_get_signal_name_str(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @param The simulation object.
 * @return An unencoded byte string of the type of the simulator object or NULL
 * if an error occurred.
 */
GPI_EXPORT const char *gpi_get_signal_type_str(gpi_sim_hdl gpi_hdl) NOEXCEPT;

// Functions for querying the properties of a handle

/**
 * @param gpi_hdl The simulation object.
 * @return The type of the given simulation object.
 */
GPI_EXPORT gpi_objtype gpi_get_object_type(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @param gpi_hdl The simulation object.
 * @return The name of the definition of the simulation object or NULL if
 * it could not be found.
 */
GPI_EXPORT const char *gpi_get_definition_name(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @param gpi_hdl The simulation object.
 * @return The file in which the given simulation object was defined or NULL if
 * it could not be found.
 */
GPI_EXPORT const char *gpi_get_definition_file(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @param gpi_hdl The simulation object.
 * @return `1` if the given simulation object is immutable, `0` otherwise.
 */
GPI_EXPORT int gpi_is_constant(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @param gpi_hdl The simulation object.
 * @return `1` if the given simulation object is indexable, `0` otherwise.
 */
GPI_EXPORT int gpi_is_indexable(gpi_sim_hdl gpi_hdl) NOEXCEPT;

/**
 * @return `1` if the object is a signal object and supports getting and setting
 * values and registering value change callbacks, `0` otherwise.
 */
GPI_EXPORT int gpi_is_signal(gpi_sim_hdl gpi_hdl) NOEXCEPT;

// Functions for setting the properties of a handle

/** Set the value of a floating point simulation object.
 *
 * @param gpi_hdl The simulation object.
 * @param value The floating point value to set the simulation object to.
 * @param action The action to take when setting the value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_set_signal_value_real(gpi_sim_hdl gpi_hdl, double value,
                                         gpi_set_action action) NOEXCEPT;

/** Set the value of an integer simulation object.
 *
 * This takes an `int32_t` because some languages, simulators, and interfaces
 * are limited to 32-bit integers.
 *
 * @param gpi_hdl The simulation object.
 * @param value The integer value to set the simulation object to.
 * @param action The action to take when setting the value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_set_signal_value_int(gpi_sim_hdl gpi_hdl, int32_t value,
                                        gpi_set_action action) NOEXCEPT;

/** Set the value of a logic scalar or array simulation object.
 *
 * @param gpi_hdl The simulation object.
 * @param value An NUL-terminated ASCII string to set the simulation object to.
 * Each character represents the value of an element of the logic array.
 * @param action The action to take when setting the value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_set_signal_value_binstr(gpi_sim_hdl gpi_hdl, const char *str,
                                           gpi_set_action action) NOEXCEPT;

/** Set the value of a string simulation object.
 *
 * @param gpi_hdl The simulation object.
 * @param value A NUL-terminated byte string value to set the simulation object
 * to.
 * @param action The action to take when setting the value.
 * @return `0` on success, `1` on failure.
 */
GPI_EXPORT int gpi_set_signal_value_str(gpi_sim_hdl gpi_hdl, const char *str,
                                        gpi_set_action action) NOEXCEPT;

// Functions for registering callbacks on simulation events.

/** Register a callback to occur in `time` simulation time units.
 *
 * @param cb_func The callback function to call once the event occurs.
 * @param cb_data Data to pass as the single argument to the callback function
 * when the callback function is called.
 * @param time The number of simulation time steps from the current one when the
 * callback should be called. Must be positive.
 * @return A handle to the callback which can be cancelled by passing it to
 * `gpi_remove_cb`, or NULL if an error occurred.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_timed_callback(void (*cb_func)(void *),
                                                  void *cb_data,
                                                  uint64_t time) NOEXCEPT;

/** Register a callback to occur when a simulation object changes value.
 *
 * @param cb_func The callback function to call once the event occurs.
 * @param cb_data Data to pass as the single argument to the callback function
 * when the callback function is called.
 * @param gpi_hdl The simulation object to wait for a change in value.
 * @param edge Which kind of value change the callback should be called on.
 * @return A handle to the callback which can be cancelled by passing it to
 * `gpi_remove_cb`, or NULL if an error occurred.
 */
GPI_EXPORT gpi_cb_hdl
gpi_register_value_change_callback(void (*cb_func)(void *), void *cb_data,
                                   gpi_sim_hdl gpi_hdl, gpi_edge edge) NOEXCEPT;

/** Register a callback to be called in the next ReadOnly phase of the current
 * time step.
 *
 * @param cb_func The callback function to call once the event occurs.
 * @param cb_data Data to pass as the single argument to the callback function
 * when the callback function is called.
 * @return A handle to the callback which can be cancelled by passing it to
 * `gpi_remove_cb`, or NULL if an error occurred.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_readonly_callback(void (*cb_func)(void *),
                                                     void *cb_data) NOEXCEPT;

/** Register a callback to be called at the beginning of the next time step.
 *
 * @param cb_func The callback function to call once the event occurs.
 * @param cb_data Data to pass as the single argument to the callback function
 * when the callback function is called.
 * @return A handle to the callback which can be cancelled by passing it to
 * `gpi_remove_cb`, or NULL if an error occurred.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_nexttime_callback(void (*cb_func)(void *),
                                                     void *cb_data) NOEXCEPT;

/** Register a callback to be called in the next ReadWrite phase of the current
 * time step.
 *
 * @param cb_func The callback function to call once the event occurs.
 * @param cb_data Data to pass as the single argument to the callback function
 * when the callback function is called.
 * @return A handle to the callback which can be cancelled by passing it to
 * `gpi_remove_cb`, or NULL if an error occurred.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_readwrite_callback(void (*cb_func)(void *),
                                                      void *cb_data) NOEXCEPT;

/** Remove callback before it fires.
 *
 * The callback will not fire after this function is called.
 * The argument is no longer valid if this function succeeds.
 *
 * @param cb_hdl The handle to the callback to remove.
 */
GPI_EXPORT void gpi_remove_cb(gpi_cb_hdl cb_hdl) NOEXCEPT;

/** Retrieve user callback information from callback handle.
 *
 * @param cb_hdl The handle to the callback.
 * @param cb_func Where the user callback function should be placed.
 * @param cb_data Where the user callback function data should be placed.
 */
GPI_EXPORT void gpi_get_cb_info(gpi_cb_hdl cb_hdl, int (**cb_func)(void *),
                                void **cb_data) NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* COCOTB_GPI_H_ */
