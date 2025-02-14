/******************************************************************************
 * Copyright (c) 2013 Potential Ventures Ltd
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

#include <sys/types.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "cocotb_utils.h"
#include "embed.h"
#include "gpi.h"
#include "gpi_priv.h"

static std::vector<GpiImpl *> registered_impls;

// TODO Global handle store exists to work around the hard problem of garbage
// collection. Parents should have ref-counted pointers to children and children
// weak references to parents.

class GpiHandleStore {
  public:
    GpiObjHdl *check_and_store(GpiObjHdl *hdl) {
        std::map<std::string, GpiObjHdl *>::iterator it;

        const std::string &name = hdl->get_fullname();

        LOG_DEBUG("Checking %s exists", name.c_str());

        it = handle_map.find(name);
        if (it == handle_map.end()) {
            handle_map[name] = hdl;
            return hdl;
        } else {
            LOG_DEBUG("Found duplicate %s", name.c_str());

            delete hdl;
            return it->second;
        }
    }

    uint64_t handle_count() { return handle_map.size(); }

    void clear() {
        std::map<std::string, GpiObjHdl *>::iterator it;

        // Delete the object handles before clearing the map
        for (it = handle_map.begin(); it != handle_map.end(); it++) {
            delete (it->second);
        }
        handle_map.clear();
    }

  private:
    std::map<std::string, GpiObjHdl *> handle_map;
};

static GpiHandleStore unique_handles;

#define CHECK_AND_STORE(_x) unique_handles.check_and_store(_x)
#define CLEAR_STORE() unique_handles.clear()

void gpi_register_impl(GpiImpl *impl) {
    registered_impls.push_back(impl);
    LOG_INFO("%s registered", impl->repr().c_str());
}

int gpi_has_registered_impl() noexcept { return registered_impls.size() > 0; }

void gpi_start_sim() { user_start_sim(); }

// Exists to prevent calling GpiImpl::end_sim if the simulator has already
// requested shutdown, or if it's already been called once before.
static bool sim_ending = false;

void gpi_stop_sim() {
    sim_ending = true;
    user_stop_sim();
}

void gpi_end_sim() noexcept {
    if (!sim_ending) {
        registered_impls[0]->end_sim();
        sim_ending = true;
    }
}

void gpi_finalize() {
    CLEAR_STORE();
    user_finalize();
}

static void gpi_load_libs(std::vector<std::string> to_load) {
    std::vector<std::string>::iterator iter;

    for (iter = to_load.begin(); iter != to_load.end(); iter++) {
        std::string arg = *iter;

        auto const idx = arg.rfind(
            ':');  // find from right since path could contain colons (Windows)
        if (idx == std::string::npos) {
            // no colon in the string
            printf("cocotb: Error parsing GPI_EXTRA %s\n", arg.c_str());
            exit(1);
        }

        std::string const lib_name = arg.substr(0, idx);
        std::string const func_name = arg.substr(idx + 1, std::string::npos);

        void *lib_handle = utils_dyn_open(lib_name.c_str());
        if (!lib_handle) {
            printf("cocotb: Error loading shared library %s\n",
                   lib_name.c_str());
            exit(1);
        }

        void *entry_point = utils_dyn_sym(lib_handle, func_name.c_str());
        if (!entry_point) {
            char const *fmt =
                "cocotb: Unable to find entry point %s for shared library "
                "%s\n%s";
            char const *msg =
                "        Perhaps you meant to use `,` instead of `:` to "
                "separate library names, as this changed in cocotb 1.4?\n";
            printf(fmt, func_name.c_str(), lib_name.c_str(), msg);
            exit(1);
        }

        layer_entry_func new_lib_entry = (layer_entry_func)entry_point;
        new_lib_entry();
    }
}

int gpi_initialize(int argc, char const *const *argv) {
    /* Lets look at what other libs we were asked to load too */
    char *lib_env = getenv("GPI_EXTRA");

    if (lib_env) {
        std::string lib_list = lib_env;
        std::string const delim = ",";
        std::vector<std::string> to_load;

        size_t e_pos = 0;
        while (std::string::npos != (e_pos = lib_list.find(delim))) {
            std::string lib = lib_list.substr(0, e_pos);
            lib_list.erase(0, e_pos + delim.length());

            to_load.push_back(lib);
        }
        if (lib_list.length()) {
            to_load.push_back(lib_list);
        }

        gpi_load_libs(to_load);
    }

    return user_initialize(argc, argv);
}

uint64_t gpi_get_sim_time() noexcept {
    return registered_impls[0]->get_sim_time();
}

int32_t gpi_get_sim_precision() noexcept {
    return registered_impls[0]->get_sim_precision();
}

const char *gpi_get_simulator_product() noexcept {
    return registered_impls[0]->get_simulator_product().c_str();
}

const char *gpi_get_simulator_version() noexcept {
    return registered_impls[0]->get_simulator_version().c_str();
}

gpi_sim_hdl gpi_get_root_handle(const char *name) noexcept {
    /* May need to look over all the implementations that are registered
       to find this handle */

    GpiObjHdl *hdl = NULL;

    LOG_DEBUG("Looking for root handle '%s' over %d implementations", name,
              registered_impls.size());

    for (auto impl : registered_impls) {
        if ((hdl = impl->get_root_handle(name))) {
            LOG_DEBUG("Got a Root handle (%s) back from %s",
                      hdl->get_name().c_str(), impl->repr().c_str());
            break;
        }
    }

    if (hdl)
        return CHECK_AND_STORE(hdl);
    else {
        LOG_ERROR("No root handle found");
        return hdl;
    }
}

static GpiObjHdl *gpi_get_handle_by_name_(GpiObjHdl *parent,
                                          const std::string &name,
                                          GpiImpl *skip_impl) noexcept {
    LOG_DEBUG("Searching for %s", name.c_str());

    // check parent impl *first* if it's not skipped
    if (!skip_impl || (skip_impl != parent->get_impl())) {
        auto hdl = parent->get_impl()->native_check_create(name, parent);
        if (hdl) {
            return CHECK_AND_STORE(hdl);
        }
    }

    // iterate over all registered impls to see if we can get the signal
    for (auto impl : registered_impls) {
        // check if impl is skipped
        if (skip_impl && (skip_impl == impl)) {
            LOG_DEBUG("Skipping %s implementation", impl->repr().c_str());
            continue;
        }

        // already checked parent implementation
        if (impl == parent->get_impl()) {
            LOG_DEBUG("Already checked %s implementation",
                      impl->repr().c_str());
            continue;
        }

        LOG_DEBUG("Checking if %s is native through implementation %s",
                  name.c_str(), impl->repr().c_str());

        /* If the current interface is not the same as the one that we
           are going to query then append the name we are looking for to
           the parent, such as <parent>.name. This is so that its entity can
           be seen discovered even if the parents implementation is not the same
           as the one that we are querying through */

        auto hdl = impl->native_check_create(name, parent);
        if (hdl) {
            LOG_DEBUG("Found %s via %s", name.c_str(), impl->repr().c_str());
            return CHECK_AND_STORE(hdl);
        }
    }

    return NULL;
}

static GpiObjHdl *gpi_get_handle_by_raw(GpiObjHdl *parent, void *raw_hdl,
                                        GpiImpl *skip_impl) noexcept {
    GpiObjHdl *hdl = NULL;

    for (auto impl : registered_impls) {
        if (skip_impl && (skip_impl == impl)) {
            LOG_DEBUG("Skipping %s implementation", impl->repr().c_str());
            continue;
        }

        if ((hdl = impl->native_check_create(raw_hdl, parent))) {
            LOG_DEBUG("Found %s via %s", hdl->get_name().c_str(),
                      impl->repr().c_str());
            break;
        }
    }

    if (hdl)
        return CHECK_AND_STORE(hdl);
    else {
        LOG_WARN(
            "Failed to convert a raw handle to valid object via any registered "
            "implementation");
        return hdl;
    }
}

gpi_sim_hdl gpi_get_handle_by_name(gpi_sim_hdl base,
                                   const char *name) noexcept {
    std::string s_name = name;
    GpiObjHdl *hdl = gpi_get_handle_by_name_(base, s_name, NULL);
    if (!hdl) {
        LOG_DEBUG(
            "Failed to find a handle named %s via any registered "
            "implementation",
            name);
    }
    return hdl;
}

gpi_sim_hdl gpi_get_handle_by_index(gpi_sim_hdl base, int32_t index) noexcept {
    GpiObjHdl *hdl = NULL;
    GpiImpl *impl = base->get_impl();

    /* Shouldn't need to iterate over interfaces because indexing into a handle
     * shouldn't cross the interface boundaries.
     *
     * NOTE: IUS's VPI interface returned valid VHDL handles, but then couldn't
     *       use the handle properly.
     */
    LOG_DEBUG("Checking if index %d native through implementation %s ", index,
              impl->repr().c_str());
    hdl = impl->native_check_create(index, base);

    if (hdl)
        return CHECK_AND_STORE(hdl);
    else {
        LOG_WARN(
            "Failed to find a handle at index %d via any registered "
            "implementation",
            index);
        return hdl;
    }
}

gpi_iterator_hdl gpi_iterate(gpi_sim_hdl obj_hdl,
                             gpi_iterator_sel type) noexcept {
    if (type == GPI_PACKAGE_SCOPES) {
        if (obj_hdl != NULL) {
            LOG_ERROR("Cannot iterate over package from non-NULL handles");
            return NULL;
        }

        LOG_DEBUG("Looking for packages over %d implementations",
                  registered_impls.size());

        for (auto impl : registered_impls) {
            GpiIterator *iter = impl->iterate_handle(NULL, GPI_PACKAGE_SCOPES);
            if (iter != NULL) return iter;
        }
        return NULL;
    }

    GpiIterator *iter = obj_hdl->get_impl()->iterate_handle(obj_hdl, type);
    if (!iter) {
        return NULL;
    }
    return iter;
}

gpi_sim_hdl gpi_next(gpi_iterator_hdl iter) noexcept {
    std::string name;
    GpiObjHdl *parent = iter->get_parent();

    while (true) {
        GpiObjHdl *next = NULL;
        void *raw_hdl = NULL;
        GpiIterator::Status ret = iter->next_handle(name, &next, &raw_hdl);

        switch (ret) {
            case GpiIterator::NATIVE:
                LOG_DEBUG("Create a native handle");
                return CHECK_AND_STORE(next);
            case GpiIterator::NATIVE_NO_NAME:
                LOG_DEBUG("Unable to fully setup handle, skipping");
                continue;
            case GpiIterator::NOT_NATIVE:
                LOG_DEBUG(
                    "Found a name but unable to create via native "
                    "implementation, trying others");
                next = gpi_get_handle_by_name_(parent, name, iter->get_impl());
                if (next) {
                    return next;
                }
                LOG_WARN(
                    "Unable to create %s via any registered implementation",
                    name.c_str());
                continue;
            case GpiIterator::NOT_NATIVE_NO_NAME:
                LOG_DEBUG(
                    "Found an object but not accessible via %s, trying others",
                    iter->get_impl()->repr().c_str());
                next = gpi_get_handle_by_raw(parent, raw_hdl, iter->get_impl());
                if (next) {
                    return next;
                }
                continue;
            case GpiIterator::END:
                LOG_DEBUG("Reached end of iterator");
                delete iter;
                return NULL;
        }
    }
}

const char *gpi_get_definition_name(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_definition_name();
}

const char *gpi_get_definition_file(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_definition_file();
}

const char *gpi_get_signal_value_binstr(gpi_sim_hdl sig_hdl) noexcept {
    if (!sig_hdl->is_signal()) {
        return NULL;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    static std::string g_binstr = obj_hdl->get_signal_value_binstr();
    std::transform(g_binstr.begin(), g_binstr.end(), g_binstr.begin(),
                   ::toupper);
    return g_binstr.c_str();
}

const char *gpi_get_signal_value_str(gpi_sim_hdl sig_hdl) noexcept {
    if (!sig_hdl->is_signal()) {
        return NULL;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return obj_hdl->get_signal_value_str();
}

int gpi_get_signal_value_real(gpi_sim_hdl sig_hdl, double *value) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    *value = obj_hdl->get_signal_value_real();
    return 0;
}

int gpi_get_signal_value_long(gpi_sim_hdl sig_hdl, int64_t *value) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    *value = obj_hdl->get_signal_value_long();
    return 0;
}

const char *gpi_get_signal_name_str(gpi_sim_hdl sig_hdl) noexcept {
    if (!sig_hdl->is_signal()) {
        return NULL;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return obj_hdl->get_name().c_str();
}

const char *gpi_get_signal_type_str(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_type_str().c_str();
}

gpi_objtype gpi_get_object_type(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_type();
}

int gpi_is_constant(gpi_sim_hdl obj_hdl) noexcept {
    if (obj_hdl->is_const()) return 1;
    return 0;
}

int gpi_is_indexable(gpi_sim_hdl obj_hdl) noexcept {
    if (obj_hdl->is_indexable()) return 1;
    return 0;
}

int gpi_is_signal(gpi_sim_hdl obj_hdl) noexcept {
    if (obj_hdl->is_signal()) return 1;
    return 0;
}

int gpi_set_signal_value_int(gpi_sim_hdl sig_hdl, int32_t value,
                             gpi_set_action action) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return obj_hdl->set_signal_value(value, action);
}

int gpi_set_signal_value_binstr(gpi_sim_hdl sig_hdl, const char *binstr,
                                gpi_set_action action) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    std::string value = binstr;
    return obj_hdl->set_signal_value_binstr(value, action);
}

int gpi_set_signal_value_str(gpi_sim_hdl sig_hdl, const char *str,
                             gpi_set_action action) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    std::string value = str;
    return obj_hdl->set_signal_value_str(value, action);
}

int gpi_set_signal_value_real(gpi_sim_hdl sig_hdl, double value,
                              gpi_set_action action) noexcept {
    if (!sig_hdl->is_signal()) {
        return 1;
    }
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return obj_hdl->set_signal_value(value, action);
}

int gpi_get_num_elems(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_num_elems();
}

int gpi_get_range_left(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_range_left();
}

int gpi_get_range_right(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_range_right();
}

gpi_range_dir gpi_get_range_dir(gpi_sim_hdl obj_hdl) noexcept {
    return obj_hdl->get_range_dir();
}

gpi_cb_hdl gpi_register_value_change_callback(void (*cb_func)(void *),
                                              void *cb_data,
                                              gpi_sim_hdl sig_hdl,
                                              gpi_edge edge) noexcept {
    if (!sig_hdl->is_signal()) {
        return NULL;
    }
    GpiSignalObjHdl *signal_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return signal_hdl->register_value_change_callback(edge, cb_func, cb_data);
}

gpi_cb_hdl gpi_register_timed_callback(void (*cb_func)(void *), void *cb_data,
                                       uint64_t time) noexcept {
    return registered_impls[0]->register_timed_callback(time, cb_func, cb_data);
}

gpi_cb_hdl gpi_register_readonly_callback(void (*cb_func)(void *),
                                          void *cb_data) noexcept {
    return registered_impls[0]->register_readonly_callback(cb_func, cb_data);
}

gpi_cb_hdl gpi_register_nexttime_callback(void (*cb_func)(void *),
                                          void *cb_data) noexcept {
    return registered_impls[0]->register_nexttime_callback(cb_func, cb_data);
}

gpi_cb_hdl gpi_register_readwrite_callback(void (*cb_func)(void *),
                                           void *cb_data) noexcept {
    return registered_impls[0]->register_readwrite_callback(cb_func, cb_data);
}

void gpi_remove_cb(gpi_cb_hdl cb_hdl) noexcept { cb_hdl->remove(); }

void gpi_get_cb_info(gpi_cb_hdl cb_hdl, int (**cb_func)(void *),
                     void **cb_data) noexcept {
    cb_hdl->get_cb_info(cb_func, cb_data);
}

void gpi_to_user() { LOG_TRACE("Passing control to GPI user"); }

void gpi_to_simulator() {
    if (sim_ending) {
        gpi_finalize();
    }
    LOG_TRACE("Returning control to simulator");
}
