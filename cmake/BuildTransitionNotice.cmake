# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

message(WARNING
  "FORMOSA's CMake source build is deprecated; Xmake is the primary build/test entry.\n"
  "  From the repository root: xmake workflow list\n"
  "  Quick start: xmake f -m debug -o build-xmake --workflow=lv\n"
  "               xmake && xmake test\n"
  "  CMake remains a temporary build/test fallback and the shared Nix SDK producer.\n"
  "  SDK consumers using find_package(Formosa) remain supported.\n"
  "  Transition and fallback instructions: CONTRIBUTING.md (repository root).")
