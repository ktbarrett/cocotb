// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

#include <embed.h>
#include <cocotb_utils.h>   // xstr, utils_dyn_open, utils_dyn_sym
#include <gpi.h>            // gpi_event_t

#ifndef PYTHON_SO_LIB
#error "Python version needs passing in with -DPYTHON_SO_LIB=libpython<ver>.so"
#else
#define PYTHON_SO_LIB_STR xstr(PYTHON_SO_LIB)
#endif

#ifndef EMBED_IMPL_LIB
#error "Name of embed implementation library required"
#else
#define EMBED_IMPL_LIB_STR xstr(EMBED_IMPL_LIB)
#endif


static void (*_embed_init_python)(void);
static void (*_embed_sim_cleanup)(void);
static int (*_embed_sim_init)(int argc, char const * const * argv);
static void (*_embed_sim_event)(gpi_event_t level, const char *msg);


extern "C" void embed_init_python(void)
{
    // preload python library
    utils_dyn_open(PYTHON_SO_LIB_STR);

    // load embed implementation library and functions
    auto embed_impl_lib_handle = utils_dyn_open(EMBED_IMPL_LIB_STR);
    _embed_init_python = reinterpret_cast<decltype(_embed_init_python)>(utils_dyn_sym(embed_impl_lib_handle, "_embed_init_python"));
    _embed_sim_cleanup = reinterpret_cast<decltype(_embed_sim_cleanup)>(utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_cleanup"));
    _embed_sim_init = reinterpret_cast<decltype(_embed_sim_init)>(utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_init"));
    _embed_sim_event = reinterpret_cast<decltype(_embed_sim_event)>(utils_dyn_sym(embed_impl_lib_handle, "_embed_sim_event"));

    // call to embed library impl
    _embed_init_python();
}


extern "C" void embed_sim_cleanup(void)
{
    _embed_sim_cleanup();
}


extern "C" int embed_sim_init(int argc, char const * const * argv)
{
    return _embed_sim_init(argc, argv);
}


extern "C" void embed_sim_event(gpi_event_t level, const char *msg)
{
    _embed_sim_event(level, msg);
}
