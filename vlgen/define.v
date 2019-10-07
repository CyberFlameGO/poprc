`ifndef __DEFINE__
`define __DEFINE__

`ifndef intN
 `define intN 32
`endif

`ifndef symN
 `define symN 1
`endif

`ifndef anyN
 `define anyN `intN
`endif

`ifndef addrN
 `define addrN 16
`endif

`define intT [`intN-1:0]
`define symT [`symN-1:0]
`define anyT [`anyN-1:0]
`define addrT [`anyN-1:0]

`define sync_ports \
  input clk, \
  input in_valid, output in_ready, \
  output out_valid, input out_ready

`define id(x) x

`define concat_(a, b) `id(a)``_``b

`define stringify(x) `"x`"

`define rename(dst, src) wire src; assign dst = src

`define top_sync(ready) \
  assign in_ready = ready

`define loop_sync(ready) \
  reg active = `false; \
  assign in_ready = ~active & ready

`define sync_wire(name) `concat_(`current_inst, name)

`define inst(t, n) t t``_``n

`define apply(f, x) `f x

`define inst_sync(t, n) \
  `define current_inst n \
  wire `sync_wire(out_valid); \
  `inst(t, n)

`define start_block(name) \
`define block name \

`define end_block(name) \
`undef block \

`define sync(valid, ready) \
      .clk(clk), \
      .in_valid(valid), \
      .out_valid(`sync_wire(out_valid)), \
      .in_ready(`sync_wire(in_ready)), \
      .out_ready(ready)

`define input(type, index) `input_``type(index)
`define output(type, index) `output_``type(index)
`define define_simple_input(type) `define input_``type``(index) input `type``T `id(in)``index
`define define_simple_output(type) `define output_``type``(index) output `type``T `id(out)``index
`define in(type, index, name) `in_``type(index, name)
`define out(type, index, name) `out_``type(index, name)
`define define_simple_in(type) `define in_``type(index, name) .`id(in)``index(name)
`define define_simple_out(type) `define out_``type(index, name) .`id(out)``index(name)
`define alias(type, name, index) `alias_``type(name, index)
`define variable(type, name, in) `variable_``type(name, in)
`define define_simple_alias(type) `define alias_``type(name, other) wire `type``T name = other
`define define_simple_variable(type) `define variable_``type(name, in) reg `type``T name = 0
`define wire(type, name) `wire_``type(name)
`define define_simple_wire(type) `define wire_``type(name) wire `type``T name
`define reg(type, name) `reg_``type(name)
`define define_simple_reg(type) `define reg_``type(name) reg `type``T name
`define const(type, name, val) `const_``type(name, val)
`define define_simple_const(type) `define const_``type(name, val) localparam `type``T name = val

`define define_simple_type(type) \
  `define_simple_input(type) \
  `define_simple_output(type) \
  `define_simple_in(type) \
  `define_simple_out(type) \
  `define_simple_alias(type) \
  `define_simple_variable(type) \
  `define_simple_wire(type) \
  `define_simple_reg(type) \
  `define_simple_const(type)

`define_simple_type(int)
`define_simple_type(sym)
`define_simple_type(any)

`define input_stream(index) input `intT in``index, input in``index``_valid, output in``index``_ready
`define output_stream(index) output `intT out``index, output out``index``_valid, input out``index``_ready
`define in_stream(index, name) .in``index(name), .in``index``_valid(name``_valid), .in``index``_ready(name``_ready)
`define out_stream(index, name) .out``index(name), .out``index``_valid(name``_valid), .out``index``_ready(name``_ready)
`define alias_stream(name, other) wire `intT name = other; wire name``_valid = other``_valid; wire name``_ready; assign other``_ready = name``_ready
`define variable_stream(name, in) \
  wire `intT name = in; \
  reg name``_valid_reg; \
  wire name``_valid = in``_valid; \
  `rename(in``_ready, name``_ready)
`define wire_stream(name) wire `intT name; wire name``_valid; wire name``_ready
`define reg_stream(name) reg `intT name; reg name``_valid; wire name``_ready
`define const_stream(name, val) localparam `intT name = val; localparam name``_valid = `true
`define const_nil(name) localparam `intT name = `nil; localparam name``_valid = `false

`define interface(type, index) `interface_``type(index)
`define intf(type, index, name) `intf_``type(index, name)
`define bus(type, index, inst) `bus_``type``(index, inst)

/* ------------------------------------------------------ *
     ARRAY BUS
 * ------------------------------------------------------ *
     addr:  address
     we:    write enable
     di:    data in
     do:    data out
     valid: address is valid
     ready: ready for a new address

     NOTES:
      -> do is assumed valid once the address
         has been transmitted (ready & valid)
      -> all inputs to the bus must be low
         if valid is low, allowing the bus to be OR'ed
 * ------------------------------------------------------ */
`define interface_Array(index) \
  output `addrT intf``index``_addr, \
  output intf``index``_we, \
  output `intT intf``index``_di, \
  input `intT intf``index``_do, \
  output intf``index``_valid, \
  input intf``index``_ready
`define intf_Array(index, name) \
      .intf``index``_addr(`concat_(`current_inst, name``_addr)), \
      .intf``index``_we(`concat_(`current_inst, name``_we)), \
      .intf``index``_di(`concat_(`current_inst, name``_di)), \
      .intf``index``_do(name``_do), \
      .intf``index``_valid(`concat_(`current_inst, name``_valid)), \
      .intf``index``_ready(name``_ready)
`define bus_Array(index, inst) \
  wire `addrT inst``_intf``index``_addr; \
  wire inst``_intf``index``_we; \
  wire `intT inst``_intf``index``_di; \
  wire inst``_intf``index``_valid

`define to_bus(pre) {pre``_addr, pre``_we, pre``_di, pre``_valid}

`define true 1'b1
`define false 1'b0
`define nil (~(`intN'd0))

`define assert(n, x) \
  `define current_inst n \
    wire `sync_wire(out_valid) = (x)

`define set(x) x <= `true
`define reset(x) x <= `false

`define valid(x) (! (& x))

`define returned_to(name) (returned && return_addr == label_``name)

`endif
