# Copyright 2014 Google Inc. All Rights Reserved.
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
    'sb_pedantic_warnings': 1,
  },
  'targets': [
    {
      'target_name': 'media',
      'type': 'static_library',
      'sources': [
        'can_play_type_handler.h',
        'fetcher_buffered_data_source.cc',
        'fetcher_buffered_data_source.h',
        'media_module.cc',
        'media_module.h',
        'media_module_<(sb_media_platform).cc',
        'shell_media_platform_<(sb_media_platform).cc',
        'shell_media_platform_<(sb_media_platform).h',
        'shell_video_data_allocator_common.cc',
        'shell_video_data_allocator_common.h',
        'web_media_player_factory.h',
      ],
      'dependencies': [
        '<(DEPTH)/cobalt/network/network.gyp:network',
        '<(DEPTH)/media/media.gyp:media',
        '<(DEPTH)/nb/nb.gyp:nb',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/media/media.gyp:media',
      ],
      'conditions': [
        ['sb_media_platform == "ps4"', {
          'sources': [
            'decoder_working_memory_allocator_impl_ps4.cc',
            'decoder_working_memory_allocator_impl_ps4.h',
          ],
        }],
        ['enable_map_to_mesh == 1', {
          'defines' : ['ENABLE_MAP_TO_MESH'],
        }],
      ],
    },
  ],
}
