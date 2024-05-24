/* Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/*
  Parse tree node classes for optimizer hint syntax
*/


#ifndef PARSE_TREE_HINTS_INCLUDED
#define PARSE_TREE_HINTS_INCLUDED

#include "my_global.h"
#include "my_config.h"
#include "sql_alloc.h"
#include "sql_list.h"
#include "mem_root_array.h"
#include "opt_hints.h"

struct LEX;

/**
  Environment data for the contextualization phase
*/
struct Parse_context {
  THD * const thd;              ///< Current thread handler
  MEM_ROOT *mem_root;           ///< Current MEM_ROOT
  st_select_lex * select;       ///< Current SELECT_LEX object

  Parse_context(THD *thd, st_select_lex *select);
};

struct Hint_param_table
{
  LEX_CSTRING table;
  LEX_CSTRING opt_query_block;
};


class PT_hint : public Sql_alloc
{
  opt_hints_enum hint_type; // Hint type
  bool state;                    // true if hints is on, false otherwise
public:
  PT_hint(opt_hints_enum hint_type_arg, bool switch_state_arg)
    : hint_type(hint_type_arg), state(switch_state_arg)
  {}

  opt_hints_enum type() const { return hint_type; }
  bool switch_on() const { return state; }
  /**
    Print warning issuing in processing of the hint.

    @param thd             Pointer to THD object
    @param err_code        Error code
    @param qb_name_arg     QB name
    @param table_name_arg  table name
    @param key_name_arg    key name
    @param hint            Pointer to hint object
  */
  virtual void print_warn(THD *thd, uint err_code,
                          const LEX_CSTRING *qb_name_arg,
                          LEX_CSTRING *table_name_arg,
                          LEX_CSTRING *key_name_arg,
                          PT_hint *hint) const;
  /**
    Append additional hint arguments.

    @param thd             Pointer to THD object
    @param str             Pointer to String object
  */
  virtual void append_args(THD *thd, String *str) const {}

  /**
    Do all context-sensitive things and mark the node as contextualized

    @param      pc      current parse context

    @retval     false   success
    @retval     true    syntax/OOM/etc error
  */
  virtual bool contextualize(Parse_context *pc) { return false; } // OLEGS: differs from original

  virtual ~PT_hint()= default;
};


class PT_hint_list : public Sql_alloc
{
  Mem_root_array<PT_hint *, true> hints;

public:
  explicit PT_hint_list(MEM_ROOT *mem_root) : hints(mem_root) {}

  virtual bool contextualize(Parse_context *pc);

  bool push_back(PT_hint *hint) { return hints.push_back(hint); }

  virtual ~PT_hint_list()= default;
};


class Hint_param_table_list : public Sql_alloc
{
  Mem_root_array<Hint_param_table, true> tables;

public:
  Hint_param_table_list (MEM_ROOT *mem_root)
    : tables(mem_root)
  {}

  bool push_back(Hint_param_table *table) { return tables.push_back(*table); }
};


class Hint_param_index_list : public Sql_alloc
{
  Mem_root_array<LEX_CSTRING, true> indexes;

public:
  Hint_param_index_list (MEM_ROOT *mem_root)
    : indexes(mem_root)
  {}

  bool push_back(LEX_CSTRING *index) { return indexes.push_back(*index); }
};


/**
  Parse tree hint object for table level hints.
*/

class PT_table_level_hint : public PT_hint
{
  const LEX_CSTRING qb_name;
  Hint_param_table_list table_list;

  typedef PT_hint super;
public:
  PT_table_level_hint(const LEX_CSTRING qb_name_arg, // OLEGS: passed by value?
                      const Hint_param_table_list &table_list_arg, // OLEGS: copied?
                      bool switch_state_arg,
                      opt_hints_enum hint_type_arg)
    : PT_hint(hint_type_arg, switch_state_arg),
      qb_name(qb_name_arg), table_list(table_list_arg)
  {}

  /**
    Function handles table level hint. It also creates
    table hint object (Opt_hints_table) if it does not
    exist.

    @param pc  Pointer to Parse_context object

    @return  true in case of error,
             false otherwise
  */
  virtual bool contextualize(Parse_context *pc);
};


/**
  Parse tree hint object for key level hints.
*/

class PT_key_level_hint : public PT_hint
{
  Hint_param_table table_name;
  Hint_param_index_list key_list;

  typedef PT_hint super;
public:
  PT_key_level_hint(Hint_param_table &table_name_arg, // OLEGS: copied?
                    const Hint_param_index_list &key_list_arg, // OLEGS: copied?
                    bool switch_state_arg,
                    opt_hints_enum hint_type_arg)
    : PT_hint(hint_type_arg, switch_state_arg),
      table_name(table_name_arg), key_list(key_list_arg)
  {}

  /**
    Function handles key level hint.
    It also creates key hint object
    (Opt_hints_key) if it does not
    exist.

    @param pc  Pointer to Parse_context object

    @return  true in case of error,
             false otherwise
  */
  virtual bool contextualize(Parse_context *pc);
};


/**
  Parse tree hint object for QB_NAME hint.
*/

class PT_hint_qb_name : public PT_hint
{
  const LEX_CSTRING qb_name;

  typedef PT_hint super;
public:
  PT_hint_qb_name(const LEX_CSTRING qb_name_arg) // OLEGS: copied?
    : PT_hint(QB_NAME_HINT_ENUM, true), qb_name(qb_name_arg)
  {}

  /**
    Function sets query block name.

    @param pc  Pointer to Parse_context object

    @return  true in case of error,
             false otherwise
  */
  virtual bool contextualize(Parse_context *pc);
  virtual void append_args(THD *thd, String *str) const
  {
    append_identifier(thd, str, qb_name.str, qb_name.length);
  }

};


class PT_hint_debug1 : public PT_hint
{
   const LEX_CSTRING opt_qb_name;
   Hint_param_table_list *table_list;

public:
  PT_hint_debug1(const LEX_CSTRING &opt_qb_name_arg,
                 Hint_param_table_list *table_list_arg)
  : PT_hint(DEBUG_HINT_ENUM, true),
    opt_qb_name(opt_qb_name_arg),
    table_list(table_list_arg)
  {}
};


class PT_hint_debug2 : public PT_hint
{
  Hint_param_index_list *opt_index_list;

public:
  explicit
  PT_hint_debug2(Hint_param_index_list *opt_index_list_arg)
  : PT_hint(DEBUG_HINT_ENUM, true),
    opt_index_list(opt_index_list_arg)
  {}
};



#endif /* PARSE_TREE_HINTS_INCLUDED */
