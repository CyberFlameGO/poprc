/* Copyright 2012-2016 Dustin DeWeese
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

#include <assert.h>
#include "rt_types.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/special.h"
#include "gen/byte_compile.h"
#include "gen/test.h"

bool func_value(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  cell_t *res = NULL;
  assert(!is_marked(c));
  measure.reduce_cnt--;

  if((c->value.type.flags & T_FAIL) ||
     !type_match(treq.t, c)) goto fail;

  if(is_var(c)) {
    if(is_any(c)) {
      if(treq.t == T_LIST) {
        res = var_create(T_LIST, c->value.ptr[0], treq.in, treq.out);
        res->value.alt_set = c->value.alt_set;
        res->alt = c->alt;
      } else {
        c->value.type.exclusive = treq.t;
        trace_update(c, c);
      }
    } else if(is_list(c)) {
      csize_t
        in = function_in(c),
        out = function_out(c);
      if(treq.out > out || treq.in > in) {
      cell_t *f = c->value.ptr[in];
      res = var_create_list(ref(f), treq.in - in, treq.out - out, out);
      COUNTUP(i, out) {
        res->value.ptr[i] = ref(c->value.ptr[i]);
      }
      res->value.alt_set = c->value.alt_set;
      res->alt = c->alt;
      }
    }
    if(res) {
      // HACK
      // may result in multiple placeholder extensions,
      // but a value cannot be expanded without breaking
      // references; value -> id won't work (reduce_lazy)
      // because values must remain values.
      drop(c);
      *cp = res;
    }
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

void placeholder_extend(cell_t **lp, int in, int out) {
  cell_t *l = *lp;
  if(!is_var(l)) return;
  csize_t
    f_in = function_in(l),
    f_out = function_out(l),
    d_in = in - min(in, f_in),
    d_out = out - min(out, f_out);
  if(d_in == 0 && d_out == 0) return;
  cell_t *f = l->value.ptr[f_out];
  cell_t *ph = func(func_placeholder, d_in + 1, d_out + 1);

  l = expand(l, d_out);
  COUNTUP(i, d_out) {
    cell_t *d = dep(ph);
    l->value.ptr[f_out + i] = d;
    arg(ph, d);
  }
  arg(ph, f);
  refn(ph, d_out);
  l->value.ptr[list_size(l) - 1] = ph;
  l->value.type.flags = T_VAR;
  *lp = l;
}

cell_t *var_create(int t, cell_t *tc, int in, int out) {
  return t == T_LIST ?
    var_create_list(var_create_nonlist(T_FUNCTION, tc), in, out, 0) :
    var_create_nonlist(t, tc);
}

cell_t *var_create_nonlist(int t, cell_t *tc) {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->size = 2;
  c->value.ptr[0] = tc;
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
  c->value.type.flags = T_VAR;
  return c;
}

cell_t *var(int t, cell_t *c) {
  return var_create(t, trace_alloc(c ? c->size : 2), 0, 0);
}

bool is_var(cell_t const *c) {
  return c && is_value(c) && (c->value.type.flags & T_VAR) != 0;
}

cell_t *vector(csize_t n) {
  cell_t *c = closure_alloc(n+1);
  c->func = func_value;
  c->value.type.exclusive = T_ANY;
  return c;
}

cell_t *quote(cell_t *x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type.exclusive = T_LIST;
  c->value.ptr[0] = x;
  return c;
}

cell_t *empty_list() {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->value.type.exclusive = T_LIST;
  return c;
}

cell_t *make_list(csize_t n) {
  if(n == 0) return &nil_cell;
  cell_t *c = closure_alloc(n + 1);
  c->func = func_value;
  c->value.type.exclusive = T_LIST;
  return c;
}

bool is_list(cell_t const *c) {
  return c && is_value(c) && c->value.type.exclusive == T_LIST;
}

bool is_row_list(cell_t const *c) {
  return is_list(c) && (c->value.type.flags & T_ROW);
}

bool is_function(cell_t const *c) {
  return c && is_value(c) && c->value.type.exclusive == T_FUNCTION;
}

cell_t *make_map(csize_t s) {
  csize_t cs = calculate_map_size(s);
  cell_t *c = closure_alloc_cells(cs);
  uintptr_t size = (sizeof(cell_t) * cs - offset(cell_t, value.map)) / sizeof(pair_t) - 1;
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
  traverse(c, {
      if(*p == d) ret = true;
    }, ARGS_OUT);
  return ret;
}

/* todo: propagate types here */
bool func_dep(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  /* rely on another cell for reduction */
  /* don't need to drop arg, handled by other function */
  /* must temporarily reference to avoid replacement of p which is referenced elsewhere */
  cell_t *p = ref(c->expr.arg[0]);
  assert(is_dep_of(c, p));
  insert_root(&p);
  c->func = func_dep_entered;
  reduce_dep(&p);
  assert(c->func != func_dep_entered);
  remove_root(&p);
  drop(p);
  return false;
}

bool func_dep_entered(cell_t **cp, type_request_t treq) {
  // shouldn't happen; circular dependency
  assert(false);
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
  assert(!is_marked(c));
  if(treq.t != T_ANY && treq.t != T_FUNCTION) goto fail;
  csize_t in = closure_in(c), n = closure_args(c);

  if(n == 1) {
    *cp = ref(c->expr.arg[0]);
    drop(c);
    return false;
  }

  alt_set_t alt_set = 0;
  assert(in >= 1);
  if(!reduce_arg(c, in - 1, &alt_set, req_simple(T_FUNCTION))) goto fail;
  COUNTUP(i, in - 1) {
    if(!reduce_arg(c, i, &alt_set, req_any) ||
      as_conflict(alt_set)) goto fail;
  }
  clear_flags(c);

  cell_t *res = var(T_FUNCTION, c);
  for(csize_t i = in; i < n; ++i) {
    cell_t *d = c->expr.arg[i];
    if(d && is_dep(d)) {
      drop(c);
      d->expr.arg[0] = res;
      store_var(d, 0);
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
  return c && clear_ptr(c->func) == (void *)func_placeholder;
}

bool func_fail(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  measure.reduce_cnt--;
  fail(cp, treq);
  return false;
}
// WORD("fcompose", fcompose, 2, 1)
bool func_fcompose(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));

  alt_set_t alt_set = 0;
  type_request_t atr = req_simple(T_FUNCTION);
  if(!reduce_arg(c, 0, &alt_set, atr) ||
     !reduce_arg(c, 1, &alt_set, atr) ||
     as_conflict(alt_set)) goto fail;
  clear_flags(c);

  cell_t *res = var(T_FUNCTION, c);
  store_reduced(cp, mod_alt(res, c->alt, alt_set));
  ASSERT_REF();
  return true;

 fail:
  fail(cp, treq);
  return false;
}

// not really a func
bool func_list(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  if(treq.t == T_ANY && treq.t == T_LIST) return true;
  if(treq.t != T_RETURN) goto fail;
  csize_t n = list_size(c);

  alt_set_t alt_set = c->value.alt_set;
  COUNTDOWN(i, n) {
    if(!reduce_ptr(c, i, &alt_set, req_any) ||
      as_conflict(alt_set)) goto fail;
  }
  traverse(c, *p = clear_ptr(*p), PTRS);
  return true;

 fail:
  fail(cp, treq);
  return false;
}

void reduce_list(cell_t **cp) {
  if(!*cp) return;
  while(*cp) {
    if(func_list(cp, req_simple(T_RETURN))) {
      cp = &(*cp)->alt;
    }
  }
}

#if INTERFACE
typedef struct list_iterator {
  cell_t **array;
  csize_t index, size;
  bool row;
} list_iterator_t;

#define FORLIST(x, l) for(list_iterator_t it = list_begin(l); (x = list_next(&it)) ; )
#define WHILELIST(x, it) while((x = list_next(&it)))

#endif

list_iterator_t list_begin(cell_t *l) {
  assert(is_list(l));
  bool row = is_row_list(l);
  list_iterator_t it = {
    .array = l->value.ptr,
    .index = 0,
    .size = list_size(l) - row,
    .row = row
  };
  return it;
}

bool list_continue(list_iterator_t *it) {
  return !!it->array;
}

cell_t **list_next(list_iterator_t *it) {
  if(!it->array) return NULL;
  if(it->index < it->size) {
    return &it->array[it->index++];
  } else if(it->row) {
    cell_t **rp = &it->array[it->size];
    reduce(rp, req_simple(T_LIST)); // HACK fix this
    *it = list_begin(*rp);
    return &it->array[++it->index];
  } else {
    it->array = NULL;
    return NULL;
  }
}

// number of remaining elements
// NOTE: will reduce all rows
csize_t list_remaining_size(list_iterator_t it) {
  csize_t n = 0;
  while(it.row) {
    cell_t **rp = &it.array[it.size];
    reduce(rp, req_simple(T_LIST)); // HACK fix this
    it = list_begin(it.array[it.size]);
    n += it.size;
  }
  return n + it.size;
}

// finds the cell which contains a pointer
cell_t *ptr_to_cell(void *p) {
  return &cells[(cell_t *)p - cells];
}

// returns a copy of the rest of the list
cell_t *list_rest(list_iterator_t it) {
  if(!it.array) return NULL;
  if(!it.index) return ref(ptr_to_cell(it.array));
  csize_t elems = it.size - it.index + it.row;
  cell_t *rest = make_list(elems);
  COUNTUP(i, elems) {
    rest->value.ptr[i] = ref(it.array[i + it.index]);
  }
  return rest;
}

cell_t *flat_copy(cell_t *l) {
  assert(is_list(l));
  if(!(l->value.type.flags & T_ROW)) return ref(l);
  csize_t n = flat_list_size(l);
  if(!n) return &nil_cell;
  cell_t *res = make_list(n);
  res->value.type = l->value.type;
  res->value.type.flags &= ~T_ROW;
  cell_t **p, **rp = res->value.ptr;
  FORLIST(p, l) {
    *rp++ = ref(*p);
  }
  return res;
}

bool is_empty_list(const cell_t *l) {
  return is_list(l) && list_size(l) == 0;
}
