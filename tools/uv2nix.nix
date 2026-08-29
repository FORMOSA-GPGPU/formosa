# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs, python ? pkgs.python3, uv2nix, pyproject-nix, pyproject-build-systems
, workspaceRoot, envName ? "uv2nix-env" }:

let
  lib = pkgs.lib;

  inherit python;

  workspace = uv2nix.lib.workspace.loadWorkspace { inherit workspaceRoot; };

  uvOverlay = workspace.mkPyprojectOverlay { sourcePreference = "wheel"; };

  pythonSet = (pkgs.callPackage pyproject-nix.build.packages {
    inherit python;
  }).overrideScope (lib.composeManyExtensions [
    pyproject-build-systems.overlays.default # For build tools
    uvOverlay # uv lock dependencies
  ]);

  pythonEnv = pythonSet.mkVirtualEnv envName workspace.deps.default;
in {
  inherit python # For derivation
    pythonSet # For debug use
    pythonEnv # For devShell
    workspace;
}
