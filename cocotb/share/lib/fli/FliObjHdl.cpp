/******************************************************************************
* Copyright (c) 2015/16 Potential Ventures Ltd
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

#include <bitset>
#include <vector>

#include "FliImpl.h"
#include "acc_vhdl.h"

int FliObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    char * str;

    switch (get_type()) {
        case GPI_GENARRAY:
            m_indexable = true;
        case GPI_MODULE:
            m_num_elems = 1;
            break;
        default:
            LOG_CRITICAL("Invalid object type for FliObjHdl. (%s (%s))", name.c_str(), get_type_str());
            return -1;
    }

    str = mti_GetPrimaryName(get_handle<mtiRegionIdT>());
    if (str != NULL)
        m_definition_name = str;

    str = mti_GetRegionSourceName(get_handle<mtiRegionIdT>());
    if (str != NULL)
        m_definition_file = str;

    return GpiObjHdl::initialise(name, fq_name);
}


int FliSignalObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    if (fli_type == FLI_TYPE_SIGNAL) {
        if ((m_rising_cb = new FliSignalCbHdl(m_impl, this, GPI_RISING)) == NULL) {
            LOG_CRITICAL("Failed to allocate memory for Rising-Edge Callback. (%s (%s))", name.c_str(), get_type_str());
            return -1;
        }
        if ((m_falling_cb = new FliSignalCbHdl(m_impl, this, GPI_FALLING)) == NULL) {
            LOG_CRITICAL("Failed to allocate memory for Falling-Edge Callback. (%s (%s))", name.c_str(), get_type_str());
            return -1;
        }
        if ((m_either_cb = new FliSignalCbHdl(m_impl, this, GPI_FALLING | GPI_RISING)) == NULL) {
            LOG_CRITICAL("Failed to allocate memory for Edge Callback. (%s (%s))", name.c_str(), get_type_str());
            return -1;
        }

        m_get_value = (mti_GetValue)&mti_GetSignalValue;
        m_set_value = (mti_SetValue)&mti_SetSignalValue;
        m_get_array_value = (mti_GetArrayValue)&mti_GetArraySignalValue;
        m_get_value_indirect = (mti_GetValueIndirect)&mti_GetSignalValueIndirect;
    } else {
        m_get_value = (mti_GetValue)&mti_GetVarValue;
        m_set_value = (mti_SetValue)&mti_SetVarValue;
        m_get_array_value = (mti_GetArrayValue)&mti_GetArrayVarValue;
        m_get_value_indirect = (mti_GetValueIndirect)&mti_GetVarValueIndirect;

    }
    return GpiObjHdl::initialise(name, fq_name);
}

GpiCbHdl *FliSignalObjHdl::value_change_cb(unsigned int edge)
{
    FliSignalCbHdl *cb = NULL;

    switch (edge) {
        case 1:
            cb = m_rising_cb;
            break;
        case 2:
            cb = m_falling_cb;
            break;
        case 3:
            cb = m_either_cb;
            break;
        default:
            return NULL;
    }

    if (cb == NULL) {
        return NULL;
    }

    if (cb->arm_callback()) {
        return NULL;
    }

    return (GpiCbHdl *)cb;
}

int FliValueObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    mtiTypeIdT _type = (fli_type == FLI_TYPE_SIGNAL) ? mti_GetSignalType(get_handle<mtiSignalIdT>()) : mti_GetVarType(get_handle<mtiVariableIdT>());

    if (get_type() == GPI_ARRAY) {
        m_range_left  = mti_TickLeft(_type);
        m_range_right = mti_TickRight(_type);
        m_num_elems   = mti_TickLength(_type);
        m_indexable   = true;
    } else if (get_type() == GPI_STRUCTURE) {
        m_num_elems = mti_GetNumRecordElements(_type);
    }

    return FliSignalObjHdl::initialise(name, fq_name, fli_type);
}

const char* FliValueObjHdl::get_signal_value_binstr(void)
{
    LOG_ERROR("Getting signal/variable value as binstr not supported for %s of type %d", m_fullname.c_str(), m_type);
    return NULL;
}

const char* FliValueObjHdl::get_signal_value_str(void)
{
    LOG_ERROR("Getting signal/variable value as str not supported for %s of type %d", m_fullname.c_str(), m_type);
    return NULL;
}

double FliValueObjHdl::get_signal_value_real(void)
{
    LOG_ERROR("Getting signal/variable value as double not supported for %s of type %d", m_fullname.c_str(), m_type);
    return -1;
}

long FliValueObjHdl::get_signal_value_long(void)
{
    LOG_ERROR("Getting signal/variable value as long not supported for %s of type %d", m_fullname.c_str(), m_type);
    return -1;
}

int FliValueObjHdl::set_signal_value(const long value)
{
    LOG_ERROR("Setting signal/variable value via long not supported for %s of type %d", m_fullname.c_str(), m_type);
    return -1;
}

int FliValueObjHdl::set_signal_value(std::string &value)
{
    LOG_ERROR("Setting signal/variable value via string not supported for %s of type %d", m_fullname.c_str(), m_type);
    return -1;
}

int FliValueObjHdl::set_signal_value(const double value)
{
    LOG_ERROR("Setting signal/variable value via double not supported for %s of type %d", m_fullname.c_str(), m_type);
    return -1;
}

int FliEnumObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    mtiTypeIdT _type = (fli_type == FLI_TYPE_SIGNAL) ? mti_GetSignalType(get_handle<mtiSignalIdT>()) : mti_GetVarType(get_handle<mtiVariableIdT>());

    m_num_elems   = 1;
    m_value_enum  = mti_GetEnumValues(_type);
    m_num_enum    = mti_TickLength(_type);

    return FliValueObjHdl::initialise(name, fq_name, fli_type);
}

const char* FliEnumObjHdl::get_signal_value_str(void)
{
    return m_value_enum[m_get_value(get_handle<void *>())];
}

long FliEnumObjHdl::get_signal_value_long(void)
{
    return (long)m_get_value(get_handle<void *>());
}

int FliEnumObjHdl::set_signal_value(const long value)
{
    if (value > m_num_enum || value < 0) {
        LOG_ERROR("Attempted to set a enum with range [0,%d] with invalid value %d!\n", m_num_enum, value);
        return -1;
    }

    m_set_value(get_handle<void *>(), value);

    return 0;
}

int FliLogicObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    mtiTypeIdT _type = (fli_type == FLI_TYPE_SIGNAL) ? mti_GetSignalType(get_handle<mtiSignalIdT>()) : mti_GetVarType(get_handle<mtiVariableIdT>());

    switch (mti_GetTypeKind(_type)) {
        case MTI_TYPE_ENUM:
            m_num_elems   = 1;
            m_value_enum  = mti_GetEnumValues(_type);
            m_num_enum    = mti_TickLength(_type);
            break;
        case MTI_TYPE_ARRAY: {
                mtiTypeIdT elemType = mti_GetArrayElementType(_type);

                m_range_left  = mti_TickLeft(_type);
                m_range_right = mti_TickRight(_type);
                m_num_elems   = mti_TickLength(_type);
                m_indexable   = true;

                m_value_enum  = mti_GetEnumValues(elemType);
                m_num_enum    = mti_TickLength(elemType);

                m_mti_buff    = (char*)malloc(sizeof(*m_mti_buff) * m_num_elems);
                if (!m_mti_buff) {
                    LOG_CRITICAL("Unable to alloc mem for value object mti read buffer: ABORTING");
                    return -1;
                }
            }
            break;
        default:
            LOG_CRITICAL("Object type is not 'logic' for %s (%d)", name.c_str(), mti_GetTypeKind(_type));
            return -1;
    }

    for (mtiInt32T i = 0; i < m_num_enum; i++) {
        m_enum_map[m_value_enum[i][1]] = i;  // enum is of the format 'U' or '0', etc.
    }

    m_val_buff = (char*)malloc(m_num_elems+1);
    if (!m_val_buff) {
        LOG_CRITICAL("Unable to alloc mem for value object read buffer: ABORTING");
    }
    m_val_buff[m_num_elems] = '\0';

    return FliValueObjHdl::initialise(name, fq_name, fli_type);
}

const char* FliLogicObjHdl::get_signal_value_binstr(void)
{
    if (!m_indexable) {
        m_val_buff[0] = m_value_enum[m_get_value(get_handle<void *>())][1];
    } else {
        m_get_array_value(get_handle<void *>(), m_mti_buff);

        for (int i = 0; i < m_num_elems; i++ ) {
            m_val_buff[i] = m_value_enum[(int)m_mti_buff[i]][1];
        }
    }

    LOG_DEBUG("Retrieved \"%s\" for value object %s", m_val_buff, m_name.c_str());

    return m_val_buff;
}

int FliLogicObjHdl::set_signal_value(const long value)
{
    if (!m_indexable) {
        mtiInt32T enumVal = value ? m_enum_map['1'] : m_enum_map['0'];

        m_set_value(get_handle<void *>(), enumVal);
    } else {
        LOG_DEBUG("set_signal_value(long)::0x%016x", value);
        for (int i = 0, idx = m_num_elems-1; i < m_num_elems; i++, idx--) {
            mtiInt32T enumVal = value&(1L<<i) ? m_enum_map['1'] : m_enum_map['0'];

            m_mti_buff[idx] = (char)enumVal;
        }

        m_set_value(get_handle<void *>(), (mtiLongT)m_mti_buff);
    }

    return 0;
}

int FliLogicObjHdl::set_signal_value(std::string &value)
{
    if (!m_indexable) {
        mtiInt32T enumVal = m_enum_map[value.c_str()[0]];

        m_set_value(get_handle<void *>(), enumVal);
    } else {

        if ((int)value.length() != m_num_elems) {
            LOG_ERROR("FLI: Unable to set logic vector due to the string having incorrect length.  Length of %d needs to be %d", value.length(), m_num_elems);
            return -1;
        }

        LOG_DEBUG("set_signal_value(string)::%s", value.c_str());

        mtiInt32T enumVal;
        std::string::iterator valIter;
        int i = 0;

        for (valIter = value.begin(); (valIter != value.end()) && (i < m_num_elems); valIter++, i++) {
            enumVal = m_enum_map[*valIter];
            m_mti_buff[i] = (char)enumVal;
        }

        m_set_value(get_handle<void *>(), (mtiLongT)m_mti_buff);
    }

    return 0;
}

int FliIntObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    m_num_elems   = 1;

    m_val_buff = (char*)malloc(33);  // Integers are always 32-bits
    if (!m_val_buff) {
        LOG_CRITICAL("Unable to alloc mem for value object read buffer: ABORTING");
        return -1;
    }
    m_val_buff[m_num_elems] = '\0';

    return FliValueObjHdl::initialise(name, fq_name, fli_type);
}

const char* FliIntObjHdl::get_signal_value_binstr(void)
{
    mtiInt32T val;

    val = m_get_value(get_handle<void *>());

    std::bitset<32> value((unsigned long)val);
    std::string bin_str = value.to_string<char,std::string::traits_type, std::string::allocator_type>();
    snprintf(m_val_buff, 33, "%s", bin_str.c_str());

    return m_val_buff;
}

long FliIntObjHdl::get_signal_value_long(void)
{
    mtiInt32T value;

    value = m_get_value(get_handle<void *>());

    return (long)value;
}

int FliIntObjHdl::set_signal_value(const long value)
{
    m_set_value(get_handle<void *>(), value);
    return 0;
}

int FliRealObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{

    m_num_elems   = 1;

    m_mti_buff    = (double*)malloc(sizeof(double));
    if (!m_mti_buff) {
        LOG_CRITICAL("Unable to alloc mem for value object mti read buffer: ABORTING");
        return -1;
    }

    return FliValueObjHdl::initialise(name, fq_name, fli_type);
}

double FliRealObjHdl::get_signal_value_real(void)
{
    m_get_value_indirect(get_handle<void *>(), m_mti_buff);

    LOG_DEBUG("Retrieved \"%f\" for value object %s", m_mti_buff[0], m_name.c_str());

    return m_mti_buff[0];
}

int FliRealObjHdl::set_signal_value(const double value)
{
    m_mti_buff[0] = value;

    m_set_value(get_handle<void *>(), (mtiLongT)m_mti_buff);

    return 0;
}

int FliStringObjHdl::initialise(std::string &name, std::string &fq_name, fli_type_t fli_type)
{
    mtiTypeIdT _type = (fli_type == FLI_TYPE_SIGNAL) ? mti_GetSignalType(get_handle<mtiSignalIdT>()) : mti_GetVarType(get_handle<mtiVariableIdT>());

    m_range_left  = mti_TickLeft(_type);
    m_range_right = mti_TickRight(_type);
    m_num_elems   = mti_TickLength(_type);
    m_indexable   = true;

    m_mti_buff    = (char*)malloc(sizeof(char) * m_num_elems);
    if (!m_mti_buff) {
        LOG_CRITICAL("Unable to alloc mem for value object mti read buffer: ABORTING");
        return -1;
    }

    m_val_buff = (char*)malloc(m_num_elems+1);
    if (!m_val_buff) {
        LOG_CRITICAL("Unable to alloc mem for value object read buffer: ABORTING");
        return -1;
    }
    m_val_buff[m_num_elems] = '\0';

    return FliValueObjHdl::initialise(name, fq_name, fli_type);
}

const char* FliStringObjHdl::get_signal_value_str(void)
{
    m_get_array_value(get_handle<void *>(), m_mti_buff);

    strncpy(m_val_buff, m_mti_buff, m_num_elems);

    LOG_DEBUG("Retrieved \"%s\" for value object %s", m_val_buff, m_name.c_str());

    return m_val_buff;
}

int FliStringObjHdl::set_signal_value(std::string &value)
{
    strncpy(m_mti_buff, value.c_str(), m_num_elems);

    m_set_value(get_handle<void *>(), (mtiLongT)m_mti_buff);

    return 0;
}

