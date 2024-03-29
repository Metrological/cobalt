# Copyright 2019 The Cobalt Authors. All Rights Reserved.
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
  'targets': [
    {
      'target_name': 'google_benchmark',
      'type': 'static_library',
      'sources': [
        'src/benchmark.cc',
        'src/benchmark_api_internal.cc',
        'src/benchmark_name.cc',
        'src/benchmark_register.cc',
        'src/benchmark_runner.cc',
        'src/colorprint_starboard.cc',
        'src/commandlineflags.cc',
        'src/complexity.cc',
        'src/console_reporter.cc',
        'src/counter.cc',
        'src/csv_reporter.cc',
        'src/json_reporter.cc',
        'src/reporter.cc',
        'src/sleep.cc',
        'src/statistics.cc',
        'src/string_util.cc',
        'src/sysinfo.cc',
        'src/timers.cc',
      ],
      'include_dirs': [
        'include',
      ],
    },
  ],
}
