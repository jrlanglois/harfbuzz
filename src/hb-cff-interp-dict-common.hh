/*
 * Copyright © 2018 Adobe Systems Incorporated.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Adobe Author(s): Michiharu Ariza
 */
#ifndef HB_CFF_INTERP_DICT_COMMON_HH
#define HB_CFF_INTERP_DICT_COMMON_HH

#include "hb-common.h"
#include "hb-cff-interp-common.hh"

namespace CFF {

using namespace OT;

/* an opstr and the parsed out dict value(s) */
struct DictVal : OpStr
{
  inline void init (void)
  {
    single_val.set_int (0);
    multi_val.init ();
  }

  inline void fini (void)
  {
    multi_val.fini ();
  }

  Number              single_val;
  hb_vector_t<Number> multi_val;
};

typedef DictVal NumDictVal;

template <typename VAL>
struct DictValues
{
  inline void init (void)
  {
    opStart = 0;
    values.init ();
  }

  inline void fini (void)
  {
    values.fini ();
  }

  inline void addOp (OpCode op, const SubByteStr& substr = SubByteStr ())
  {
    VAL *val = values.push ();
    val->op = op;
    val->str = ByteStr (substr.str, opStart, substr.offset - opStart);
    opStart = substr.offset;
  }

  inline void addOp (OpCode op, const SubByteStr& substr, const VAL &v)
  {
    VAL *val = values.push (v);
    val->op = op;
    val->str = ByteStr (substr.str, opStart, substr.offset - opStart);
    opStart = substr.offset;
  }

  inline bool hasOp (OpCode op) const
  {
    for (unsigned int i = 0; i < getNumValues (); i++)
      if (getValue (i).op == op) return true;
    return false;
  }

  inline unsigned getNumValues (void) const { return values.len; }
  inline const VAL &getValue (unsigned int i) const { return values[i]; }
  inline const VAL &operator [] (unsigned int i) const { return getValue (i); }

  unsigned int       opStart;
  hb_vector_t<VAL>   values;
};

template <typename OPSTR=OpStr>
struct TopDictValues : DictValues<OPSTR>
{
  inline void init (void)
  {
    DictValues<OPSTR>::init ();
    charStringsOffset = 0;
    FDArrayOffset = 0;
  }

  inline void fini (void)
  {
    DictValues<OPSTR>::fini ();
  }

  inline unsigned int calculate_serialized_op_size (const OPSTR& opstr) const
  {
    switch (opstr.op)
    {
      case OpCode_CharStrings:
      case OpCode_FDArray:
        return OpCode_Size (OpCode_longintdict) + 4 + OpCode_Size (opstr.op);

      default:
        return opstr.str.len;
    }
  }

  unsigned int  charStringsOffset;
  unsigned int  FDArrayOffset;
};

struct DictOpSet : OpSet<Number>
{
  static inline bool process_op (OpCode op, InterpEnv<Number>& env)
  {
    switch (op) {
      case OpCode_longintdict:  /* 5-byte integer */
        return env.argStack.push_longint_from_substr (env.substr);
      case OpCode_BCD:  /* real number */
        float v;
        if (unlikely (!env.argStack.check_overflow (1) || !parse_bcd (env.substr, v)))
          return false;
        env.argStack.push_real (v);
        return true;

      default:
        return OpSet<Number>::process_op (op, env);
    }

    return true;
  }

  static inline bool parse_bcd (SubByteStr& substr, float& v)
  {
    v = 0.0f;
    hb_vector_t<char, 64> str;

    str.init ();
    str.push ('x'); /* hack: disguise a varation option string */
    str.push ('=');
    unsigned char byte = 0;
    for (unsigned int i = 0; substr.avail (); i++)
    {
      char c;
      if ((i & 1) == 0)
      {
        byte = substr[0];
        substr.inc ();
        c = (byte & 0xF0) >> 4;
      }
      else
        c = byte & 0x0F;

      if (c == 0x0F)
      {
        hb_variation_t  var;
        if (unlikely (!hb_variation_from_string (&str[0], str.len, &var)))
        {
          str.fini ();
          return false;
        }
        v = var.value;
        str.fini ();
        return true;
      }
      else if (c == 0x0D) return false;
      
      str.push ("0123456789.EE - "[c]);
      if (c == 0x0C)
        str.push ('-');
    }
    str.fini ();
    return false;
  }

  static inline bool is_hint_op (OpCode op)
  {
    switch (op)
    {
      case OpCode_BlueValues:
      case OpCode_OtherBlues:
      case OpCode_FamilyBlues:
      case OpCode_FamilyOtherBlues:
      case OpCode_StemSnapH:
      case OpCode_StemSnapV:
      case OpCode_StdHW:
      case OpCode_StdVW:
      case OpCode_BlueScale:
      case OpCode_BlueShift:
      case OpCode_BlueFuzz:
      case OpCode_ForceBold:
      case OpCode_LanguageGroup:
      case OpCode_ExpansionFactor:
        return true;
      default:
        return false;
    }
  }
};

template <typename VAL=OpStr>
struct TopDictOpSet : DictOpSet
{
  static inline bool process_op (OpCode op, InterpEnv<Number>& env, TopDictValues<VAL> & dictval)
  {
    switch (op) {
      case OpCode_CharStrings:
        if (unlikely (!env.argStack.check_pop_uint (dictval.charStringsOffset)))
          return false;
        env.clear_args ();
        break;
      case OpCode_FDArray:
        if (unlikely (!env.argStack.check_pop_uint (dictval.FDArrayOffset)))
          return false;
        env.clear_args ();
        break;
      default:
        return DictOpSet::process_op (op, env);
    }

    return true;
  }
};

template <typename OPSET, typename PARAM, typename ENV=NumInterpEnv>
struct DictInterpreter : Interpreter<ENV>
{
  inline bool interpret (PARAM& param)
  {
    param.init ();
    do
    {
      OpCode op;
      if (unlikely (!SUPER::env.fetch_op (op) ||
                    !OPSET::process_op (op, SUPER::env, param)))
        return false;
    } while (SUPER::env.substr.avail ());
    
    return true;
  }

  private:
  typedef Interpreter<ENV> SUPER;
};

} /* namespace CFF */

#endif /* HB_CFF_INTERP_DICT_COMMON_HH */
