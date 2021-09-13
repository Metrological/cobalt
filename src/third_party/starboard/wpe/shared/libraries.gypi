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
{
  'variables': {
    'has_ocdm': '<!(echo $COBALT_HAS_OCDM)',
    'has_provision': '<!(echo $COBALT_HAS_PROVISION)',
    'has_waylandsink': '<!(echo $COBALT_HAS_WAYLANDSINK)',
    'common_libs': [
      '-lpthread',
    ],
    'pkg_libs': [
      'WPEFrameworkCore',
      'WPEFrameworkDefinitions',
      'WPEFrameworkPlugins',
      'compositorclient',
      'gstreamer-1.0',
      'gstreamer-app-1.0',
      'gstreamer-base-1.0',
      'gstreamer-video-1.0',
      'gstreamer-audio-1.0',
      'glib-2.0',
      'gobject-2.0',
    ],
    'common_linker_flags': [
      '-Wl,--wrap=eglGetDisplay',
    ],
    'conditions': [
      ['<(has_ocdm)==1', {
        'pkg_libs': [
          'ocdm',
        ],
      }],
      ['<(has_provision)==1', {
        'pkg_libs': [
          'provisionproxy',
        ],
      }],
      ['<(has_waylandsink)==1', {
          'common_libs': [
            '-lgstwayland-1.0',
          ],
      }],
    ],
  },
}
