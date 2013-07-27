/* Copyright 2012-2013 Dustin DeWeese
   This file is part of pegc.

    pegc is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    pegc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pegc.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include "rt_types.h"
#include "gen/rt.h"
#include "gen/primitive.h"

   /*-----------------------------------------------,
    |          VARIABLE NAME CONVENTIONS            |
    |-----------------------------------------------|
    |                                               |
    |  cell_t *c = closure being reduced            |
    |  cell_t *d = first dep                        |
    |    (second closure returned, lower on stack)  |
    |  cell_t *e = second dep                       |
    |  cell_t *f = ...                              |
    |  cell_t *p = arg[0], leftmost arg             |
    |  cell_t *q = arg[1]                           |
    |  bool s = success                             |
    |  cell_t *res = result to be stored in c       |
    |  cell_t *res_X = result to be stored in X     |
    |                                               |
    '-----------------------------------------------*/

/* must be in ascending order */
word_entry_t word_table[27] = {
  {"!", func_assert, 1, 1},
  {"'", func_quote, 1, 1},
  {"*", func_mul, 2, 1},
  {"+", func_add, 2, 1},
  {"-", func_sub, 2, 1},
  {".", func_compose, 2, 1},
  {"<", func_lt, 2, 1},
  {"<=", func_lte, 2, 1},
  {"==", func_eq, 2, 1},
  {">", func_gt, 2, 1},
  {">=", func_gte, 2, 1},
  {"cut", func_cut, 1, 1},
  {"dip11", func_dip11, 3, 2},
  {"dip12", func_dip12, 3, 3},
  {"dip21", func_dip21, 4, 2},
  {"drop", func_drop, 2, 1},
  {"dup", func_dup, 1, 2},
  {"force", func_force, 2, 2},
  {"head", func_head, 1, 1},
  {"id", func_id, 1, 1},
  {"ifte", func_ifte, 3, 1},
  {"popr", func_popr, 1, 2},
  {"pushl", func_pushl, 2, 1},
  {"pushr", func_pushr, 2, 1},
  {"swap", func_swap, 2, 2},
  {"|", func_alt, 2, 1},
  {"||", func_alt2, 2, 1}
};

bool func_op2(cell_t **cp, intptr_t (*op)(intptr_t, intptr_t)) {
  cell_t *res;
  cell_t *c = clear_ptr(*cp, 3);
  if(!(reduce(&c->arg[0]) &&
       reduce(&c->arg[1]))) goto fail;
    c->alt = closure_split(c, 2);
  if(bm_conflict(c->arg[0]->alt_set,
		 c->arg[1]->alt_set)) goto fail;
  cell_t *p = get(c->arg[0]);
  cell_t *q = get(c->arg[1]);
  int val_size = min(p->val_size,
		     q->val_size);
  res = vector(val_size);
  res->type = T_INT;
  res->func = func_reduced;
  int i;
  for(i = 0; i < val_size; ++i)
    res->val[i] = op(p->val[i], q->val[i]);
  res->val_size = val_size;
  res->alt_set =
    alt_set_ref(c->arg[0]->alt_set |
		c->arg[1]->alt_set);
  res->alt = c->alt;
  drop(c->arg[0]);
  drop(c->arg[1]);
  store_reduced(c, res);
  return true;

 fail:
  fail(cp);
  return false;
}

intptr_t add_op(intptr_t x, intptr_t y) { return x + y; }
bool func_add(cell_t **cp) { return func_op2(cp, add_op); }

intptr_t mul_op(intptr_t x, intptr_t y) { return x * y; }
bool func_mul(cell_t **cp) { return func_op2(cp, mul_op); }

intptr_t sub_op(intptr_t x, intptr_t y) { return x - y; }
bool func_sub(cell_t **cp) { return func_op2(cp, sub_op); }

intptr_t gt_op(intptr_t x, intptr_t y) { return x > y; }
bool func_gt(cell_t **cp) { return func_op2(cp, gt_op); }

intptr_t gte_op(intptr_t x, intptr_t y) { return x >= y; }
bool func_gte(cell_t **cp) { return func_op2(cp, gte_op); }

intptr_t lt_op(intptr_t x, intptr_t y) { return x < y; }
bool func_lt(cell_t **cp) { return func_op2(cp, lt_op); }

intptr_t lte_op(intptr_t x, intptr_t y) { return x <= y; }
bool func_lte(cell_t **cp) { return func_op2(cp, lte_op); }

intptr_t eq_op(intptr_t x, intptr_t y) { return x == y; }
bool func_eq(cell_t **cp) { return func_op2(cp, eq_op); }

bool func_compose(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *res;
  if(!(reduce(&c->arg[0]) && reduce(&c->arg[1]))) goto fail;
  c->alt = closure_split(c, 2);
  if(bm_conflict(c->arg[0]->alt_set,
		 c->arg[1]->alt_set)) goto fail;
  cell_t *p = get(c->arg[0]), *q = get(c->arg[1]);
  res = compose_nd(ref(p), ref(q));
  res->alt_set =
    alt_set_ref(c->arg[0]->alt_set |
		c->arg[1]->alt_set);
  res->alt = c->alt;
  drop(c->arg[0]);
  drop(c->arg[1]);
  store_reduced(c, res);
  return true;

 fail:
  fail(cp);
  return false;
}

bool func_pushl(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  if(!(reduce(&c->arg[1]))) goto fail;
  c->alt = closure_split1(c, 1);
  cell_t *q = get(c->arg[1]);
  if(!is_list(q)) goto fail;
  cell_t *p = get(c->arg[0]);
  cell_t *res;
  res = pushl_nd(ref(p), ref(q));
  drop(res->alt);
  res->alt = c->alt;
  res->alt_set = alt_set_ref(c->arg[1]->alt_set);
  drop(c->arg[0]);
  drop(c->arg[1]);
  store_reduced(c, res);
  return true;

  fail:
    fail(cp);
    return false;
}

bool func_pushr(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  if(!reduce(&c->arg[0])) goto fail;
  cell_t *res;
  c->alt = closure_split1(c, 0);
  cell_t *p = get_(c->arg[0]);
  cell_t *q = get_(c->arg[1]);
  alt_set_t alt_set = c->arg[0]->alt_set;
  alt_set_ref(alt_set);
  alt_set_drop(p->alt_set);
  int n = list_size(p);
  res = expand(p, 1);
  memmove(res->ptr+1, res->ptr, sizeof(cell_t *)*n);
  res->ptr[0] = q;
  res->alt = c->alt;
  res->alt_set = alt_set;
  store_reduced(c, res);
  return true;

 fail:
  fail(cp);
  return false;
}

bool func_quote(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t res[2] = {{ .ptr = {c->arg[0]} }};
  store_reduced(c, res);
  return true;
}

bool func_popr(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *res;
  int res_n, elems;
  cell_t *alt = 0;
  cell_t *d = is_hole(c->arg[1]) ? 0 : c->arg[1];
  if(!reduce(&c->arg[0])) goto fail;

  if(c->arg[0]->alt) {
    alt = closure_alloc(2);
    alt->func = func_popr;
    alt->arg[0] = ref(c->arg[0]->alt);
    alt->arg[1] = d ? dep(ref(alt)) : hole;
    c->alt = alt;
    if(d) d->alt = conc_alt(alt->arg[1], d->alt);
  }

  cell_t *p = get(c->arg[0]);
  if(!(is_list(p) &&
       list_size(p) > 0)) goto fail;

  if(d) {
    drop(c);
    d->func = func_id;
    d->arg[0] = ref(p->ptr[0]);
    d->arg[1] = (cell_t *)alt_set_ref(c->arg[0]->alt_set);
  }

  /* drop the right list element */
  elems = list_size(p) - 1;
  res_n = calculate_list_size(elems);
  res = closure_alloc_cells(res_n);
  int i;
  for(i = 0; i < elems; ++i)
    res->ptr[i] = ref(p->ptr[i+1]);

  res->alt_set = alt_set_ref(p->alt_set);
  res->alt = c->alt;
  drop(c->arg[0]);
  store_reduced(c, res);
  return true;

 fail:
  if(d) {
    drop(c);
    store_fail(d, d->alt);
  }
  c->arg[1] = 0;
  fail(cp);
  return false;
}

bool is_alt(cell_t *c) {
  return c->func == func_alt;
}

bool func_alt(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  uint8_t a = new_alt_id(2);
  cell_t *r0 = id(ref(c->arg[0]));
  r0->arg[1] = (cell_t *)bm(a, 0);
  cell_t *r1 = id(ref(c->arg[1]));
  r1->arg[1] = (cell_t *)bm(a, 1);
  r0->alt = r1;
  *cp = r0;
  drop(c);
  return false;
}

bool func_alt2(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *r0 = id(ref(c->arg[0]));
  r0->arg[1] = 0;
  cell_t *r1 = id(ref(c->arg[1]));
  r1->arg[1] = 0;
  r0->alt = r1;
  *cp = r0;
  drop(c);
  return false;
}

bool func_assert(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  if(!reduce(&c->arg[0])) goto fail;
  c->alt = closure_split1(c, 0);
  cell_t *p = get(c->arg[0]);
  if(!p->type == T_INT || p->val[0] == 0) goto fail;
  store_reduced(c, mod_alt(c->arg[0], c->alt,
			   c->arg[0]->alt_set));
  return true;
 fail:
  fail(cp);
  return false;
}

/* this function is problematic */
cell_t *get_(cell_t *c) {
  cell_t *p = ref(get(c));
  drop(c);
  return p;
}

bool func_id(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  alt_set_t alt_set = (alt_set_t)c->arg[1];
  if(alt_set || c->alt) {
    if(!reduce(&c->arg[0])) goto fail;
    c->alt = closure_split1(c, 0);
    cell_t *p = c->arg[0];
    if(p->alt) alt_set_ref(alt_set);
    if(bm_conflict(alt_set, c->arg[0]->alt_set)) goto fail;
    store_reduced(c, mod_alt(p, c->alt, alt_set | p->alt_set));
    alt_set_drop(alt_set);
    return true;
  } else {
    cell_t *p = ref(c->arg[0]);
    drop(c);
    *cp = p;
    return false;
  }

 fail:
  fail(cp);
  return false;
}

bool func_force(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *d = is_hole(c->arg[2]) ? 0 : c->arg[2];
  if(!(reduce(&c->arg[0]) & reduce(&c->arg[1]))) goto fail;
  cell_t *alt = c->alt = closure_split(c, 2);
  if(alt) {
    if(d) d->alt = alt->arg[2] = dep(ref(alt));
    else alt->arg[2] = hole;
  }
  cell_t *p = c->arg[0], *q = c->arg[1];
  if(bm_conflict(p->alt_set, q->alt_set)) goto fail;
  alt_set_t alt_set = p->alt_set | q->alt_set;
  store_reduced(c, mod_alt(p, alt, alt_set));
  if(d) {
    drop(c);
    store_reduced(d, mod_alt(q, d->alt, alt_set));
  } else drop(q);
  return true;

 fail:
  if(d) {
    drop(c);
    store_fail(d, d->alt);
  }
  c->arg[2] = 0;
  fail(cp);
  return false;
}

bool func_drop(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *p = ref(c->arg[0]);
  drop(c);
  *cp = p;
  return false;
}

bool func_swap(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *d = is_hole(c->arg[2]) ? 0 : c->arg[2];
  c->func = func_id;
  if(d) {
    drop(c);
    d->func = func_id;
    d->arg[0] = c->arg[0];
    d->arg[1] = 0;
  } else drop(c->arg[0]);
  cell_t *q = c->arg[0] = c->arg[1];
  c->arg[1] = c->arg[2] = 0;
  *cp = ref(q);
  drop(c);
  return false;
}

cell_t *id(cell_t *c) {
  cell_t *i = func(func_id, 1);
  arg(i, c);
  return i;
}

bool func_dup(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *d = is_hole(c->arg[1]) ? 0 : c->arg[1];
  if(!reduce(&c->arg[0])) goto fail;
  cell_t *p = get_(c->arg[0]);
  if(d) {
    drop(c);
    store_reduced(d, ref(p));
  }
  store_reduced(c, p);
  return true;

 fail:
  if(d) {
    drop(c);
    store_fail(d, d->alt);
  }
  c->arg[1] = 0;
  fail(cp);
  return false;
}

cell_t *build(char *str, unsigned int n);
#define BUILD(x) build((x), sizeof(x))

bool func_ifte(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  char code[] = "[] pushl pushl swap pushr"
    "[0 == ! force drop swap force drop]"
    "[1 == ! force drop force drop] | . popr swap drop";
  cell_t *b = BUILD(code);
  cell_t *p = ref(b->ptr[0]);
  drop(b);
  arg(p, c->arg[2]);
  arg(p, c->arg[1]);
  arg(p, c->arg[0]);
  if(!reduce(&p)) goto fail;
  store_reduced(c, p);
  return true;

 fail:
  fail(cp);
  return false;
}

bool func_dip11(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *d = is_hole(c->arg[3]) ? 0 : c->arg[3];
  char code[] = "swap pushr pushl popr "
    "swap popr swap drop swap";
  cell_t *b = BUILD(code);
  cell_t *p = b->ptr[1];

  arg(p, c->arg[2]);
  arg(p, c->arg[1]);
  arg(p, c->arg[0]);

  closure_shrink(c, 1);
  c->func = func_id;
  c->arg[0] = ref(p);
  c->arg[1] = 0;
  c->arg[2] = 0;
  if(d) {
    drop(c);
    d->func = func_id;
    d->arg[0] = ref(b->ptr[0]);
    d->arg[1] = 0;
  }
  drop(b);
  return false;
}

bool func_dip12(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *other2 = is_hole(c->arg[3]) ? 0 : c->arg[3];
  cell_t *other1 = is_hole(c->arg[4]) ? 0 : c->arg[4];
  char code[] = "swap pushr pushl popr swap pushl"
    "[swap] . popr swap popr swap popr swap drop swap";
  cell_t *b = BUILD(code);
  cell_t *p = b->ptr[2];
  arg(p, c->arg[2]);
  arg(p, c->arg[1]);
  arg(p, c->arg[0]);

  closure_shrink(c, 1);
  c->func = func_id;
  c->arg[0] = ref(p);
  c->arg[1] = 0;
  c->arg[2] = 0;
  if(other1) {
    drop(c);
    other1->func = func_id;
    other1->arg[0] = ref(b->ptr[1]);
    other1->arg[1] = 0;
  }
  if(other2) {
    drop(c);
    other2->func = func_id;
    other2->arg[0] = ref(b->ptr[0]);
    other2->arg[1] = 0;
  }
  drop(b);
  return false;
}

bool func_dip21(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  cell_t *other = is_hole(c->arg[4]) ? 0 : c->arg[4];
  char code[] = "swap pushr pushl pushl popr "
    "swap popr swap drop swap";
  cell_t *b = BUILD(code);
  cell_t *p = b->ptr[1];
  arg(p, c->arg[3]);
  arg(p, c->arg[2]);
  arg(p, c->arg[1]);
  arg(p, c->arg[0]);

  closure_shrink(c, 1);
  c->func = func_id;
  c->arg[0] = ref(p);
  c->arg[1] = 0;
  c->arg[2] = 0;
  if(other) {
    drop(c);
    other->func = func_id;
    other->arg[0] = ref(b->ptr[0]);
    other->arg[1] = 0;
  }
  drop(b);
  return false;
}

bool func_cut(cell_t **cp) {
  cell_t *c = *cp;
  cell_t *p = c->arg[0];
  if(!reduce(&p)) goto fail;
  drop(p->alt);
  p->alt = 0;
  store_reduced(c, p);
  return true;

 fail:
  fail(cp);
  return false;
}

bool func_head(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  char code[] = "popr swap drop";
  cell_t *b = BUILD(code);
  cell_t *p = b->ptr[0];

  arg(p, c->arg[0]);

  closure_shrink(c, 1);
  c->func = func_id;
  c->arg[0] = ref(p);
  c->arg[1] = 0;
  drop(b);
  return false;
}
