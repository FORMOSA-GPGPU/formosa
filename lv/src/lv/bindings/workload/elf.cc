// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/output.h>
#include <systemc.h>

#include <elfio/elfio.hpp>
#include <memory>

LV_BINDING_WITH_NAME(workload, ELFIO::elfio, "ELF")
    .constructor(
        [](const char *file_path) {
          auto elfio = std::make_shared<ELFIO::elfio>();
          if (!elfio->load(file_path)) {
            LV_FATAL("Cannot load {}", file_path);
          }
          return elfio;
        },
        lv::params("file_path"), lv::doc("Load an ELF file"))
    .property("entry", &ELFIO::elfio::get_entry,
              lv::doc("ELF entry point address"));
