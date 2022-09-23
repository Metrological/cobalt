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
from starboard.build import platform_configuration
from starboard.tools import build
from starboard.tools.testing import test_filter
from starboard.tools.toolchain import ar
from starboard.tools.toolchain import bash
from starboard.tools.toolchain import clang
from starboard.tools.toolchain import clangxx
from starboard.tools.toolchain import cp
from starboard.tools.toolchain import touch

# Use a bogus path instead of None so that anything based on COBALT_STAGING_DIR and
# COBALT_TOOLCHAIN_PRFIX won't inadvertently end up pointing to something in the root directory,
# and this will show up in an error message when that fails.
_UNDEFINED_COBALT_STAGING_DIR = '/UNDEFINED/COBALT_STAGING_DIR'
_UNDEFINED_COBALT_TOOLCHAIN_PREFIX = '/UNDEFINED/COBALT_TOOLCHAIN_PREFIX'

class WpePlatformConfig(platform_configuration.PlatformConfiguration):
  """Starboard Raspberry Pi platform configuration."""

  def __init__(self, platform, sabi_json_path='starboard/sabi/default/sabi.json'):
    super(WpePlatformConfig, self).__init__(platform)
    self.AppendApplicationConfigurationPath(os.path.dirname(__file__))
    self.build_home = os.environ.get('COBALT_STAGING_DIR', _UNDEFINED_COBALT_STAGING_DIR)
    if self.build_home != _UNDEFINED_COBALT_STAGING_DIR:
      self.sysroot = os.path.realpath(os.path.join(self.build_home, ''))
      self.toolchain = os.environ.get('COBALT_TOOLCHAIN_PREFIX', _UNDEFINED_COBALT_TOOLCHAIN_PREFIX)
      self.sabi_json_path = sabi_json_path

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
        'include_path_platform_deploy_gypi':
            'third_party/starboard/wpe/rpi/platform_deploy.gypi',
    })


    return variables

  def GetEnvironmentVariables(self):
    env_variables = {}

    env_variables.update({
        'CC': self.build_accelerator + ' ' + self.toolchain + 'gcc',
        'CXX': self.build_accelerator + ' ' + self.toolchain + 'g++',
        'CC_host': 'gcc',
        'CXX_host': 'g++',
    })
    return env_variables

  def SetupPlatformTools(self, build_number):
    # Nothing to setup, but validate that COBALT_STAGING_DIR is correct.
    if self.build_home == _UNDEFINED_COBALT_STAGING_DIR:
      raise RuntimeError('Wpe builds require the "COBALT_STAGING_DIR" '
                         'environment variable to be set.')
    if not os.path.isdir(self.sysroot):
      raise RuntimeError('Wpe builds require COBALT_STAGING_DIR sysroot '
                         'to be a valid directory.')

  def GetTargetToolchain(self, **kwargs):
    environment_variables = self.GetEnvironmentVariables()
    cc_path = environment_variables['CC']
    cxx_path = environment_variables['CXX']

    return [
        clang.CCompiler(path=cc_path),
        clang.CxxCompiler(path=cxx_path),
        clang.AssemblerWithCPreprocessor(path=cc_path),
        ar.StaticThinLinker(),
        ar.StaticLinker(),
        clangxx.ExecutableLinker(path=cxx_path, write_group=True),
        clangxx.SharedLibraryLinker(path=cxx_path),
        cp.Copy(),
        touch.Stamp(),
        bash.Shell(),
    ]

  def GetHostToolchain(self, **kwargs):
    environment_variables = self.GetEnvironmentVariables()
    cc_path = environment_variables['CC_host']
    cxx_path = environment_variables['CXX_host']

    return [
        clang.CCompiler(path=cc_path),
        clang.CxxCompiler(path=cxx_path),
        clang.AssemblerWithCPreprocessor(path=cc_path),
        ar.StaticThinLinker(),
        ar.StaticLinker(),
        clangxx.ExecutableLinker(path=cxx_path, write_group=True),
        clangxx.SharedLibraryLinker(path=cxx_path),
        cp.Copy(),
        touch.Stamp(),
        bash.Shell(),
    ]

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

  def GetPathToSabiJsonFile(self):
    return self.sabi_json_path

def CreatePlatformConfig():
  return WpePlatformConfig('wpe-rpi64', sabi_json_path='starboard/sabi/arm64/sabi-v{sb_api_version}.json')
