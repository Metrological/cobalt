# Copyright 2017 The Cobalt Authors. All Rights Reserved.
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
"""Starboard Raspberry Pi platform configuration."""

import os

from starboard.build import clang
from starboard.build import platform_configuration
from starboard.tools import build
from starboard.tools.testing import test_filter

# Use a bogus path instead of None so that anything based on $BUILDROOT_HOME won't
# inadvertently end up pointing to something in the root directory, and this
# will show up in an error message when that fails.
_UNDEFINED_BUILDROOT_HOME = '/UNDEFINED/BUILDROOT_HOME'


class WpePlatformConfig(platform_configuration.PlatformConfiguration):
  """Starboard WPE BRCM ARM platform configuration."""

  def __init__(self, platform):
    super(WpePlatformConfig, self).__init__(platform)
    self.AppendApplicationConfigurationPath(os.path.dirname(__file__))
    self.buildroot_home = os.environ.get('BUILDROOT_HOME', _UNDEFINED_BUILDROOT_HOME)
    self.sysroot = os.path.realpath(os.path.join(self.buildroot_home, 'arm-buildroot-linux-gnueabihf/sysroot'))

  def GetBuildFormat(self):
    """Returns the desired build format."""
    # The comma means that ninja and qtcreator_ninja will be chained and use the
    # same input information so that .gyp files will only have to be parsed
    # once.
    return 'ninja,qtcreator_ninja'

  def GetVariables(self, configuration):
    variables = super(WpePlatformConfig, self).GetVariables(configuration)
    variables.update({
        'clang': 0,
        'sysroot': self.sysroot,
    })
    variables.update({
        'javascript_engine': 'v8',
        'cobalt_enable_jit': 1,
        'include_path_platform_deploy_gypi':
             'third_party/starboard/wpe/brcm/arm/platform_deploy.gypi',
    })


    return variables

  def GetEnvironmentVariables(self):
    env_variables = {}
    toolchain = os.path.realpath(
        os.path.join(
            self.buildroot_home,
            '.'))
    toolchain_bin_dir = os.path.join(toolchain, 'bin')
    env_variables.update({
        'CC': os.path.join(toolchain_bin_dir, 'arm-buildroot-linux-gnueabihf-gcc'),
        'CXX': os.path.join(toolchain_bin_dir, 'arm-buildroot-linux-gnueabihf-g++'),
        'CC_host': 'gcc -m32',
        'CXX_host': 'g++ -m32',
    })

    return env_variables

  def SetupPlatformTools(self, build_number):
    # Nothing to setup, but validate that BUILDROOT_HOME is correct.
    if self.buildroot_home == _UNDEFINED_BUILDROOT_HOME:
      raise RuntimeError('Wpe builds require the "BUILDROOT_HOME" '
                         'environment variable to be set.')
    if not os.path.isdir(self.sysroot):
      raise RuntimeError('Wpe builds require $BUILDROOT_HOME/sysroot '
                         'to be a valid directory.')

  def GetLauncherPath(self):
    """Gets the path to the launcher module for this platform."""
    return os.path.dirname(__file__)

  def GetGeneratorVariables(self, config_name):
    del config_name
    generator_variables = {
        'qtcreator_session_name_prefix': 'cobalt',
    }
    return generator_variables

  def GetTestFilters(self):
    filters = super(WpePlatformConfig, self).GetTestFilters()
    for target, tests in self.__FILTERED_TESTS.iteritems():
      filters.extend(test_filter.TestFilter(target, test) for test in tests)
    return filters

  __FILTERED_TESTS = {
      'nplb': [
#          'SbDrmTest.AnySupportedKeySystems',
          # The Wpe test devices don't have access to an IPV6 network, so
          # disable the related tests.
#          'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest'
#          '.SunnyDayDestination/1',
#          'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest'
#          '.SunnyDaySourceForDestination/1',
#          'SbSocketAddressTypes/SbSocketGetInterfaceAddressTest'
#          '.SunnyDaySourceNotLoopback/1',
      ],
      'player_filter_tests': [
          # TODO: debug these failures.
#          'VideoDecoderTests/VideoDecoderTest.EndOfStreamWithoutAnyInput/0',
#          'VideoDecoderTests/VideoDecoderTest.MultipleResets/0',
#          'VideoDecoderTests/VideoDecoderTest'
#          '.MultipleValidInputsAfterInvalidKeyFrame/*',
#          'VideoDecoderTests/VideoDecoderTest.MultipleInvalidInput/*',
      ],
  }

def CreatePlatformConfig():
  return WpePlatformConfig('wpe-brcm-arm')
