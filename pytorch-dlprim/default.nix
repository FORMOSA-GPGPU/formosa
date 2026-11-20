# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  python = pkgs.python313;
  # Darwin's OpenCL/opencl.hpp spelling needs the C header alias and C++ headers
  # in the same include tree.
  openclHeaders = pkgs.symlinkJoin {
    name = "dlprim-opencl-headers";
    paths = [ pkgs.opencl-headers pkgs.opencl-clhpp ];
  };
  dlprimitives = pkgs.fetchFromGitHub {
    owner = "artyom-beilis";
    repo = "dlprimitives";
    rev = "331118d3af1892aad1b089e4964101e3269af2ce";
    hash = "sha256-5js6Xab2oYgV6zAOVVDU1dhpT5m1EQtfFR5xUINP9wo=";
  };
in python.pkgs.buildPythonPackage {
  pname = "pytorch-dlprim";
  version = "0.2.0-unstable-2026-11-03";
  pyproject = false;

  src = pkgs.fetchFromGitHub {
    owner = "artyom-beilis";
    repo = "pytorch_dlprim";
    rev = "ae2d35eb09398cb22489b5b164475c6327e48e2b";
    hash = "sha256-v32u5Zex4kZuydr4ofCNJvsstfvEjG5RM12hghOAAP8=";
  };
  postUnpack = ''
    mkdir -p "$sourceRoot/dlprimitives"
    cp -r ${dlprimitives}/. "$sourceRoot/dlprimitives/"
    chmod -R u+w "$sourceRoot/dlprimitives"
  '';

  nativeBuildInputs = [ pkgs.cmake pkgs.ninja python.pkgs.pybind11 ];
  buildInputs = [
    openclHeaders
    pkgs.ocl-icd
    pkgs.sqlite
    (pkgs.lib.getDev python.pkgs.torch)
  ];
  propagatedBuildInputs = [ python.pkgs.torch ];
  # Build the Python extension and Formosa's extra ATen operators in one module
  # while keeping both fetched projects' source files unchanged.
  cmakeDir = ./.;
  preConfigure = ''
    cmakeFlagsArray+=("-DPYTORCH_DLPRIM_SOURCE_DIR=$PWD")
  '';
  cmakeFlags = [
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    "-DOCL_PATH=${openclHeaders}/include"
    "-DOCL_LIB=${
      pkgs.lib.getLib pkgs.ocl-icd
    }/lib/libOpenCL${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}"
  ];
  postInstall = ''
    mkdir -p "$out/${python.sitePackages}"
    mv "$out/python/pytorch_ocl" "$out/${python.sitePackages}/"
    rmdir "$out/python"
  '';

  # OpenCL execution is checked by Formosa's simulator tests, outside the builder.
  doCheck = false;
  pythonImportsCheck = [ "pytorch_ocl" ];
  meta.license = pkgs.lib.licenses.mit;
}
