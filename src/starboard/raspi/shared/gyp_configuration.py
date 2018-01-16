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

import config.base
import gyp_utils
from starboard.tools.testing import test_filter


class RaspiPlatformConfig(config.base.PlatformConfigBase):
  """Starboard Raspberry Pi platform configuration."""

  def __init__(self, platform):
    super(RaspiPlatformConfig, self).__init__(platform)

  def _GetRasPiHome(self):
    try:
      raspi_home = os.environ['RASPI_HOME']
    except KeyError:
      logging.critical('RasPi builds require the "RASPI_HOME" '
                       'environment variable to be set.')
      sys.exit(1)
    return raspi_home

  def GetVariables(self, configuration):
    raspi_home = self._GetRasPiHome()
    sysroot = os.path.realpath(os.path.join(raspi_home, 'sysroot'))
    if not os.path.isdir(sysroot):
      logging.critical('RasPi builds require $RASPI_HOME/sysroot '
                       'to be a valid directory.')
      sys.exit(1)
    variables = super(RaspiPlatformConfig, self).GetVariables(configuration)
    variables.update({
        'clang': 0,
        'sysroot': sysroot,
    })

    return variables

  def GetEnvironmentVariables(self):
    env_variables = gyp_utils.GetHostCompilerEnvironment()
    raspi_home = self._GetRasPiHome()

    toolchain = os.path.realpath(os.path.join(
        raspi_home,
        'tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64'))
    toolchain_bin_dir = os.path.join(toolchain, 'bin')
    env_variables.update({
        'CC': os.path.join(toolchain_bin_dir, 'arm-linux-gnueabihf-gcc'),
        'CXX': os.path.join(toolchain_bin_dir, 'arm-linux-gnueabihf-g++'),
    })
    return env_variables

  def WebdriverBenchmarksEnabled(self):
    return True

  def GetTestFilters(self):
    """Gets all tests to be excluded from a unit test run.

    Returns:
      A list of initialized TestFilter objects.
    """
    return [
        # Fails with SpiderMonkey.
        test_filter.TestFilter(
            'bindings_test', ('GlobalInterfaceBindingsTest.'
                              'PropertiesAndOperationsAreOwnProperties')),
        test_filter.TestFilter(
            'net_unittests', 'HostResolverImplDnsTest.DnsTaskUnspec'),

        # The RasPi test devices don't have access to an IPV6 network, so
        # disable the related tests.
        test_filter.TestFilter(
            'nplb', 'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest.'
                    'SunnyDayDestination/1'),
        test_filter.TestFilter(
            'nplb', 'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest.'
                    'SunnyDaySourceForDestination/1'),
        test_filter.TestFilter(
            'nplb', 'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest.'
                    'SunnyDaySourceNotLoopback/1'),

        # These tests are currently producing slightly different images on the
        # RasPi.
        test_filter.TestFilter(
            'renderer_test', 'PixelTest.CircularSubPixelBorder'),
        test_filter.TestFilter(
            'renderer_test', 'PixelTest.FilterBlurred100PxText'),

        test_filter.TestFilter('starboard_platform_tests',
                               test_filter.FILTER_ALL),
        test_filter.TestFilter('nplb_blitter_pixel_tests',
                               test_filter.FILTER_ALL),
        test_filter.TestFilter('web_platform_tests', test_filter.FILTER_ALL)

    ]

  def GetTestEnvVariables(self):
    return {
        'base_unittests': {'ASAN_OPTIONS': 'detect_leaks=0'},
        'crypto_unittests': {'ASAN_OPTIONS': 'detect_leaks=0'},
        'net_unittests': {'ASAN_OPTIONS': 'detect_leaks=0'}
    }
