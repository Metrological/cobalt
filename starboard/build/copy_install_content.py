# Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

# TODO(b/216341587): Refactor install_content to get rid of this file.

# This file is loosely based on starboard/build/copy_data.py
"""Copies all input files to the output directory.

The folder structure of the input files is maintained in the output relative
to the 'base_dir' parameter.
"""

import argparse
import os
import shutil


class InvalidArgumentException(Exception):
  pass


def copy_files(files_to_copy, base_dir, output_dir):
  for path in files_to_copy:
    # All input paths must point at files.
    if not os.path.isfile(path):
      raise InvalidArgumentException(path + ' is not a file.')

    # Get the path of the file relative to the source base_dir.
    rel_path = os.path.relpath(path, base_dir)

    # In certain cases, files would fail to open on windows if relative paths
    # were provided.  Using absolute paths fixes this.
    filename = os.path.abspath(os.path.join(base_dir, rel_path))
    output_dir = os.path.abspath(output_dir)
    output_filename = os.path.abspath(os.path.join(output_dir, rel_path))

    # In cases where a directory has turned into a file or vice versa, delete it
    # before copying it below.
    if os.path.exists(output_dir) and not os.path.isdir(output_dir):
      os.remove(output_dir)
    if os.path.exists(output_filename) and os.path.isdir(output_filename):
      shutil.rmtree(output_filename)

    if not os.path.exists(output_dir):
      os.makedirs(output_dir)

    shutil.copy(filename, output_filename)


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--output_dir', dest='output_dir', required=True, help='output directory')
  parser.add_argument(
      '--base_dir',
      dest='base_dir',
      required=True,
      help='source base directory')
  parser.add_argument(
      'input_paths', metavar='path', nargs='+', help='path to an input file')
  options = parser.parse_args()

  copy_files(options.input_paths, options.base_dir, options.output_dir)
