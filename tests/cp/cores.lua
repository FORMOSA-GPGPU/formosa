-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Test-local adapters share project namespaces without replacing project modules.
local here = assert(package.searchpath("cp.cores", package.path)):match("^(.*)/")
package.path = package.path .. ";" .. here .. "/cores/?.lua"
