# Copyright 2017 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Starboard Raspberry Pi platform configuration for gyp_cobalt."""

import logging
import os
import sys

import config.starboard
import gyp_utils


class RaspiPlatformConfig(config.starboard.PlatformConfigStarboard):
  """Starboard Raspberry Pi platform configuration."""

  def __init__(self, platform):
    super(RaspiPlatformConfig, self).__init__(platform)

  def _GetRasPiHome(self):
    try:
      buildroot_home = os.environ['BUILDROOT_HOME']
    except KeyError:
      logging.critical('RasPi builds require the "BUILDROOT_HOME" '
                       'environment variable to be set.')
      sys.exit(1)
    return buildroot_home

  def GetVariables(self, configuration):
    buildroot_home = self._GetRasPiHome()
    sysroot = os.path.realpath(os.path.join(buildroot_home, 'arm-buildroot-linux-gnueabihf/sysroot'))
    if not os.path.isdir(sysroot):
      logging.critical('RasPi builds require $BUILDROOT_HOME/sysroot '
                       'to be a valid directory.')
      sys.exit(1)
    variables = super(RaspiPlatformConfig, self).GetVariables(configuration)
    variables.update({
        'clang': 0,
        'sysroot': sysroot,
    })

    return variables

  def GetEnvironmentVariables(self):
    env_variables = { }
    buildroot_home = self._GetRasPiHome()

    toolchain = os.path.realpath(os.path.join(
        buildroot_home,
        '.'))
    toolchain_bin_dir = os.path.join(toolchain, 'bin')
    env_variables.update({
        'CC': os.path.join(toolchain_bin_dir, 'arm-buildroot-linux-gnueabihf-gcc'),
        'CXX': os.path.join(toolchain_bin_dir, 'arm-buildroot-linux-gnueabihf-g++'),
    })
    return env_variables
