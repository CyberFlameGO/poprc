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

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "rt_types.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/primitive.h"
#include "gen/special.h"
#include "gen/print.h"

   /*-----------------------------------------------,
    |          VARIABLE NAME CONVENTIONS            |
    |-----------------------------------------------|
    |                                               |
    |  cell_t *c = closure being reduced            |
    |  cell_t *d = first dep                        |
    |    (second closure returned, lower on stack)  |
    |  cell_t *e = second dep                       |
    |  cell_t *f = ...                              |
    |  cell_t *p = expr.arg[0], leftmost arg        |
    |  cell_t *q = expr.arg[1]                      |
    |  bool s = success                             |
    |  cell_t *res = result to be stored in c       |
    |  cell_t *res_X = result to be stored in X     |
    |                                               |
    '-----------------------------------------------*/

cell_t *_op2(val_t (*op)(val_t, val_t), cell_t *x, cell_t *y) {
  csize_t size = min(val_size(x),
                     val_size(y));
  cell_t *res = vector(size);
  for(csize_t i = 0; i < size; ++i)
    res->value.integer[i] = op(x->value.integer[i],
                               y->value.integer[i]);
  res->size = size + 1;
  return res;
}

bool func_op2(cell_t **cp, type_t t, alt_set_t as, type_t arg_type, type_t res_type, val_t (*op)(val_t, val_t)) {
  cell_t *res = 0;
  cell_t *const c = *cp;
  alt_set_t alt_set = as;
  assert(!is_marked(c));

  if(t != T_ANY && t != res_type) goto fail;

  if(!reduce_arg(c, 0, &alt_set, arg_type) ||
     !reduce_arg(c, 1, &alt_set, arg_type)) goto fail;
  clear_flags(c);
  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  res = is_var(p) || is_var(q) ? var(t) : _op2(op, p, q);
  res->value.type |= res_type;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  store_reduced(cp, res);
  return true;

 fail:
  drop(res);
  fail(cp, t, as);
  return false;
}

// WORD("+", add, 2, 1)
val_t add_op(val_t x, val_t y) { return x + y; }
bool func_add(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_INT, add_op); }


// WORD("*", mul, 2, 1)
val_t mul_op(val_t x, val_t y) { return x * y; }
bool func_mul(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_INT, mul_op); }

// WORD("-", sub, 2, 1)
val_t sub_op(val_t x, val_t y) { return x - y; }
bool func_sub(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_INT, sub_op); }

// WORD(">", gt, 2, 1)
val_t gt_op(val_t x, val_t y) { return x > y; }
bool func_gt(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, gt_op); }

// WORD(">=", gte, 2, 1)
val_t gte_op(val_t x, val_t y) { return x >= y; }
bool func_gte(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, gte_op); }

// WORD("<", lt, 2, 1)
val_t lt_op(val_t x, val_t y) { return x < y; }
bool func_lt(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, lt_op); }

// WORD("<=", lte, 2, 1)
val_t lte_op(val_t x, val_t y) { return x <= y; }
bool func_lte(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, lte_op); }

// WORD("==", eq, 2, 1)
// WORD("=:=", eq_s, 2, 1)
val_t eq_op(val_t x, val_t y) { return x == y; }
bool func_eq(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, eq_op); }
bool func_eq_s(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_SYMBOL, T_SYMBOL, eq_op); }

// WORD("!=", neq, 2, 1)
// WORD("!:=", neq_s, 2, 1)
val_t neq_op(val_t x, val_t y) { return x != y; }
bool func_neq(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_INT, T_SYMBOL, neq_op); }
bool func_neq_s(cell_t **cp, type_t t, alt_set_t as) { return func_op2(cp, t, as, T_SYMBOL, T_SYMBOL, neq_op); }

// WORD(".", compose, 2, 1)
bool func_compose(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *const c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;

  if(!(reduce_arg(c, 0, &alt_set, T_LIST) &&
       reduce_arg(c, 1, &alt_set, T_LIST))) goto fail;
  clear_flags(c);
  cell_t *res = compose_nd(ref(c->expr.arg[0]), ref(c->expr.arg[1]));
  res->value.alt_set = alt_set;
  drop(res->alt);
  res->alt = c->alt;
  store_reduced(cp, res);
  return true;

 fail:
  fail(cp, t, as);
  return false;
}

// WORD("pushl", pushl, 2, 1)
bool func_pushl(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;
  if(!reduce_arg(c, 1, &alt_set, T_LIST)) goto fail;
  clear_flags(c);
  cell_t *q = c->expr.arg[1];
  bool rvar = is_var(q);
  cell_t *res = pushl_nd(ref(c->expr.arg[0]), ref(q));
  if(rvar) res->value.type |= T_VAR;
  drop(res->alt);
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  store_reduced(cp, res);
  return true;

  fail:
  fail(cp, t, as);
    return false;
}

// WORD("pushr", pushr, 2, 1)
bool func_pushr(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;
  if(!reduce_arg(c, 0, &alt_set, T_LIST)) goto fail;
  clear_flags(c);
  cell_t *p = c->expr.arg[0];

  int n = list_size(p);
  cell_t *res = expand(ref(p), 1);
  memmove(res->value.ptr+1, res->value.ptr, sizeof(cell_t *)*n);
  res->value.ptr[0] = ref(c->expr.arg[1]);
  res->value.alt_set = alt_set;
  drop(res->alt);
  res->alt = c->alt;

  store_reduced(cp, res);
  return true;

 fail:
  fail(cp, t, as);
  return false;
}

// WORD("'", quote, 1, 1)
bool func_quote(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(*cp));
  cell_t res = { .size = 2, .value.ptr = {ref(c->expr.arg[0])} };
  store_reduced(cp, &res);
  return true;
}

// WORD("popr", popr, 1, 2)
bool func_popr(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *d = c->expr.arg[1];
  as = 0; // HACK ***
  alt_set_t alt_set = as;
  if(!reduce_arg(c, 0, &alt_set, T_LIST)) goto fail;
  clear_flags(c);
  cell_t *p = c->expr.arg[0];
  if(list_size(p) == 0) goto fail;

  type_t res_type = T_LIST;
  // adds an extra output dep to the placeholder, and puts the dep in front of it
  // [P[in|out] -> [P[in|out+d], d]
  // also marks result as a variable
  cell_t *res;
  cell_t **l = p->value.ptr;
  if(is_placeholder(l[0])) {
    closure_set_ready(l[0], true);
    cell_t *h = expand_inplace_dep(l[0], 1); // *** no shift here
    l[0] = h->expr.arg[closure_in(h)] = dep(ref(h));
    l[1] = h;
    res_type |= T_VAR;
    res = ref(p);
  } else if(closure_is_ready(l[0])) {
    /* drop the right list element */
    res = closure_alloc(closure_args(p)-1);
    res->func = func_value;
    csize_t elems = list_size(res);
    res->value.type = res_type;
    for(csize_t i = 0; i < elems; ++i) {
      res->value.ptr[i] = ref(l[i+1]);
    }
  } else goto fail;

  store_lazy_dep(c, d, ref(p->value.ptr[0]), alt_set);
  res->value.alt_set = alt_set;
  res->alt = c->alt;
  store_reduced(cp, res);
  return true;

 fail:
  if(d) {
    drop(c);
    store_fail(d, d->alt);
  }
  c->expr.arg[1] = 0;
  fail(cp, t, as);
  return false;
}

// WORD("|", alt, 2, 1)
bool func_alt(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  uint8_t a = new_alt_id(2);
  cell_t *r0 = id(c->expr.arg[0]);
  r0->expr.arg[1] = (cell_t *)as_single(a, 0);
  cell_t *r1 = id(c->expr.arg[1]);
  r1->expr.arg[1] = (cell_t *)as_single(a, 1);
  r0->alt = r1;
  store_lazy(cp, c, r0, 0);
  return false;
}

// WORD("||", alt2, 2, 1)
bool func_alt2(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *r0 = id(ref(c->expr.arg[0]));
  r0->expr.arg[1] = 0;
  cell_t *r1 = id(ref(c->expr.arg[1]));
  r1->expr.arg[1] = 0;
  r0->alt = r1;
  *cp = r0;
  drop(c);
  return false;
}

// WORD("!", assert, 2, 1)
bool func_assert(cell_t **cp, type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;
  if(!reduce_arg(c, 1, &alt_set, T_SYMBOL)) goto fail;
  clear_flags(c);
  cell_t *p = c->expr.arg[1];
  if(is_var(p)) {
    if(!reduce_arg(c, 0, &alt_set, t)) goto fail;
    store_reduced(cp, var(t));
    return true;
  } else if(p->value.integer[0] == SYM_TRUE) {
    drop(p);
    store_lazy(cp, c, c->expr.arg[0], alt_set);
    return false;
  } else goto fail;
 fail:
  fail(cp, t, as);
  return false;
}

/*
bool type_check(cell_t **cp, type_t type) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  if(!reduce(&c->expr.arg[0], type)) goto fail;
  c->alt = closure_split1(c, 0);
  cell_t *p = get(c->expr.arg[0]);
  if(!((p->value.type == type) ||
       is_var(p))) goto fail;
  store_reduced(c, mod_alt(c->expr.arg[0], c->alt,
                           c->expr.arg[0]->value.alt_set));
  return true;
 fail:
  fail(cp);
  return false;
}
*/

// WORD("id", id, 1, 1)
bool func_id(cell_t **cp, type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = (alt_set_t)c->expr.arg[1];
  // ***
  // if(as_conflict(as, alt_set)) goto fail;
  // alt_set |= as;
  if(alt_set || c->alt) {
    if(!reduce_arg(c, 0, &alt_set, t)) goto fail;
    clear_flags(c);
    cell_t *p = c->expr.arg[0];
    store_reduced(cp, mod_alt(ref(p), c->alt, alt_set));
    return true;
  } else {
    cell_t *p = ref(c->expr.arg[0]);
    drop(c);
    *cp = p;
    return false;
  }

 fail:
  fail(cp, t, as);
  return false;
}

// WORD("drop", drop, 2, 1)
bool func_drop(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *p = ref(c->expr.arg[0]);
  drop(c);
  *cp = p;
  return false;
}

// WORD("swap", swap, 2, 2)
bool func_swap(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *d = c->expr.arg[2];
  store_lazy_dep(c, d, c->expr.arg[0], 0);
  store_lazy(cp, c, c->expr.arg[1], 0);
  return false;
}

cell_t *id(cell_t *c) {
  cell_t *i = func(func_id, 1, 1);
  arg(&i, c);
  return i;
}

// WORD("dup", dup, 1, 2)
bool func_dup(cell_t **cp, UNUSED type_t t, UNUSED alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *d = c->expr.arg[1];
  store_lazy_dep(c, d, ref(c->expr.arg[0]), 0);
  store_lazy(cp, c, c->expr.arg[0], 0);
  return false;
}

void trace_expand_select(cell_t *c, cell_t *x, type_t t, alt_set_t as) {
  while(reduce(&x, t, as)) { // ***
    if(!is_var(x)) trace(x, c, tt_force, 0);
    trace(c, x, tt_select, 0);
    if(!is_var(x)) break;
    x = x->alt;
  }
}

// WORD("cut", cut, 1, 1)
bool func_cut(cell_t **cp, type_t t, alt_set_t as) {
  cell_t *c = *cp;
  if(!reduce(&c->expr.arg[0], t, as)) goto fail;
  cell_t *p = c->expr.arg[0];
  if(is_var(p)) {
    cell_t *alt = ref(p->alt);
    store_reduced(cp, mod_alt(ref(p), 0, p->value.alt_set));
    trace_expand_select(*cp, alt, t, as);
    drop(alt);
  } else {
    store_reduced(cp, mod_alt(ref(p), 0, p->value.alt_set));
  }
  return true;

 fail:
  fail(cp, t, as);
  return false;
}

/* args w/alts not handled correctly, */
/* should probably cut them, or hide select */
// WORD("select", select, 2, 1)
bool func_select(cell_t **cp, type_t t, alt_set_t as) {
  cell_t *c = *cp;
  alt_set_t alt_set = as; // ***
  if(reduce_arg(c, 0, &alt_set, t)) {
    if(is_var(c->expr.arg[0])) {
      clear_flags(c);
      store_reduced(cp, var(t));
    } else {
      clear_flags(c);
      store_reduced(cp, ref(c->expr.arg[0]));
    }
    return true;
  } else {
    clear_flags(c);
    store_lazy(cp, c, c->expr.arg[1], alt_set);
    return false;
  };
}

// WORD("ift", ift, 3, 1)
bool func_ift(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as; // ***
  if(!reduce_arg(c, 0, &alt_set, T_INT)) goto fail;
  clear_flags(c);
  if(c->expr.arg[0]->value.integer[0] > 1) goto fail;
  if(is_var(c->expr.arg[0])) {
    drop(c->expr.arg[0]); // need to add assertions
    cell_t *res = id(c->expr.arg[1]);
    res->alt = id(c->expr.arg[2]);
    store_lazy(cp, c, res, alt_set); // ***
  } else if(c->expr.arg[0]->value.integer[0]) {
    drop(c->expr.arg[0]);
    drop(c->expr.arg[2]);
    store_lazy(cp, c, c->expr.arg[1], alt_set);
  } else {
    drop(c->expr.arg[0]);
    drop(c->expr.arg[1]);
    store_lazy(cp, c, c->expr.arg[2], alt_set);
  }
  return false;
 fail:
  fail(cp, t, as);
  return false;
}

bool func_ap(cell_t **cp, UNUSED type_t t, alt_set_t as) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;
  const csize_t
    in = closure_in(c),
    n = closure_args(c),
    out = closure_out(c);
  csize_t i;
  if(!reduce_arg(c, in-1, &alt_set, T_LIST)) goto fail;
  cell_t *l = c->expr.arg[in-1];
  COUNTDOWN(i, in-1) {
    l = pushl_nd(c->expr.arg[i], l);
  }

  csize_t ln = list_size(l);
  //cell_t *ph = 0;
  if(ln && is_placeholder(l->value.ptr[ln-1])) {
    //ph = l->value.ptr[ln-1];
    ln--;
  }

  csize_t stop = min(ln, out);
  for(i = 0; i < stop; i++) {
    store_lazy_dep(c, c->expr.arg[n-1-i], ref(l->value.ptr[i]), alt_set);
  }

  for(; i < out; i++) {
    cell_t *p = c->expr.arg[n-1-i];
    drop(c);
    store_fail(p, p->alt);
  }

  if(out > ln) goto fail; // todo: handle placeholders

  /* drop the right list elements */
  cell_t *res = closure_alloc(closure_args(l) - out);
  csize_t elems = list_size(res);
  res->value.type = T_LIST;
  for(csize_t i = 0; i < elems; ++i)
    res->value.ptr[i] = ref(l->value.ptr[i+(n-in)]);

  res->func = func_value;
  res->value.alt_set = alt_set;
  res->alt = c->alt;
  c->alt = 0;
  store_lazy(cp, c, res, 0);
  drop(l);
  return false;
fail:
  fail(cp, t, as);
  return false;
}

// WORD("print", print, 2, 1)
bool func_print(cell_t **cp, type_t t, alt_set_t as) {
  cell_t *res = 0;
  cell_t *const c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = as;

  if(t != T_ANY && t != T_SYMBOL) goto fail;

  if(!reduce_arg(c, 0, &alt_set, T_SYMBOL) ||
     !reduce_arg(c, 1, &alt_set, T_ANY)) goto fail;
  clear_flags(c);
  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(t);
  } else if(p->value.integer[0] == SYM_IO) {
    show_one(q);
    res = mod_alt(ref(p), NULL, alt_set);
  } else goto fail;
  res->value.type |= T_SYMBOL;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  store_reduced(cp, res);
  return true;

 fail:
  drop(res);
  fail(cp, t, as);
  return false;
}
