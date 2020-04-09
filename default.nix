{ pkgs ? import <nixpkgs> { } }:

with pkgs;

stdenv.mkDerivation {
  name = "poprc";
  src = ./.;
  buildInputs = [ verilog ];
  buildFlags = [ "testbenches" "PREFIX=$(out)" "EXTRA_CFLAGS=$(NIX_CFLAGS_COMPILE)" "USE_LINENOISE=y" ];
  installFlags = [ "PREFIX=$(out)" ];
}
