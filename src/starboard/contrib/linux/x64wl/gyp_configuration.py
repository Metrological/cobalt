# Copyright 2018 The Cobalt Authors. All Rights Reserved.
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
"""Starboard Linux X64 Wayland platform configuration."""

from starboard.linux.shared import gyp_configuration as shared_configuration
from starboard.tools.toolchain import ar
from starboard.tools.toolchain import bash
from starboard.tools.toolchain import clang
from starboard.tools.toolchain import clangxx
from starboard.tools.toolchain import cp
from starboard.tools.toolchain import touch


class LinuxX64WaylandConfiguration(shared_configuration.LinuxConfiguration):
  """Starboard Linux X64 Wayland platform configuration."""

  def __init__(self,
               platform='linux-x64wl',
               asan_enabled_by_default=False,
               sabi_json_path='starboard/sabi/default/sabi.json'):
    # pylint: disable=useless-super-delegation
    super(LinuxX64WaylandConfiguration, self).__init__(platform,
                                                       asan_enabled_by_default,
                                                       sabi_json_path)

  def GetTargetToolchain(self, **kwargs):
    return self.GetHostToolchain(**kwargs)

  def GetHostToolchain(self, **kwargs):
    environment_variables = self.GetEnvironmentVariables()
    cc_path = environment_variables['CC']
    cxx_path = environment_variables['CXX']

    return [
        clang.CCompiler(path=cc_path),
        clang.CxxCompiler(path=cxx_path),
        clang.AssemblerWithCPreprocessor(path=cc_path),
        ar.StaticThinLinker(),
        ar.StaticLinker(),
        clangxx.ExecutableLinker(path=cxx_path),
        cp.Copy(),
        touch.Stamp(),
        bash.Shell(),
    ]


def CreatePlatformConfig():
  return LinuxX64WaylandConfiguration()
