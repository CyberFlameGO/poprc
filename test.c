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

#include "rt_types.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "linenoise/linenoise.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/test.h"
#include "gen/eval.h"
#include "gen/primitive.h"
#include "gen/test_table.h"

struct __test_entry tests[] = TESTS;

int test_alloc(UNUSED char *name) {
  cell_t *a[30];
  LOOP(50) {
    FOREACH(i, a) {
      a[i] = func(func_add, 9, 1);
    }
    FOREACH(i, a) {
      closure_free(a[i]);
    }
  }
  return check_free() ? 0 : -1;
}

int test_loops(UNUSED char *name) {
  COUNTUP(i, 3) {
    printf("up: %d\n", (unsigned int)i);
  }
  COUNTDOWN(i, 3) {
    printf("down: %d\n", (unsigned int)i);
  }
  COUNTUP(i, 0) {
    printf("down: shouldn't print this\n");
  }
  COUNTDOWN(i, 0) {
    printf("down: shouldn't print this\n");
  }

  unsigned int arr[] = {1, 4, 9};
  FOREACH(i, arr) {
    printf("arr[%d] = %d\n", (unsigned int)i, arr[i]);
  }
  LOOP(3) {
    LOOP(3) {
      putchar('x');
    }
    putchar('\n');
  }
  return 0;
}

bool check_free() {
  bool leak = false;
  FOREACH(i, cells) {
    if(is_closure(&cells[i])) {
      printf("LEAK: %" PRIuPTR " (%u)\n", i, (unsigned int)cells[i].n);
      leak = true;
    }
  }
  return !leak;
}

#define MAX_NAME_SIZE 4096

int run_test(char *name, void (*logger)(char *name, int result)) {
  int name_size = strnlen(name, MAX_NAME_SIZE);
  int fail = 0;
  FOREACH(i, tests) {
    struct __test_entry *entry = &tests[i];
    int entry_name_size = strnlen(entry->name, MAX_NAME_SIZE);
    if(strncmp(name, entry->name, min(name_size, entry_name_size)) == 0) {
      printf("@ %s\n", entry->name);
      int result = entry->func(name);
      if((uintptr_t)logger > 1) logger(entry->name, result);
      if(result && !fail) fail = result;
    }
  }
  return fail;
}

void test_log(char *name, int result) {
  printf("%s => %d\n", name, result);
}


#define TEST2(x0, x1, x2, ...) printf("TEST2(" x0 ", " x1 ", " x2 ")\n")
#define TEST1(...) printf("TEST1\n")

int test_macro_dispatch(UNUSED char *name) {
  DISPATCH(TEST, 5, "1", "2", "3");
  DISPATCH(TEST, 5, "1", "2", "3", "4");
  return 0;
}
