{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "rkb";
  version = "2.0.0";

  # Unpinned: builtins.fetchGit is a nix builtin with no hash parameter, so
  # there is nothing for nix to enforce and main is always resolved at build
  # time. Do not convert this back to pkgs.fetchgit - that is a fixed-output
  # derivation and requires a sha256, which pins it.
  src = builtins.fetchGit {
    url = "https://github.com/mckenney5/repo-kanban.git";
    ref = "refs/heads/main";
    submodules = true;
  };

  makeFlags = [ "PREFIX=$(out)" ];

  # buildInputs = [ pkgs.ncurses ]; # Uncomment to add dependencies

  meta = with pkgs.lib; {
    description = "Local CLI Kanban board for Agents";
    homepage = https://github.com/mckenney5/repo-kanban;
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
