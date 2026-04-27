// Copyright cocotb contributors
// Copyright (c) 2013, 2018 Potential Ventures Ltd
// Copyright (c) 2013 SolarFlare Communications Inc
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef GPI_H
#define GPI_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef GPI_EXPORT
#if _WIN32
#define GPI_EXPORT __declspec(dllexport)
#else
#define GPI_EXPORT __attribute__((visibility("default")))
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to a simulation object. */
typedef struct GpiObjHdl *gpi_obj_hdl;

/** Handle to a callback object. */
typedef struct GpiCbHdl *gpi_cb_hdl;

/** Handle to an iterator object. */
typedef struct GpiIterHdl *gpi_iter_hdl;

/** @defgroup SimIntf Simulator Control and Interrogation
 * These functions are for controlling and querying
 * simulator state and information.
 * @{
 */

/** Control types for the simulator. */
typedef enum {
    GPI_FINISH,
} gpi_control_type;

/** Stop the simulation after control returns to the GPI.
 *
 * @param control_type Type of control action to perform.
 * @return             Zero on success, non-zero on failure.
 */
GPI_EXPORT int gpi_control(gpi_control_type control_type);

/** Return the arguments to the simulator executable call.
 *
 * @param sim_name      Location to return simulator name.
 * @param sim_version   Location to return simulator version.
 * @param argc          Pointer to store argument count.
 * @param argv          Pointer to store argument values.
 * @return              Zero on success, non-zero on failure.
 */
GPI_EXPORT int gpi_get_simulator_info(const char **sim_name,
                                      const char **sim_version, int *argc,
                                      char const *const **argv);

/** @} */  // End of group SimIntf

/** @defgroup ObjQuery Simulation Object Query
 * These functions are for getting handles to simulation objects.
 * @{
 */

/** Get a handle to a child simulation object by its name.
 *
 * @param parent            Parent object handle or `NULL` for root.
 * @param name              Name of the child object.
 * @return                  Handle to simulation object or `NULL` if not found.
 */
GPI_EXPORT gpi_obj_hdl gpi_get_obj_by_name(gpi_obj_hdl parent,
                                           const char *name);

/** Get a handle to a child simulation object by its index.
 *
 * @param parent    Parent indexable object handle.
 * @param index     Index of the child object.
 * @return          Handle to simulation object or `NULL` if not found.
 */
GPI_EXPORT gpi_obj_hdl gpi_get_obj_by_index(gpi_obj_hdl parent, int64_t index);

/** @} */  // End of group ObjQuery

/** @defgroup ObjProps General Object Properties
 * These functions are for getting and setting properties of a simulation
 * object.
 * @{
 */

/** Integer properties of a simulation object. */
typedef enum {
    GPI_OBJ_TYPE,   ///< Type of simulation object, a value in the `GPI_TYPE_*`
                    ///< set of macros.
    GPI_SIZE,       ///< Size of the object.
    GPI_LEFT,       ///< Left bound of the object's range.
    GPI_RIGHT,      ///< Right bound of the object's range.
    GPI_DIRECTION,  ///< Direction of the object's range.
    GPI_CONST,      ///< Whether the object is constant.
    GPI_SIGNED,     ///< Whether the object is signed.
    GPI_SIM_TIME,   ///< Simulation time of the object.
    GPI_SIM_PRECISION,  ///< Simulation precision of the object.
} gpi_int_property_type;

/** String properties of a simulation object. */
typedef enum {
    GPI_DEFINITION_NAME,  ///< Name of the object's definition.
    GPI_DEFINITION_FILE,  ///< File where the object's definition is located.
    GPI_NAME,             ///< Name of the object.
    GPI_TYPE,             ///< Type of the object as a string.
    GPI_PATH,             ///< Hierarchical path to the object.
} gpi_str_property_type;

// 0 reserved for error
// These values can overlap as they are contextual based on the type of the
// query.

/** Type of simulation object. */
#define GPI_TYPE_UNKNOWN 0
#define GPI_TYPE_MEMORY 1
#define GPI_TYPE_MODULE 2
// GPI_NET = 3,  // Deprecated
// GPI_PARAMETER = 4,  // Deprecated
// GPI_REGISTER = 5,  // Deprecated
#define GPI_TYPE_ARRAY 6
#define GPI_TYPE_ENUM 7
#define GPI_TYPE_STRUCTURE 8
#define GPI_TYPE_REAL 9
#define GPI_TYPE_INTEGER 10
#define GPI_TYPE_STRING 11
#define GPI_TYPE_GENARRAY 12
#define GPI_TYPE_PACKAGE 13
#define GPI_TYPE_PACKED_STRUCTURE 14
#define GPI_TYPE_LOGIC 15
#define GPI_TYPE_LOGIC_ARRAY 16
#define GPI_TYPE_FIXED_STRING 17

/** Direction of range constraint of an object. */
#define GPI_RANGE_DOWN -1
#define GPI_RANGE_NO_DIR 0
#define GPI_RANGE_UP 1

/** Get an integer property of a simulation object.
 *
 * @param prop      Property to query.
 * @param obj_hdl   Simulation object handle or `NULL` for global properties.
 * @param value     Location to place property value.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_property_int(gpi_int_property_type prop, gpi_obj_hdl obj_hdl,
                                int64_t *value);

/** Get a string property of a simulation object.
 *
 * @param prop      Property to query.
 * @param obj_hdl   Simulation object handle or `NULL` for global properties.
 * @return          Property value, or `NULL` if an error occurred.
 */
GPI_EXPORT char const *gpi_property_str(gpi_str_property_type prop,
                                        gpi_obj_hdl obj_hdl);

/** @} */  // End of group ObjProps

/** @defgroup SigValueProps Signal Value Properties
 * These functions are for getting and setting properties of a signal object.
 * @{
 */

/** Action to use when setting object value. */
typedef enum {
    GPI_DEPOSIT = 0,
    GPI_FORCE = 1,
    GPI_RELEASE = 2,
    GPI_NO_DELAY = 3,
} gpi_set_action;

// Getting properties

/** Get signal object value as a byte array.
 *
 * @param sig_hdl   Signal object handle.
 * @return          Object value as a null-terminated byte array, or `NULL` if
 * an error occurred.
 */
GPI_EXPORT const char *gpi_get_value_str(gpi_obj_hdl sig_hdl);

/** Get signal object value as a real.
 *
 * @param sig_hdl   Signal object handle.
 * @param value     Location to place object value.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_get_value_real(gpi_obj_hdl sig_hdl, double *value);

/** Get signal object value as a 64-bit signed integer.
 *
 * @param sig_hdl   Signal object handle.
 * @param value     Location to place object value.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_get_value_int(gpi_obj_hdl sig_hdl, int64_t *value);

// Setting properties

/** Set simulator object value with a real.
 *
 * @param sig_hdl   Signal object handle.
 * @param value     Object value.
 * @param action    Action to use.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_set_value_real(gpi_obj_hdl sig_hdl, double value,
                                  gpi_set_action action);

/** Set simulator object value with a 64-bit signed integer.
 *
 * @param sig_hdl   Signal object handle.
 * @param value     Object value.
 * @param action    Action to use.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_set_value_int(gpi_obj_hdl sig_hdl, int64_t value,
                                 gpi_set_action action);

/** Set simulator object value with a byte array.
 *
 * @param sig_hdl   Signal object handle.
 * @param str       Object value. Null-terminated byte array.
 * @param action    Action to use.
 * @return          0 if successful, non-zero if an error occurred.
 */
GPI_EXPORT int gpi_set_value_str(gpi_obj_hdl sig_hdl, const char *str,
                                 gpi_set_action action);

/** @} */  // End of group SigValueProps

/** @defgroup HandleIteration Simulation Object Iteration
 * These functions are for iterating over simulation object handles
 * to discover child objects.
 * @{
 */

/** Types of child objects to search for when iterating. */
typedef enum {
    GPI_OBJECTS = 1,
    GPI_DRIVERS = 2,
    GPI_LOADS = 3,
    GPI_PACKAGE_SCOPES = 4,
    GPI_TOPS = 5,
} gpi_iter_type;

/** Start iteration on a simulation object.
 *
 * The iterator handle may only be `NULL` if the `type` is not supported. If no
 * objects of the requested type are found, an empty iterator is returned.
 *
 * @param type  Iteration type.
 * @param base  Simulation object to iterate over or `NULL` for top-level
 *              objects.
 * @return      An iterator handle which can then be used with @ref gpi_next or
 *              `NULL` if the iterator could not be created.
 */
GPI_EXPORT gpi_iter_hdl gpi_iterate(gpi_iter_type type, gpi_obj_hdl base);

/** Get next object in iteration.
 *
 * This function cannot fail. When there are no more objects, it returns `NULL`.
 *
 * @param iter_hdl  Iterator handle.
 * @return          Object handle, or `NULL` when there are no more objects.
 */
GPI_EXPORT gpi_obj_hdl gpi_next(gpi_iter_hdl iter_hdl);

/** @} */  // End of group HandleIteration

/** @defgroup SimCallbacks Simulation Callbacks
 * These functions are for registering and controlling callbacks.
 * @{
 */

/** Type of callback. */
typedef enum {
    GPI_NEXTTIME,          /** Callback at the next simulation time step. */
    GPI_READWRITE,         /** Callback when we enter the ReadWrite phase. */
    GPI_READONLY,          /** Callback when we enter the ReadOnly phase. */
    GPI_START_OF_SIM_TIME, /** Callback at the start of simulation time. */
    GPI_END_OF_SIM_TIME,   /** Callback at the end of simulation time. */
} gpi_cb_type;

/** Type of value change to match when registering for callback. */
typedef enum {
    GPI_VALUE_CHANGE, /** Callback on any value change. */
    GPI_RISING,       /** Callback on X->1 transition. */
    GPI_FALLING,      /** Callback on X->0 transition. */
} gpi_value_change_cb_type;

/** Register a callback.
 *
 * @param cb_type   Type of callback.
 * @param cb        Callback function pointer.
 * @param cb_data   Pointer to user data to be passed to callback function.
 * @return          Handle to callback object or `NULL` if the callback could
 *                  not be registered.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_cb(gpi_cb_type cb_type, void (*cb)(void *),
                                      void *cb_data);

/** Register a timed callback.
 *
 * @param time      Time delay in simulation time units.
 * @param cb        Callback function pointer.
 * @param cb_data   Pointer to user data to be passed to callback function.
 * @return          Handle to callback object or `NULL` if the callback could
 *                  not be registered.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_timed_cb(uint64_t time, void (*cb)(void *),
                                            void *cb_data);
/** Register a value change callback.
 *
 * @param sim_hdl   Simulation object to monitor for value change.
 * @param cb_type   Type of value change to monitor for.
 * @param cb        Callback function pointer.
 * @param cb_data   Pointer to user data to be passed to callback function.
 * @return          Handle to callback object or `NULL` if the callback could
 *                  not be registered.
 */
GPI_EXPORT gpi_cb_hdl gpi_register_value_change_cb(
    gpi_obj_hdl sim_hdl, gpi_value_change_cb_type cb_type, void (*cb)(void *),
    void *cb_data);

/** Remove callback.
 *
 * The callback will not fire after this function is called.
 * The handle is no longer valid if this function succeeds.
 *
 * @param cb_hdl    The handle to the callback to remove.
 * @return          `0` on successful removal, `1` otherwise.
 */
GPI_EXPORT int gpi_remove_cb(gpi_cb_hdl cb_hdl);

/** Retrieve user callback information from callback handle.
 *
 * This function cannot fail.
 *
 * @param cb_hdl    The handle to the callback.
 * @param cb_func   Where the user callback function should be placed.
 * @param cb_data   Where the user callback function data should be placed.
 */
GPI_EXPORT void gpi_get_cb_info(gpi_cb_hdl cb_hdl, void (**cb_func)(void *),
                                void **cb_data);

/** @} */  // End of group SimCallbacks

#ifdef __cplusplus
}
#endif

#endif /* GPI_H */
