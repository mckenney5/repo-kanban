{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "rkb";
  version = "2.0.0";

  src = pkgs.fetchgit {
    url = https://github.com/mckenney5/repo-kanban.git;
    rev = "refs/heads/main";
    fetchSubmodules = true;
    sha256 = "sha256-9gYPVIDZkYtwZ6XoaZRtmUKFoIHPi2KCPlEk5no2U3o=";
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
