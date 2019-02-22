/******************************************************************************
* Copyright (c) 2014 Potential Ventures Ltd
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright
*      notice, this list of conditions and the following disclaimer in the
*      documentation and/or other materials provided with the distribution.
*    * Neither the name of Potential Ventures Ltd
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

#ifndef COCOTB_FLI_IMPL_H_
#define COCOTB_FLI_IMPL_H_

#include "../gpi/gpi_priv.h"
#include "mti.h"

#include <queue>
#include <map>

extern "C" {
void cocotb_init(void);
void handle_fli_callback(void *data);
}

class FliImpl;
class FliSignalObjHdl;

// Callback handles

// In FLI some callbacks require us to register a process
// We use a subclass to track the process state related to the callback
class FliProcessCbHdl : public virtual GpiCbHdl {
public:
    FliProcessCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                              m_proc_hdl(NULL),
                                              m_sensitised(false) { }
    virtual ~FliProcessCbHdl() { }

    virtual int arm_callback(void) = 0;
    virtual int cleanup_callback(void);

protected:
    mtiProcessIdT       m_proc_hdl;
    bool                m_sensitised;
};

// One class of callbacks uses mti_Sensitize to react to a signal
class FliSignalCbHdl : public FliProcessCbHdl, public GpiValueCbHdl {

public:
    FliSignalCbHdl(GpiImplInterface *impl,
                   FliSignalObjHdl *sig_hdl,
                   unsigned int edge);

    virtual ~FliSignalCbHdl() { }
    int arm_callback(void);
    int cleanup_callback(void) {
        return FliProcessCbHdl::cleanup_callback();
    }

private:
    mtiSignalIdT        m_sig_hdl;
};

// All other callbacks are related to the simulation phasing
class FliSimPhaseCbHdl : public FliProcessCbHdl {

public:
    FliSimPhaseCbHdl(GpiImplInterface *impl, mtiProcessPriorityT priority) : GpiCbHdl(impl),
                                                                             FliProcessCbHdl(impl),
                                                                             m_priority(priority) { }
    virtual ~FliSimPhaseCbHdl() { }

    int arm_callback(void);

protected:
    mtiProcessPriorityT         m_priority;
};

// FIXME templates?
class FliReadWriteCbHdl : public FliSimPhaseCbHdl {
public:
    FliReadWriteCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                                FliSimPhaseCbHdl(impl, MTI_PROC_SYNCH) { }
    virtual ~FliReadWriteCbHdl() { }
};

class FliNextPhaseCbHdl : public FliSimPhaseCbHdl {
public:
    FliNextPhaseCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                                FliSimPhaseCbHdl(impl, MTI_PROC_IMMEDIATE) { }
    virtual ~FliNextPhaseCbHdl() { }
};

class FliReadOnlyCbHdl : public FliSimPhaseCbHdl {
public:
    FliReadOnlyCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                               FliSimPhaseCbHdl(impl, MTI_PROC_POSTPONED) { }
    virtual ~FliReadOnlyCbHdl() { }
};

class FliStartupCbHdl : public FliProcessCbHdl {
public:
    FliStartupCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                              FliProcessCbHdl(impl) { }
    virtual ~FliStartupCbHdl() { }

    int arm_callback(void);
    int run_callback(void);
};

class FliShutdownCbHdl : public FliProcessCbHdl {
public:
    FliShutdownCbHdl(GpiImplInterface *impl) : GpiCbHdl(impl),
                                               FliProcessCbHdl(impl) { }
    virtual ~FliShutdownCbHdl() { }

    int arm_callback(void);
    int run_callback(void);
};

class FliTimedCbHdl : public FliProcessCbHdl {
public:
    FliTimedCbHdl(GpiImplInterface *impl, uint64_t time_ps);
    virtual ~FliTimedCbHdl() { }

    int arm_callback(void);
    void reset_time(uint64_t new_time) {
        m_time_ps = new_time;
    }
    int cleanup_callback(void);
private:
    uint64_t m_time_ps;
};

typedef enum fli_type_e {
    FLI_TYPE_REGION   = 0,
    FLI_TYPE_SIGNAL   = 1,
    FLI_TYPE_VARIABLE = 2,
} fli_type_t;

typedef mtiInt32T (*mti_GetValue)(void *);
typedef void      (*mti_SetValue)(void *, mtiLongT);
typedef void *    (*mti_GetArrayValue)(void *, void *);
typedef void *    (*mti_GetValueIndirect)(void *, void *);

// Object Handles
class FliObjHdl : public GpiObjHdl {
public:
    FliObjHdl(GpiImplInterface *impl,
              void *hdl,
              gpi_objtype_t objtype) :
                  GpiObjHdl(impl, hdl, objtype, false) { }

    virtual ~FliObjHdl() { }

    virtual int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);
};

class FliSignalObjHdl : public GpiSignalObjHdl {
public:
    FliSignalObjHdl(GpiImplInterface *impl,
                    void *hdl,
                    gpi_objtype_t objtype,
                    bool is_const) :
                        GpiSignalObjHdl(impl, hdl, objtype, is_const),
                        m_rising_cb(NULL),
                        m_falling_cb(NULL),
                        m_either_cb(NULL) { }

    virtual ~FliSignalObjHdl() {
        if (m_rising_cb)
            delete m_rising_cb;
        if (m_falling_cb)
            delete m_falling_cb;
        if (m_either_cb)
            delete m_either_cb;
    }

    virtual GpiCbHdl *value_change_cb(unsigned int edge);
    virtual int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);

protected:
    FliSignalCbHdl      *m_rising_cb;
    FliSignalCbHdl      *m_falling_cb;
    FliSignalCbHdl      *m_either_cb;
    mti_GetValue         m_get_value;
    mti_SetValue         m_set_value;
    mti_GetArrayValue    m_get_array_value;
    mti_GetValueIndirect m_get_value_indirect;
};

class FliValueObjHdl : public FliSignalObjHdl {
public:
    FliValueObjHdl(GpiImplInterface *impl,
                   void *hdl,
                   gpi_objtype_t objtype,
                   bool is_const) :
                       FliSignalObjHdl(impl, hdl, objtype, is_const),
                       m_val_buff(NULL) { }

    virtual ~FliValueObjHdl() {
        if (m_val_buff != NULL)
            free(m_val_buff);
    }

    virtual const char* get_signal_value_binstr(void);
    virtual const char* get_signal_value_str(void);
    virtual double get_signal_value_real(void);
    virtual long get_signal_value_long(void);

    virtual int set_signal_value(const long value);
    virtual int set_signal_value(const double value);
    virtual int set_signal_value(std::string &value);

    virtual int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);

protected:
    char              *m_val_buff;
};

class FliEnumObjHdl : public FliValueObjHdl {
public:
    FliEnumObjHdl(GpiImplInterface *impl,
                  void *hdl,
                  bool is_const) :
                      FliValueObjHdl(impl, hdl, GPI_ENUM, is_const),
                      m_value_enum(NULL),
                      m_num_enum(0) { }

    virtual ~FliEnumObjHdl() { }

    const char* get_signal_value_str(void);
    long get_signal_value_long(void);

    int set_signal_value(const long value);

    int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);

private:
    char             **m_value_enum;    // Do Not Free
    mtiInt32T          m_num_enum;
};

class FliLogicObjHdl : public FliValueObjHdl {
public:
    FliLogicObjHdl(GpiImplInterface *impl,
                   void *hdl,
                   bool is_const) :
                       FliValueObjHdl(impl,
                                      hdl,
                                      GPI_REGISTER,
                                      is_const),
                       m_mti_buff(NULL),
                       m_value_enum(NULL),
                       m_num_enum(0),
                       m_enum_map() { }

    virtual ~FliLogicObjHdl() {
        if (m_mti_buff != NULL)
            free(m_mti_buff);
    }

    const char* get_signal_value_binstr(void);

    int set_signal_value(const long value);
    int set_signal_value(std::string &value);

    int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);


private:
    char                      *m_mti_buff;
    char                     **m_value_enum;    // Do Not Free
    mtiInt32T                  m_num_enum;
    std::map<char,mtiInt32T>   m_enum_map;
};

class FliIntObjHdl : public FliValueObjHdl {
public:
    FliIntObjHdl(GpiImplInterface *impl,
                 void *hdl,
                 bool is_const) :
                     FliValueObjHdl(impl, hdl, GPI_INTEGER, is_const) { }

    virtual ~FliIntObjHdl() { }

    const char* get_signal_value_binstr(void);
    long get_signal_value_long(void);

    int set_signal_value(const long value);

    int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);
};

class FliRealObjHdl : public FliValueObjHdl {
public:
    FliRealObjHdl(GpiImplInterface *impl,
                  void *hdl,
                  bool is_const) :
                      FliValueObjHdl(impl, hdl, GPI_REAL, is_const),
                      m_mti_buff(NULL) { }

    virtual ~FliRealObjHdl() {
        if (m_mti_buff != NULL)
            free(m_mti_buff);
    }

    double get_signal_value_real(void);

    int set_signal_value(const double value);

    int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);

private:
    double *m_mti_buff;
};

class FliStringObjHdl : public FliValueObjHdl {
public:
    FliStringObjHdl(GpiImplInterface *impl,
                  void *hdl,
                  bool is_const) :
                      FliValueObjHdl(impl, hdl, GPI_STRING, is_const),
                      m_mti_buff(NULL) { }

    virtual ~FliStringObjHdl() {
        if (m_mti_buff != NULL)
            free(m_mti_buff);
    }

    const char* get_signal_value_str(void);

    int set_signal_value(std::string &value);

    int initialise(std::string &name, std::string &fq_name, fli_type_t fli_type);

private:
    char *m_mti_buff;
};

class FliTimerCache {
public:
    FliTimerCache(FliImpl* impl) : impl(impl) { }
    ~FliTimerCache() { }

    FliTimedCbHdl* get_timer(uint64_t time_ps);
    void put_timer(FliTimedCbHdl*);

private:
    std::queue<FliTimedCbHdl*> free_list;
    FliImpl *impl;
};

class FliIterator : public GpiIterator {
public:
    enum OneToMany {
        OTM_END = 0,
        OTM_CONSTANTS,  // include Generics
        OTM_SIGNALS,
        OTM_REGIONS,
        OTM_SIGNAL_SUB_ELEMENTS,
        OTM_VARIABLE_SUB_ELEMENTS
    };

    FliIterator(GpiImplInterface *impl, GpiObjHdl *hdl);

    virtual ~FliIterator() { };

    Status next_handle(std::string &name, GpiObjHdl **hdl, void **raw_hdl);

private:
    void populate_handle_list(OneToMany childType);

private:
    static GpiIteratorMapping<int, OneToMany> iterate_over;      /* Possible mappings */
    std::vector<OneToMany> *selected;                            /* Mapping currently in use */
    std::vector<OneToMany>::iterator one2many;

    std::vector<void *> m_vars;
    std::vector<void *> m_sigs;
    std::vector<void *> m_regs;
    std::vector<void *> *m_currentHandles;
    std::vector<void *>::iterator m_iterator;
};

class FliImpl : public GpiImplInterface {
public:
    FliImpl(const std::string& name) : GpiImplInterface(name),
                                       cache(this),
                                       m_readonly_cbhdl(this),
                                       m_nexttime_cbhdl(this),
                                       m_readwrite_cbhdl(this) { }

     /* Sim related */
    void sim_end(void);
    void get_sim_time(uint32_t *high, uint32_t *low);
    void get_sim_precision(int32_t *precision);

    /* Hierachy related */
    GpiObjHdl* native_check_create(std::string &name, GpiObjHdl *parent);
    GpiObjHdl* native_check_create(int32_t index, GpiObjHdl *parent);
    GpiObjHdl* native_check_create(void *raw_hdl, GpiObjHdl *paret);
    GpiObjHdl *get_root_handle(const char *name);
    GpiIterator *iterate_handle(GpiObjHdl *obj_hdl, gpi_iterator_sel_t type);

    /* Callback related, these may (will) return the same handle*/
    GpiCbHdl *register_timed_callback(uint64_t time_ps);
    GpiCbHdl *register_readonly_callback(void);
    GpiCbHdl *register_nexttime_callback(void);
    GpiCbHdl *register_readwrite_callback(void);
    int deregister_callback(GpiCbHdl *obj_hdl);

    /* Method to provide strings from operation types */
    const char *reason_to_string(int reason);

    /* Method to provide strings from operation types */
    GpiObjHdl *create_gpi_obj_from_handle(void *hdl, std::string &name, std::string &fq_name);

public:
    FliTimerCache cache;

private:
    FliReadOnlyCbHdl  m_readonly_cbhdl;
    FliNextPhaseCbHdl m_nexttime_cbhdl;
    FliReadWriteCbHdl m_readwrite_cbhdl;
};

#endif /*COCOTB_FLI_IMPL_H_ */
