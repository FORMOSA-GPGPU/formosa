-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

function xmake_address_map()
  return {
    completion_slot_count = 13,
    cp_rom_base = 0x12345000,
    cp_rom_size = 0x2345,
    cp_tcm_base = 0xABC000,
    cp_tcm_size = 0x3456,
    system_info_version = 77,
  }
end
