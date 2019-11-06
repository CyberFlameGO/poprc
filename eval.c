/* Copyright 2012-2018 Dustin DeWeese
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

#define _GNU_SOURCE
#include "rt_types.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

#if defined(USE_READLINE)
#include <readline/readline.h>
#include <readline/history.h>
#elif defined(USE_LINENOISE)
#include "linenoise/linenoise.h"
#else
#define RAW_LINE
#endif

#include "startle/error.h"
#include "startle/test.h"
#include "startle/support.h"
#include "startle/log.h"

#include "cells.h"
#include "rt.h"
#include "special.h"
#include "eval.h"
#include "byte_compile.h"
#include "parse.h"
#include "print.h"
#include "cgen.h"
#include "lex.h"
#include "module.h"
#include "list.h"
#include "trace.h"
#include "git_log.h"
#include "log_tree.h"
#include "io.h"
#include "primitive_io.h"
#include "irc.h"
#include "vlgen.h"

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifndef PREFIX
#define PREFIX "/usr/local"
#endif

#define HISTORY_FILE ".poprc_history"

bool quit = false;
bool command_line = false;
bool exit_on_error = false;

static bool tty = false;
static bool echo = false;
static bool run_leak_test = true;
static bool quiet = false;
static bool will_eval_commands = true;
static bool eval_commands = true;
static bool allow_io = true;
static int reduction_limit = 5;
const char *history_path = NULL;

char line_buffer[1024];

COMMAND(git, "git commit for this build") {
  puts(GIT_LOG);
  if(command_line) quit = true;
}

void stats_start() {
  memset(&stats, 0, sizeof(stats));
  stats.start = clock();
}

void stats_stop() {
  memcpy(&saved_stats, &stats, sizeof(stats));
  saved_stats.stop = clock();
  saved_stats.alt_cnt = alt_cnt;
  saved_stats.trace_cnt = trace_count();
}

void stats_display() {
  double time = (saved_stats.stop - saved_stats.start) /
    (double)CLOCKS_PER_SEC;
  printf("time        : %.3e sec\n"
         "allocated   : %d cells\n"
         "working set : %d cells\n"
         "reductions  : %d\n"
         "failures    : %d\n"
         "trace       : %d\n",
         time,
         saved_stats.alloc_cnt,
         saved_stats.max_alloc_cnt,
         saved_stats.reduce_cnt,
         saved_stats.fail_cnt,
         saved_stats.trace_cnt
    );
  printf("rate        :");
  if(time != 0) {
    printf(" %.3e reductions/sec",
           saved_stats.reduce_cnt / time);
  }
  printf("\n"
         "alts used   : %d\n",
         saved_stats.alt_cnt);
}

void usage() {
  printf("usage: eval [-t <test name>]\n");
}

COMMAND(echo, "whether the input line is echoed") {
  echo = !rest || segcmp("yes", tok_seg(rest)) == 0;
}

COMMAND(stats, "print statistics") {
  stats_display();
}

COMMAND(symbols, "print symbol table") {
  print_symbols();
}

COMMAND(leak, "whether leak test is performed") {
  run_leak_test = !rest || segcmp("yes", tok_seg(rest)) == 0;
}

COMMAND(single, "eval one line and exit") {
  will_eval_commands = rest && segcmp("yes", tok_seg(rest)) != 0;
}

COMMAND(eval, "evaluate the argument") {
  cell_t *p = rest;
  if(p) {
    cell_t *e = NULL;
    if((e = check_reserved(p))) {
      const char *line = e->tok_list.line;
      const char *loc = e->tok_list.location;
      size_t size = strlen(line);
      find_line(loc, &line, &size);
      int pos = loc - line;
      COUNTUP(i, pos + 2) putchar(' ');
      printf("^--- Parse error\n");
    } else {
      stats_start();
      eval("  ", p);
      stats_stop();
    }
  }
}

#ifndef EMSCRIPTEN
static
void crash_handler(int sig, UNUSED siginfo_t *info, UNUSED void *ctx) {
  throw_error(ERROR_TYPE_UNEXPECTED, "%s", _, strsignal(sig));
}

const char *strip_dir(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

int main(int argc, char **argv) {
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = crash_handler;
  sigaction(SIGSEGV, &sa, NULL);

  log_init();

  error_t error;

  if(catch_error(&error)) {
    printf(NOTE("ERROR") " ");
    print_last_log_msg();
    print_active_entries("  - while compiling ");
    if(exit_on_error) {
      printf("\nExiting on error.\n");
      printf("\n___ LOG ___\n");
      log_print_all();
      return -error.type;
    }
    exit_on_error = true;
    cleanup_cells();
  }

  log_soft_init();
  cells_init();
  parse_init();
  module_init();
  char *home = getenv("HOME");
  if(home) {
    asprintf((char **)&history_path, "%s/" HISTORY_FILE, home);
  }

  command_line = true;
  bool quit = false;
  tty = isatty(fileno(stdin)) && isatty(fileno(stdout));
  quiet = !tty;
  cell_t *parsed_args = NULL;

  if(argc >= 1) {
    const char *exec_name = strip_dir(argv[0]);
    char *args = arguments(argc - 1, argv + 1), *a = args;
    // printf("__ arguments __\n%s", a);

    if(strcmp(exec_name, "popr") == 0) {
      eval_command_string(":ld " PREFIX "/share/poprc", 0);
      eval_command_string(":import", 0);
    }

    if(args) {
      // lex the arguments
      cell_t **next = &parsed_args;
      while(*a) {
        char *e = strchr(a, '\n');
        *e = '\0'; // HACKy (fix load_file instead)
        *next = lex(a, e);
        next = &(*next)->alt;
        a = e + 1;
      }

      // run the commands
      alloc_to(64); // keep allocations consistent regardless of args
      cell_t *p = parsed_args;
      while(p) {
        quit = !eval_command(p) || quit;
        free_toks(p);
        p = p->alt;
      }

      free(args);
    }
  }

  eval_commands = will_eval_commands;
  exit_on_error = false;
  command_line = false;

  if(!quit) run_eval(echo);
  free_modules();
  unload_files();
  if(run_leak_test &&
     !leak_test()) {
    make_graph_all("leaks.dot");
  }
  free((void *)history_path);
  return 0;
}
#else // EMSCRIPTEN
int main(int argc, char **argv) {
  log_init();
  cells_init();
  parse_init();
  module_init();

  eval_command_string(":load lib.ppr tests.ppr", 0);
  eval_command_string(":import", 0);
  emscripten_exit_with_live_runtime();
  return 0;
}
#endif

#if defined(USE_LINENOISE) || defined(USE_READLINE)
/*
static seg_t last_tok(const char *str, const char *e) {
  seg_t last, n;
  while(n = tok(str, e, NULL), n.s) {
    last = n;
    str = seg_end(n);
  }
  return last;
}
*/
#endif

#ifdef USE_LINENOISE
/*
static void completion(char const *buf, linenoiseCompletions *lc) {
  unsigned int n = strlen(buf);
  seg_t t = last_tok(buf, buf + n);
  if(!t.s) return;
  word_entry_t *e = lookup_word(t);
  if(e) {
    // add completions
    char *comp = malloc(n + 64);
    strcpy(comp, buf);
    do {
      unsigned int entry_len = strlen(e->name);
      int diff = entry_len - t.n;
      if(diff > 0) {
        comp = realloc(comp, n + diff + 1);
        strcpy(comp + n, e->name + (entry_len - diff));
        linenoiseAddCompletion(lc, comp);
      }
      e++; // FIXME
    } while(strncmp(e->name, t.s, t.n) == 0);
    free(comp);
  }
}
*/
#endif

#ifdef USE_READLINE
/*
static char **completion(char *buf, UNUSED int start, UNUSED int end)
{
  unsigned int current_match = 1;
  char **matches = NULL;
  unsigned int n = strlen(buf);
  seg_t t = last_tok(buf, buf + n);
  if(t.s) {
    word_entry_t *e = lookup_word(t);
    if(e) {
      matches = malloc(sizeof(char *) * 16);
      memset(matches, 0, sizeof(char *) * 16);

      char *copy = malloc(t.n + 1);
      seg_read(t, copy, t.n + 1);
      matches[0] = copy;

      // add completions
      do {
        unsigned int entry_len = strlen(e->name);
        int diff = entry_len - t.n;
        if(diff > 0) {
          char *comp = malloc(n + diff + 1);
          memcpy(comp, buf, n);
          strcpy(comp + n, e->name + (entry_len - diff));
          matches[current_match++] = comp;
        }
        e++; // FIXME
      } while(strncmp(e->name, t.s, t.n) == 0 && current_match < 16);

      // if there is just one match, make it the substitute
      if(current_match == 2) {
        free(matches[0]);
        matches[0] = malloc(strlen(matches[1]) + 1);
        strcpy(matches[0], matches[1]);
      }
    }
  }

  return matches;
}
*/
static void initialize_readline()
{
  rl_readline_name = "PoprC";
  /*
#if defined(__clang__)
  rl_attempted_completion_function = (CPPFunction *)completion;
#else
  rl_attempted_completion_function = (rl_completion_func_t *)completion;
#endif
  */
}

#endif

void run_eval(bool echo) {
#if defined(USE_READLINE)
  if(tty) {
    run_eval_readline(echo);
    return;
  }
#elif defined(USE_LINENOISE)
  if(tty) {
    run_eval_linenoise(echo);
    return;
  }
#endif
  char *line;
  while(tty && printf(": "),
        (line = fgets(line_buffer, sizeof(line_buffer), stdin)))
  {
    replace_char(line, '\n', '\0');
    if(line[0] == '\0') {
      continue;
    }

    if(echo) puts(line);
    bool run = eval_command_string(line, 0);

    if(!run || !eval_commands) break;
  }
}

#ifdef USE_LINENOISE
void run_eval_linenoise(bool echo) {
  char *line;
  //linenoiseSetCompletionCallback(completion);
  if(history_path) linenoiseHistoryLoad(history_path);
  while((line = linenoise(": ")))
  {
    if(line[0] == '\0') {
      free(line);
      continue;
    }

    linenoiseHistoryAdd(line);
    if(history_path) linenoiseHistorySave(history_path);

    if(echo) puts(line);
    bool run = eval_command_string(line, 0);

    free(line);
    if(!run || !eval_commands) break;
  }
}
#endif

#ifdef USE_READLINE
void run_eval_readline(bool echo) {
  char *line;
  initialize_readline();
  read_history(history_path);
  while((line = readline(": ")))
  {
    if(line[0] == '\0') {
      free(line);
      continue;
    }

    add_history(line);
    write_history(history_path);

    if(echo) puts(line);
    bool run = eval_command_string(line, 0);

    free(line);
    if(!run || !eval_commands) break;
  }
}
#endif

#define COMMAND__ITEM(file, line, name, desc)            \
  {                                                      \
    .first = (uintptr_t)#name,                           \
    .second = (uintptr_t)&command_##name                 \
  },
static pair_t commands[] = {
#include "command_list.h"
};
#undef COMMAND__ITEM

#define COMMAND__ITEM(file, line, name, desc)            \
  {                                                      \
    .first = (uintptr_t)#name,                           \
    .second = (uintptr_t)desc                            \
  },
static pair_t command_descriptions[] = {
#include "command_list.h"
};
#undef COMMAND__ITEM

bool run_command(seg_t name, cell_t *rest) {
  FOREACH(i, commands) {
    pair_t *entry = &commands[i];
    char *entry_name = (char *)entry->first;
    void (*entry_func)(cell_t *) = (void (*)(cell_t *))entry->second;
    int entry_name_size = strlen(entry_name);
    if((int)name.n <= entry_name_size &&
       strncmp(name.s, entry_name, name.n) == 0) {
      entry_func(rest);
      return true;
    }
  }
  return false;
}

COMMAND(modules, "print all modules") {
  print_modules();
}

COMMAND(load, "load given file(s)") {
  char buf[64];
  while(rest) {
    read_to_ws(&rest, buf, sizeof(buf));
    load_file(0, buf);
  }
}

COMMAND(ld, "load given dir(s)") {
  char buf[64];
  while(rest) {
    read_to_ws(&rest, buf, sizeof(buf));
    load_dir(buf);
  }
}

COMMAND(arity, "print arity of the given function") {
  csize_t in, out;
  if(rest) {
    if(get_arity(rest, &in, &out, eval_module())) {
      printf("%d -> %d\n", in, out);
    }
  }
}

COMMAND(define, "define a function") {
  cell_t *p = rest;
  cell_t *name = p;
  if(!name) return;
  p = p->tok_list.next;
  if(!p || segcmp(":", tok_seg(p)) != 0) return;
  p = p->tok_list.next;
  cell_t *expr = p;
  if(!expr) return;
  parse_eval_def(tok_seg(name), expr);
}

COMMAND(help, "list available commands") {
  char pre = command_line ? '-' : ':';
  printf("%s | DESCRIPTION\n", command_line ? "'----> FLAG" : "'-> COMMAND");
  seg_t name = { .s = "", .n = 0 };
  if(rest) name = tok_seg(rest);
  FOREACH(i, command_descriptions) {
    pair_t *entry = &command_descriptions[i];
    char *entry_name = (char *)entry->first;
    char *entry_desc = (char *)entry->second;
    int entry_name_size = strlen(entry_name);
    if((int)name.n <= entry_name_size &&
       strncmp(name.s, entry_name, name.n) == 0) {
      printf("  %*c%s | %s\n", max(0, 9 - entry_name_size), pre, entry_name, entry_desc);
    }
  }
  printf("            V\n");
  if(command_line) quit = true;
}

COMMAND(quit, "quit interpreter") {
  if(rest) {
    quit = segcmp("yes", tok_seg(rest)) == 0;
  } else {
    quit = true;
  }
}

#ifdef EMSCRIPTEN
int emscripten_eval(char *str, int len) {
  error_t error;
  if(catch_error(&error)) {
    print_last_log_msg();
    return -error.type;
  } else {
    eval_command_string(str, str + len);
    return 0;
  }
}
#endif

bool eval_command(cell_t *p) {
  if(match(p, ":")) {
    if(eval_commands) {
      p = p->tok_list.next;
      if(!p || !run_command(tok_seg(p), p->tok_list.next)) {
        printf("unknown command\n");
      }
    }
  } else {
    command_eval(p);
  }
  return !quit;
}

bool eval_command_string(char *start, char *end) {
  cell_t *p = lex(start, end);
  bool ret = eval_command(p);
  free_toks(p);
  return ret;
}

static
bool can_reduce(cell_t *l) {
  return
    is_list(l) &&
    list_size(l) > 0 &&
    (is_value(l->value.ptr[0]) ||
     closure_is_ready(l->value.ptr[0]));
}

void reduce_root(cell_t **cp, int depth, int limit) {
  if(depth < limit) {
    insert_root(cp);
    reduce_list(cp, depth);
    if(*cp) {
      cell_t **p;
      FORLIST(p, *cp, true) {
        if(can_reduce(*p)) reduce_root(p, depth + 1, limit);
      }
    }
    remove_root(cp);
  }
}

cell_t *eval_module() {
  return modules ? get_module(string_seg("eval")) : NULL;
}

bool eval(const char *prefix, const cell_t *p) {
  bool success = false;
  if(!allow_io && has_IO(p)) {
    if(!quiet) printf("IO not allowed.\n");
  } else {
    cell_t *c = parse_expr(&p, eval_module(), NULL);
    if(!c) return false;
    cell_t *left = *leftmost(&c);
    if(left && !closure_is_ready(left)) {
      if(!quiet) printf("incomplete expression\n");
    } else {
      rt_init();
      reduce_root(&c, 0, reduction_limit);
      if(c) {
        ASSERT_REF();
        success = true;
        show_alts(prefix, c);
      }
    }
    drop(c);
  }
  return success;
}

bool get_arity(const cell_t *p, csize_t *in, csize_t *out, cell_t *module) {
  cell_t *c = parse_expr(&p, module, NULL);
  if(!c) {
    LOG("parse failed");
    return false;
  }
  *in = function_in(c);
  *out = function_out(c, false);
  drop(c);
  return true;
}

static struct mmfile files[16] = {};
size_t files_cnt = 0;

bool load_file(int dirfd, const char *path) {
  if(files_cnt >= LENGTH(files)) {
    if(!quiet) printf("Can't load any more files.\n");
    return false;
  }
  struct mmfile *f = &files[files_cnt++];
  f->dirfd = dirfd;
  f->path = path;
  f->read_only = true;
  if(!mmap_file(f)) {
    if(!quiet) printf("Failed to open \"%s\"\n", path);
    return false;
  }

  cell_t *toks = lex(f->data, f->data + f->size);
  cell_t *e = NULL;
  seg_t name;

  if(!quiet) printf("Load %s ", path);
  char *s = "(";
  while(parse_module(&toks, &name, &e)) {
    if(!quiet) printf("%s%.*s", s, (int)name.n, name.s);
    s = ", ";
  }
  if(!quiet) printf(")\n");

  if(e) {
    const char *line = e->tok_list.line;
    const char *loc = e->tok_list.location;
    size_t size = f->size - (line - f->data);
    find_line(loc, &line, &size);
    printf("%.*s\n", (int)size, line);
    int pos = loc - line;
    int line_no = line_number(f->data, loc);
    COUNTUP(i, pos) putchar(' ');
    printf("^--- Parse error on line %d of %s\n", line_no, path);
  }
  free_toks(toks);
  return true;
}

const char extension[4] = ".ppr";
bool load_dir(const char *path) {
  DIR *dir = opendir(path);
  if(!dir) return false;
  int dfd = dirfd(dir);
  bool res = true;
  struct dirent *ent;
  while((ent = readdir(dir))) {
    size_t len = strlen(ent->d_name);
    if(len >= sizeof(extension) &&
       strncmp(ent->d_name + len - sizeof(extension), extension, sizeof(extension)) == 0) {
      res &= load_file(dfd, ent->d_name);
    }
  }
  return res;
}

bool unload_files() {
  bool success = true;
  COUNTUP(i, files_cnt) {
    success &= munmap_file(&files[i]);
  }
  return success;
}

COMMAND(bits, "number of bits in a pointer") {
  printf("%d\n", (int)sizeof(void *) * 8);
  if(command_line) quit = true;
}

void for_each_line(void (*f)(cell_t *, unsigned int)) {
  unsigned int n = 0;
  char *line;
  while((line = fgets(line_buffer, sizeof(line_buffer), stdin)))
  {
    char *p = line;
    while(*p && *p != '\n') ++p;
    *p = 0;
    if(line[0] == '\0') continue;
    cell_t *l = lex(line, 0);
    if(l) {
      f(l, n++);
      free_toks(l);
    }
  }
}

static
void print_toks_line(cell_t *l, UNUSED unsigned int n) {
  print_toks(l);
}

COMMAND(lex, "lex and print the arguments") {
  for_each_line(print_toks_line);
  quit = true;
}

static
void parse_and_print_line(cell_t *l, UNUSED unsigned int n) {
  cell_t *c = parse_expr((const cell_t **)&l, eval_module(), NULL);
  if(c) {
    show_list_elements(c);
    drop(c);
    putchar('\n');
  }
}

COMMAND(parse, "parse and print input lines") {
  for_each_line(parse_and_print_line);
  quit = true;
}

static
void print_bytecode_line(cell_t *l, unsigned int n) {
  char name_buf[8];
  snprintf(name_buf, sizeof(name_buf), "fn%d", n++);
  cell_t *e = parse_eval_def(string_seg(name_buf), l);
  print_bytecode(tcell_entry(e), false);
}

COMMAND(bc_in, "print bytecode for each line") {
  for_each_line(print_bytecode_line);
  quit = true;
}

bool match_log_tag(const cell_t *p) {
  return match_class(p, CC_ALPHA, sizeof(tag_t), sizeof(tag_t));
}

COMMAND(watch, "set a watched cell") {
  if(match_class(rest, CC_NUMERIC, 0, 64)) {
    int idx = parse_num(rest);
    assert_throw(idx >= 0 && (unsigned int)idx < LENGTH(cells));
    int i = set_watch(&cells[idx]);
    if(i) {
      printf("watch %d set: %d\n", i, idx);
    } else {
      printf("watch not set: %d\n", idx);
    }
  } else if(match_log_tag(rest)) {
    const char *tag = rest->tok_list.location;
    cell_t *p = rest->tok_list.next;
    if(match(p, "..")) {
      p = p->tok_list.next;
      const char *tag_to;
      if(!p) {
        tag_to = NULL;
      } else {
        assert_throw(match_log_tag(p), "not a log tag");
        tag_to = p->tok_list.location;
      }
      set_log_watch_range(tag, tag_to);
      if(tag_to) {
        printf("log watch set for tags in range: " FORMAT_TAG " .. " FORMAT_TAG "\n", tag, tag_to);
      } else {
        printf("log watch set for tags from: " FORMAT_TAG "\n", tag);
      }
    } else if(match(p, "+")) {
      set_log_watch(tag, true);
      printf("log watch set for like tags from: " FORMAT_TAG "\n", tag);
    } else {
      set_log_watch(tag, false);
      printf("log watch set for tag: " FORMAT_TAG "\n", tag);
    }
  }
}

COMMAND(test, "run tests matching the argument") {
  run_test(rest ? tok_seg(rest) : (seg_t){"", 0});
  if(command_line) quit = true;
}

COMMAND(log, "print the log") {
  log_print_all();
}

COMMAND(tweak, "tweak a value") {
  bool set = false;
  if(match_log_tag(rest)) {
    const char *tag = rest->tok_list.location;
    cell_t *arg = rest->tok_list.next;
    bool after = match(arg, "+");
    if(after) arg = arg->tok_list.next;
    if(arg && arg->char_class == CC_NUMERIC) {
      intptr_t val = parse_num(arg);
      printf("tweak set%s: " FORMAT_TAG " => %d\n", after ? " starting at" : "", tag, (int)val);
      log_set_tweak(tag, val, after);
      set = true;
    }
  }
  if(!set) {
    printf("tweak unset\n");
    log_unset_tweak();
  }
}

void breakpoint_hook() {
  print_active_entries("  - while compiling ");
  make_graph_all(NULL);
  log_trees();
}

COMMAND(noio, "prevent IO") {
  allow_io = false;
}

COMMAND(op, "set a watched op") {
  if(rest) {
    seg_t w = tok_seg(rest);
    op op = OP_null;
    if(segcmp("ap", w) == 0) {
      op = OP_ap;
    } else if(segcmp("compose", w) == 0) {
      op = OP_compose;
    } else {
      cell_t *e = lookup_word(w);
      if(e) {
        op = e->op;
      }
    }
    if(op) {
      set_watched_op(op);
      printf("watching op %s\n", op_name(op));
    }
  } else {
    set_watched_op(OP_null);
  }
}

COMMAND(cl, "clear log") {
  log_init();
}

COMMAND(limit, "set reduction limit") {
  if(match_class(rest, CC_NUMERIC, 0, 64)) {
    reduction_limit = clamp(1, 100, parse_num(rest));
    printf("reduction limit set to %d\n", reduction_limit);
  } else {
    printf("reduction limit is %d\n", reduction_limit);
  }
}

COMMAND(tag, "convert tag <-> hex") {
  if(match_class(rest, CC_NUMERIC, 0, 64)) {
    int x = parse_num(rest);
    tag_t tag;
    write_tag(tag, x);
    printf(FORMAT_TAG " = 0x%x\n", tag, x);
  } else if(match_log_tag(rest)) {
    const char *tag = rest->tok_list.location;
    int x = read_tag(tag);
    printf(FORMAT_TAG " = 0x%x\n", tag, x);
  }
}

bool call(tcell_t *e, val_t *in_args, val_t *out_args) {
  assert_throw(e && NOT_FLAG(*e, entry, PRIMITIVE));
  csize_t in = e->entry.in;
  csize_t out = e->entry.out;
  cell_t *c = func(OP_exec, in + 1, out);
  COUNTUP(i, in) {
    c->expr.arg[i] = val(T_INT, in_args[i]);
  }
  c->expr.arg[in] = (cell_t *)e;
  cell_t *l = make_list(out);
  l->value.ptr[0] = c;
  RANGEUP(i, 1, out) {
    l->value.ptr[REVI(i)] = c->expr.arg[in + i] = dep(c);
  }
  refn(c, out-1);
  closure_set_ready(c, true);
  reduce_root(&l, 0, reduction_limit);
  if(!l) return false;
  ASSERT_REF();
  if(out_args) {
    COUNTUP(i, out) {
      out_args[i] = l->value.ptr[REVI(i)]->value.integer;
    }
  }
  drop(l);
  return true;
}

void inputs_from_number(int n, range_t *bounds, int bounds_n, val_t *input) {
  COUNTDOWN(i, bounds_n) {
    int span = bounds[i].max - bounds[i].min;
    input[i] = bounds[i].min + n % span;
    n /= span;
  }
}

#define ANALYZE_ARGS 8
#define ANALYZE_MAX_COMBINATIONS (1<<24)
COMMAND(analyze, "analyze a function") {
  range_t bounds[ANALYZE_ARGS];
  val_t inputs[ANALYZE_ARGS];
  if(rest) {
    seg_t name = tok_seg(rest);
    cell_t *module = eval_module();
    cell_t *e = module_lookup_compiled(name, &module);
    assert_throw(e, "function not found");
    csize_t in = e->entry.in;
    assert_throw(in, "no arguments");
    assert_throw(in <= ANALYZE_ARGS, "too many arguments");
    tcell_t *entry = tcell_entry(e);
    COUNTUP(i, in) {
      tcell_t *v = &entry[REVI(i) + 1];
      assert_throw(v->value.type == T_INT, "only integer inputs");
      assert_throw(FLAG(*v, trace, BOUNDED), "inputs must be bounded");
      bounds[i] = v->trace.bound;
    }
    int combinations = 1;
    COUNTUP(i, in) {
      combinations *= bounds[i].max - bounds[i].min;
    }
    assert_throw(combinations <= ANALYZE_MAX_COMBINATIONS, "too many combinations");
    printf("%d combinations to test\n", combinations);
    stats_start();
    int dot = combinations / 80;
    COUNTUP(i, combinations) {
      if(!dot) {
        dot = combinations / 80;
        putchar('.');
        fflush(stdout);
      } else dot--;
      inputs_from_number(i, bounds, in, inputs);
      rt_init();
      bool success = call(entry, inputs, NULL);
      if(!success) {
        printf("\nFAILED test %d with inputs:", (int)i);
        COUNTUP(j, in) {
          printf(" %d", (int)inputs[j]);
        }
        printf("\n");
        break;
      }
    }
    printf("\n");
    stats_stop();
  }
}
