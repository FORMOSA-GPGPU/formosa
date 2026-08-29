# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(daemon.lua)
lv_runfile(apccas2026-lpddr4.json)
lv_runfile(simconfig.json)

# apccas2026-lpddr4.json composes DRAMSys' own default sub-configs by bare
# filename. DRAMSys 5.6 resolves those relative to the base config's directory
# (5.0 looked them up under DRAMSYS_RESOURCE_DIR/<type>/), so stage the
# referenced defaults from the DRAMSys submodule next to the base config in
# the build tree. Keep this list in sync with apccas2026-lpddr4.json.
#
# simconfig.json is not one of these: it's a local copy of DRAMSys'
# simconfig/gem5_se.json with DatabaseRecording turned off. Upstream's
# default has it on, which makes every run open a SQLite trace database
# (DRAMSys_*.tdb) in the working directory; parallel jobs sharing a working
# directory then collide on that file with "database is locked". We don't
# use the trace database, so keep recording disabled instead of staging
# DRAMSys' own simconfig.
set(_dramsys_configs "${formosa_SOURCE_DIR}/third-party/DRAMSys/configs")
foreach(_cfg
    addressmapping/am_lpddr4_1Gbx16_robaco.json
    mcconfig/fr_fcfs.json
    memspec/JEDEC_1Gbx16_LPDDR4-1600.json)
  get_filename_component(_name "${_cfg}" NAME)
  configure_file("${_dramsys_configs}/${_cfg}"
                 "${CMAKE_CURRENT_BINARY_DIR}/${_name}" COPYONLY)
endforeach()
