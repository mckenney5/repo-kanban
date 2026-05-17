{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "rkb";
  version = "1.0.0-local";

  src = pkgs.lib.cleanSource ./.;

  makeFlags = [ "PREFIX=$(out)" ];

  # buildInputs = [ pkgs.ncurses ]; # Uncomment to add dependencies

  meta = with pkgs.lib; {
    description = "Local CLI Kanban board for Agents";
    platforms = platforms.linux;
  };
}
