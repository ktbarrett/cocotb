/******************************************************************************
 * Copyright (c) 2013, 2018 Potential Ventures Ltd
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
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE dGOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#ifndef COCOTB_GPI_PRIV_H_
#define COCOTB_GPI_PRIV_H_

#include <string>

#include "exports.h"
#include "gpi.h"

#ifdef GPI_EXPORTS
#define GPI_EXPORT COCOTB_EXPORT
#else
#define GPI_EXPORT COCOTB_IMPORT
#endif

class GpiImpl;
class GpiIterator;
class GpiCbHdl;
class GpiObjHdl;

/** GPI object handle, maps to a simulation object
 *
 * An object is any item in the hierarchy
 * Provides methods for iterating through children or finding by name
 * Initial object is returned by call to GpiImplInterface::get_root_handle()
 * Subsequent operations to get children go through this handle.
 * GpiObjHdl::get_handle_by_name/get_handle_by_index are really factories
 * that construct an object derived from GpiSignalObjHdl or GpiObjHdl
 */
class GPI_EXPORT GpiObjHdl {
  public:
    GpiObjHdl(GpiImpl *impl) noexcept : m_impl(impl) {}
    GpiObjHdl() = delete;
    virtual ~GpiObjHdl() = default;

    // Debug related
    virtual const std::string &repr() = 0;

    // Object Properties
    virtual const std::string &get_type_str();
    virtual gpi_objtype get_type() = 0;
    virtual int32_t get_num_elems() = 0;
    virtual int32_t get_range_left() = 0;
    virtual int32_t get_range_right() = 0;
    virtual gpi_range_dir get_range_dir() = 0;
    virtual bool is_const() = 0;
    virtual int is_indexable() = 0;
    virtual const char *get_definition_name() = 0;
    virtual const char *get_definition_file() = 0;
    virtual bool is_signal() noexcept { return false; }

    // Path and name
    virtual const std::string &get_fullname() = 0;
    virtual const std::string &get_name() = 0;

    GpiImpl *get_impl() const noexcept { return m_impl; }

  protected:
    GpiImpl *m_impl;
};

/** GPI Signal object handle, maps to a simulation object.
 *
 * Identical to an object but adds additional methods for getting/setting the
 * value of the signal (which doesn't apply to non signal items in the
 * hierarchy).
 */
class GPI_EXPORT GpiSignalObjHdl : public GpiObjHdl {
  public:
    using GpiObjHdl::GpiObjHdl;

    bool is_signal() noexcept override { return true; }

    // Provide public access to the implementation (composition vs inheritance)
    virtual const char *get_signal_value_binstr() = 0;
    virtual const char *get_signal_value_str() = 0;
    virtual double get_signal_value_real() = 0;
    virtual long get_signal_value_long() = 0;

    virtual int set_signal_value(int32_t value, gpi_set_action action) = 0;
    virtual int set_signal_value(double value, gpi_set_action action) = 0;
    virtual int set_signal_value_str(const std::string &value,
                                     gpi_set_action action) = 0;
    virtual int set_signal_value_binstr(const std::string &value,
                                        gpi_set_action action) = 0;

    virtual GpiCbHdl *register_value_change_callback(gpi_edge edge,
                                                     void (*cb_func)(void *),
                                                     void *cb_data) = 0;
};

class GPI_EXPORT GpiCbHdl {
  public:
    GpiCbHdl(int (*cb_func)(void *), void *cb_data) noexcept
        : m_cb_func(cb_func), m_cb_data(cb_data) {}
    virtual ~GpiCbHdl() = default;

    // Debug related
    virtual const std::string &repr() = 0;

    /** Get the current user callback function and data. */
    void get_cb_info(int (**cb_func)(void *), void **cb_data) noexcept {
        if (cb_func) {
            *cb_func = m_cb_func;
        }
        if (cb_data) {
            *cb_data = m_cb_data;
        }
    }

    /** Remove the callback before it fires.
     *
     * This function should delete the object.
     */
    virtual void remove() = 0;

    /** Run the callback.
     *
     * This function should delete the object if it can't fire again.
     */
    virtual void run() = 0;

  protected:
    int (*m_cb_func)(void *);  // GPI function to callback
    void *m_cb_data;           // GPI data supplied to "m_cb_func"
};

class GPI_EXPORT GpiIterator {
  public:
    enum Status {
        NATIVE,          // Fully resolved object was created
        NATIVE_NO_NAME,  // Native object was found but unable to fully create
        NOT_NATIVE,      // Non-native object was found but we did get a name
        NOT_NATIVE_NO_NAME,  // Non-native object was found without a name
        END
    };

    GpiIterator(GpiImpl *impl, GpiObjHdl *parent) noexcept
        : m_impl(impl), m_parent(parent) {};
    virtual ~GpiIterator() = default;

    // Debug related
    virtual const std::string &repr() = 0;

    virtual Status next_handle(std::string &name, GpiObjHdl **hdl, void **) = 0;

    GpiObjHdl *get_parent() const noexcept { return m_parent; }
    GpiImpl *get_impl() const noexcept { return m_impl; }

  protected:
    GpiImpl *m_impl;
    GpiObjHdl *m_parent;
};

class GPI_EXPORT GpiImpl {
  public:
    GpiImpl() = delete;
    virtual ~GpiImpl() = default;

    // Debug related
    virtual const std::string &repr() = 0;

    /* Sim related */
    virtual void end_sim() = 0;
    virtual uint64_t get_sim_time() = 0;
    virtual int32_t get_sim_precision() = 0;
    virtual const std::string &get_simulator_product() = 0;
    virtual const std::string &get_simulator_version() = 0;

    /* Hierarchy related */
    virtual GpiObjHdl *native_check_create(const std::string &name,
                                           GpiObjHdl *parent) = 0;
    virtual GpiObjHdl *native_check_create(int32_t index,
                                           GpiObjHdl *parent) = 0;
    virtual GpiObjHdl *native_check_create(void *raw_hdl,
                                           GpiObjHdl *parent) = 0;
    virtual GpiObjHdl *get_root_handle(const char *name) = 0;
    virtual GpiIterator *iterate_handle(GpiObjHdl *obj_hdl,
                                        gpi_iterator_sel type) = 0;

    /* Callback related, these may (will) return the same handle */
    virtual GpiCbHdl *register_timed_callback(uint64_t time,
                                              void (*gpi_function)(void *),
                                              void *gpi_cb_data) = 0;
    virtual GpiCbHdl *register_readonly_callback(void (*gpi_function)(void *),
                                                 void *gpi_cb_data) = 0;
    virtual GpiCbHdl *register_nexttime_callback(void (*gpi_function)(void *),
                                                 void *gpi_cb_data) = 0;
    virtual GpiCbHdl *register_readwrite_callback(void (*gpi_function)(void *),
                                                  void *gpi_cb_data) = 0;
};

/** Register an implementation with the global implementation record. */
GPI_EXPORT void gpi_register_impl(GpiImpl *impl);

/** Called when the simulation starts. */
GPI_EXPORT void gpi_start_sim();

/** Called when the simulation ends by request of the simulator. */
GPI_EXPORT void gpi_stop_sim();

/** The entry point into the GPI. */
GPI_EXPORT int gpi_initialize(int argc, char const *const *argv);

/** Called right before the simulator is terminated. */
GPI_EXPORT void gpi_finalize();

/** Delimits where the simulator gives control to the GPI. */
GPI_EXPORT void gpi_to_user();

/** Delimits where the GPI returns control to the simulator. */
GPI_EXPORT void gpi_to_simulator();

typedef void (*layer_entry_func)();

/* Use this macro in an implementation layer to define an entry point */
#define GPI_ENTRY_POINT(NAME, func)                     \
    extern "C" {                                        \
    COCOTB_EXPORT void NAME##_entry_point() { func(); } \
    }

#endif /* COCOTB_GPI_PRIV_H_ */
