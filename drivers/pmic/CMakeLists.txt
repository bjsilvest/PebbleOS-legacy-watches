# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources)
if(CONFIG_PMIC_NPM1300)
  list(APPEND sources npm1300.c)
elseif(CONFIG_PMIC_AS3701B)
  list(APPEND sources as3701b.c)
elseif(CONFIG_PMIC_MAX14690)
  list(APPEND sources max14690.c)
endif()
pbl_library()
pbl_library_sources(${sources})
