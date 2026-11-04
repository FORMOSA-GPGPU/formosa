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
    rev = "3fb233e6e83ee6c793fdc6bc58ef71995eced7a4";
    hash = "sha256-2NM9eZ6fCfiQ72B3QdjT1rYvWs76oO6a+8cZn6XS114=";
  };
in python.pkgs.buildPythonPackage {
  pname = "pytorch-dlprim";
  version = "0.2.0-unstable-2026-10-02";
  pyproject = false;

  src = pkgs.fetchFromGitHub {
    owner = "artyom-beilis";
    repo = "pytorch_dlprim";
    rev = "47305aa87aeb8dc450c9a91b559d5784c171b960";
    hash = "sha256-0smcSDGL8cEGsZXQr+CYZbO4Hrb1M7NUdKeB9txq1/E=";
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
