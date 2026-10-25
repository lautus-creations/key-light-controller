# SPDX-FileCopyrightText: 2026 DrEvan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Keeps the build machine's home folder out of the firmware image.
# Framework code embeds __FILE__ in its log and assert messages; this
# flag rewrites the PlatformIO core path to a neutral prefix.

Import("env")

core_dir = env.GetProjectConfig().get("platformio", "core_dir")
core_dir = core_dir.replace("\\", "/").rstrip("/")

if core_dir:
    env.Append(CCFLAGS=["-ffile-prefix-map=%s=PIO" % core_dir])
