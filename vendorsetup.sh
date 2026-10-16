#!/bin/bash
#
#	This file is part of the PD2415 TWRP device tree
# 	Copyright (C) 2025 The PD2415 TWRP Device Tree Team
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
# 	You should have received a copy of the GNU General Public License
# 	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Default language for the recovery UI
if [ -z "$TW_DEFAULT_LANGUAGE" ]; then
    unset TW_DEFAULT_LANGUAGE
    export TW_DEFAULT_LANGUAGE="zh_CN"
fi

# Build environment
export LC_ALL="C"
export ALLOW_MISSING_DEPENDENCIES=true
export TARGET_DEVICE_ALT="PD2415"

# Max backlight brightness (device reports 0-1200)
export TW_MAX_BRIGHTNESS=1200
