// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

#include "embed.h"

#include <cstdlib>  // getenv

#include "cocotb_utils.h"  // xstr, utils_dyn_open, utils_dyn_sym, DEFER

#ifdef _WIN32
#include <windows.h>  // Win32 API for loading the embed impl library

#include <string>  // string
#endif

#ifndef PYTHON_LIB
#error "Name of Python library required"
#else
#define PYTHON_LIB_STR xstr(PYTHON_LIB)
#endif

#ifndef EMBED_IMPL_LIB
#error "Name of embed implementation library required"
#else
#define EMBED_IMPL_LIB_STR xstr(EMBED_IMPL_LIB)
#endif

static int (*_user_initialize)(int argc, char const *const *argv);
static void (*_user_finalize)();
static void (*_user_start_sim)();
static void (*_user_stop_sim)();

#ifdef _WIN32
static ACTCTX act_ctx = {
    /* cbSize */ sizeof(ACTCTX),
    /* dwFlags */ ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
    /* lpSource */ NULL,
    /* wProcessorArchitecture */ 0,
    /* wLangId */ 0,
    /* lpAssemblyDirectory */ NULL,
    /* lpResourceName */ MAKEINTRESOURCE(1000),
    /* lpApplicationName */ NULL,
    /* hModule */ 0};

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        act_ctx.hModule = hinstDLL;
    }

    return TRUE;
}
#endif

extern "C" int user_initialize(int argc, char const *const *argv) {
    // preload python library
    char const *libpython_path = getenv("LIBPYTHON_LOC");
    if (!libpython_path) {
        // default to libpythonX.X.so
        libpython_path = PYTHON_LIB_STR;
    }

    auto loaded = utils_dyn_open(libpython_path);
    // LCOV_EXCL_START
    if (!loaded) {
        return -1;
    }
    // LCOV_EXCL_STOP
    // TODO do we need to close the dynamic library?

#ifdef _WIN32
    // LCOV_EXCL_START
    if (!act_ctx.hModule) {
        return -1;
    }
    // LCOV_EXCL_STOP

    HANDLE hact_ctx = CreateActCtx(&act_ctx);
    // LCOV_EXCL_START
    if (hact_ctx == INVALID_HANDLE_VALUE) {
        return -1;
    }
    // LCOV_EXCL_STOP
    DEFER(ReleaseActCtx(hact_ctx));

    ULONG_PTR Cookie;
    auto ok = ActivateActCtx(hact_ctx, &Cookie);
    // LCOV_EXCL_START
    if (!ok) {
        return -1;
    }
    // LCOV_EXCL_STOP
    DEFER(DeactivateActCtx(0, Cookie));
#endif

    // load embed implementation library and functions
    void *embed_impl_lib_handle = utils_dyn_open(EMBED_IMPL_LIB_STR);
    // LCOV_EXCL_START
    if (!embed_impl_lib_handle) {
        return -1;
    }
    // LCOV_EXCL_STOP

    _user_initialize = reinterpret_cast<decltype(_user_initialize)>(
        utils_dyn_sym(embed_impl_lib_handle, "_embed_init_python"));
    // LCOV_EXCL_START
    if (!_user_initialize) {
        return -1;
    }
    // LCOV_EXCL_STOP

    _user_finalize = reinterpret_cast<decltype(_user_finalize)>(
        utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_cleanup"));
    // LCOV_EXCL_START
    if (!_user_finalize) {
        return -1;
    }
    // LCOV_EXCL_STOP

    _user_start_sim = reinterpret_cast<decltype(_user_start_sim)>(
        utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_init"));
    // LCOV_EXCL_START
    if (!_user_start_sim) {
        return -1;
    }
    // LCOV_EXCL_STOP

    _user_stop_sim = reinterpret_cast<decltype(_user_stop_sim)>(
        utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_event"));
    // LCOV_EXCL_START
    if (!_user_stop_sim) {
        return -1;
    }
    // LCOV_EXCL_STOP

    // call to embed library impl
    return _user_initialize(argc, argv);
}

extern "C" void user_finalize() { _user_finalize(); }

extern "C" void user_start_sim() { _user_start_sim(); }

extern "C" void user_stop_sim() { _user_stop_sim(); }
