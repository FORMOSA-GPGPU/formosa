-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

set_project("formosa")
set_xmakever("3.0.4")

add_moduledirs(path.join(os.projectdir(), "xmake/modules"))
includes("xmake/tasks/*/xmake.lua")
