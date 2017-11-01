/* Copyright 2012-2017 Dustin DeWeese
   This file is part of PoprC.

    PoprC is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PoprC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PoprC.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rt_types.h"

#include "startle/error.h"
#include "startle/test.h"
#include "startle/log.h"

#include "cells.h"
#include "rt.h"
#include "special.h"
#include "trace.h"
#include "list.h"

bool func_value(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  cell_t *res = NULL;
  PRE(c, value);
  stats.reduce_cnt--;

  if(FLAG(c->value.type, T_FAIL) ||
     !type_match(treq.t, c)) {
    if(treq.t == T_FUNCTION && c == &nil_cell) return true; // HACK
    goto fail;
  }

  // NOTE: may create multiple placeholder
  // TODO use rows to work around this
  if(is_var(c)) {
    trace_dep(c);
    if(is_any(c)) {
      if(treq.t == T_LIST) {
        res = var_create(T_LIST, c->value.tc, treq.in, treq.out);
        res->value.alt_set = c->value.alt_set;
        res->alt = c->alt;
        drop(c);
        *cp = res;
      } else if(treq.t != T_ANY) {
        c->value.type.exclusive = treq.t;
        trace_update(c, c);
      }
    }
  } else if(is_row_list(c)) {
    placeholder_extend(cp, treq.in, treq.out);
  }
  return true;
fail:
  fail(cp, treq);
  return false;
}

cell_t *int_val(val_t x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type.exclusive = T_INT;
  c->value.integer[0] = x;
  return c;
}

cell_t *float_val(double x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type.exclusive = T_FLOAT;
  c->value.flt[0] = x;
  return c;
}

cell_t *symbol(val_t sym) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type.exclusive = T_SYMBOL;
  c->value.integer[0] = sym;
  return c;
}

bool is_value(cell_t const *c) {
  return c && c->func == func_value;
}

extern reduce_t func_assert;

void placeholder_extend(cell_t **lp, int in, int out) {
  cell_t *l = *lp;
  if(!is_row_list(l)) return;
  csize_t
    f_in = function_in(l),
    f_out = function_out(l, false),
    d_in = in - min(in, f_in),
    d_out = out - min(out, f_out);
  if(d_in == 0 && d_out == 0) return;
  cell_t **left = leftmost_row(&l);
  if(!left) return;
  // HACK need to map_assert after extending
  if((*left)->func == func_assert) reduce(left, REQ(any));
  cell_t *f = *left;
  if(!(is_function(f) || is_placeholder(f))) return;
  cell_t *ph = func(func_placeholder, d_in + 1, d_out + 1);

  if(l->n) {
    l->n--;
    l = copy(l);
    TRAVERSE_REF(l, alt, ptrs);
    left = leftmost_row(&l);
    f = *left;
  }
  LOG("placeholder_extend: (%d, %d) %d -> %d", in, out, (*lp)-cells, l-cells);

  if(d_out) {
    cell_t *l_exp = make_list(d_out + 1);
    l_exp->value.type.flags = T_ROW;
    COUNTUP(i, d_out) {
      cell_t *d = dep(ph);
      l_exp->value.ptr[i] = d;
      arg(ph, d);
    }
    *left = l_exp;
    left = &l_exp->value.ptr[d_out];
  }

  arg(ph, f);
  refn(ph, d_out);
  *left = ph;
  *lp = l;
}

cell_t *var_create(int t, trace_cell_t tc, int in, int out) {
  return t == T_LIST ?
    var_create_list(var_create_nonlist(T_FUNCTION, tc), in, out, 0) :
    var_create_nonlist(t, tc);
}

cell_t *var_create_nonlist(int t, trace_cell_t tc) {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->size = 2;
  c->value.tc = tc;
  c->value.type.flags = T_VAR;
  c->value.type.exclusive = t;
  trace_update_type(c);
  return c;
}

cell_t *var_create_list(cell_t *f, int in, int out, int shift) {
  cell_t *c = make_list(out + shift + 1);
  cell_t *ph = func(func_placeholder, in + 1, out + 1);
  cell_t **a = &c->value.ptr[shift];
  COUNTUP(i, out) {
    cell_t *d = dep(ph);
    a[i] = d;
    arg(ph, d);
  }
  arg(ph, f);
  refn(ph, out);
  a[out] = ph;
  c->value.type.flags = T_ROW;
  return c;
}

cell_t *var_create_with_entry(int t, cell_t *entry, csize_t size) {
  assert_error(entry);
  int ix = trace_alloc(entry, size);
  return var_create(t, (trace_cell_t) {entry, ix}, 0, 0);
}

cell_t *var_(uint8_t t, cell_t *c, uint8_t pos) {
  assert_error(c);
  cell_t *entry = trace_expr_entry(pos);
  TRAVERSE(c, in) {
    cell_t *a = clear_ptr(*p);
    if(a && is_var(a)) {
      // inherit entry with highest pos
      cell_t *e = a->value.tc.entry;
      if(e && e->pos > pos) {
        pos = e->pos;
        entry = e;
      }
    }
  }

  return var_create_with_entry(t, entry, c->size);
}

#if INTERFACE
#define var(...) DISPATCH(var, 3, __VA_ARGS__)
#define var_0(t, c, pos, ...) var_(t, c, pos)
#define var_1(t, c, ...) var_(t, c, 0)
#endif

bool is_var(cell_t const *c) {
  return c && is_value(c) && FLAG(c->value.type, T_VAR);
}

cell_t *vector(csize_t n) {
  cell_t *c = closure_alloc(n+1);
  c->func = func_value;
  c->value.type.exclusive = T_ANY;
  return c;
}

cell_t *make_map(csize_t s) {
  csize_t cs = calculate_map_size(s);
  cell_t *c = closure_alloc_cells(cs);
  uintptr_t size = (sizeof(cell_t) * cs - offsetof(cell_t, value.map)) / sizeof(pair_t) - 1;
  c->func = func_value;
  c->size = 2 * (size + 1) + 1;
  c->value.type.exclusive = T_MAP;
  c->value.map[0].first = size;
  c->value.map[0].second = 0;
  return c;
}

bool is_map(cell_t const *c) {
  return c && is_value(c) && c->value.type.exclusive == T_MAP;
}

cell_t *make_string(seg_t s) {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->value.type.exclusive = T_STRING;
  c->value.str = s;
  return c;
}

bool is_string(cell_t const *c) {
  return c && is_value(c) && c->value.type.exclusive == T_STRING;
}

bool is_dep_of(cell_t *d, cell_t *c) {
  bool ret = false;
  TRAVERSE(c, out) {
    if(*p == d) ret = true;
  }
  return ret;
}

/* todo: propagate types here */
bool func_dep(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  PRE(c, dep);
  /* rely on another cell for reduction */
  /* don't need to drop arg, handled by other function */
  /* must temporarily reference to avoid replacement of p which is referenced elsewhere */
  cell_t *p = ref(c->expr.arg[0]);
  assert_error(is_dep_of(c, p));
  insert_root(&p);
  c->func = func_dep_entered;
  reduce_dep(&p);
  trace_dep(c);
  assert_error(c->func != func_dep_entered);
  remove_root(&p);
  drop(p);
  return false;
}

bool func_dep_entered(cell_t **cp, type_request_t treq) {
  // shouldn't happen; circular dependency
  assert_error(false);
  fail(cp, treq);
  return false;
}

cell_t *dep(cell_t *c) {
  cell_t *n = closure_alloc(1);
  n->func = func_dep;
  n->expr.arg[0] = c;
  return n;
}

bool is_dep(cell_t const *c) {
  return c->func == func_dep || c->func == func_dep_entered;
}

// this shouldn't reduced directly, but is called through reduce_partial from func_dep
// WORD("??", placeholder, 0, 1)
bool func_placeholder(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  PRE(c, placeholder);
  if(!check_type(treq.t, T_FUNCTION)) goto fail;
  csize_t in = closure_in(c), n = closure_args(c);

  if(n == 1) {
    *cp = ref(c->expr.arg[0]);
    drop(c);
    return false;
  }

  alt_set_t alt_set = 0;
  assert_error(in >= 1);
  if(!reduce_arg(c, in - 1, &alt_set, REQ(function))) goto fail;
  COUNTUP(i, in - 1) {
    if(!reduce_arg(c, i, &alt_set, REQ(any)) ||
      as_conflict(alt_set)) goto fail;
  }
  clear_flags(c);

  // compose X [] --> X
  if(in == 2 &&
     is_list(c->expr.arg[1]) &&
     list_size(c->expr.arg[1]) == 0 &&
     is_function(c->expr.arg[0])) {
    store_reduced(cp, mod_alt(ref(c->expr.arg[0]), c->alt, alt_set));
    return true;
  }

  cell_t *res = var(T_FUNCTION, c, treq.pos);
  res->alt = c->alt;
  RANGEUP(i, in, n) {
    cell_t *d = c->expr.arg[i];
    if(d && is_dep(d)) {
      drop(c);
      d->expr.arg[0] = res;
      store_dep(d, res->value.tc, i, T_ANY);
    } else {
      LOG("dropped placeholder[%C] output", c);
    }
  }
  store_reduced(cp, res);
  ASSERT_REF();
  return true;

 fail:
  fail(cp, treq);
  return false;
}

bool is_placeholder(cell_t const *c) {
  return c && c->func == func_placeholder;
}

bool func_fail(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  PRE(c, fail);
  stats.reduce_cnt--;
  fail(cp, treq);
  return false;
}
