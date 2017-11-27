#!/usr/bin/python
#
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
"""Cross-platform unit test runner."""

import importlib
import os
import sys

if "environment" in sys.modules:
  environment = sys.modules["environment"]
else:
  env_path = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                          os.pardir))
  if env_path not in sys.path:
    sys.path.append(env_path)
  environment = importlib.import_module("environment")

import cStringIO
import re
import signal
import subprocess
import threading

import starboard.tools.abstract_launcher as abstract_launcher
import starboard.tools.command_line as command_line
import starboard.tools.testing.test_filter as test_filter

_TOTAL_TESTS_REGEX = (r"\[==========\] (.*) tests? from .*"
                      r"test cases? ran. \(.* ms total\)")
_TESTS_PASSED_REGEX = r"\[  PASSED  \] (.*) tests?"
_TESTS_FAILED_REGEX = r"\[  FAILED  \] (.*) tests?, listed below:"
_SINGLE_TEST_FAILED_REGEX = r"\[  FAILED  \] (.*)"


class TestLineReader(object):
  """Reads lines from the test runner's launcher output via an OS pipe.

  This is used to keep the output in memory instead of having to
  to write it to tempfiles, as well as to write the output to stdout
  in real time instead of dumping the test results all at once later.
  """

  # Both ends of the pipe are provided here because the write end of
  # the pipe needs to be closed when killing the thread. If it is not,
  # the read call in _Readlines will block.
  def __init__(self, read_pipe, write_pipe):
    self.read_pipe = read_pipe
    self.write_pipe = write_pipe
    self.output_lines = cStringIO.StringIO()
    self.stop_event = threading.Event()
    self.reader_thread = threading.Thread(target=self._ReadLines)
    self.reader_thread.start()

  def _ReadLines(self):
    """Continuously reads and stores lines of test output."""
    while not self.stop_event.is_set():
      line = self.read_pipe.readline()
      if line:
        sys.stdout.write(line)
        self.output_lines.write(line)

  def Kill(self):
    """Kills the thread reading lines from the launcher's output."""
    self.stop_event.set()

    # Close the write end of the pipe so that the read end gets EOF
    self.write_pipe.close()
    self.reader_thread.join()
    self.read_pipe.close()

  def GetLines(self):
    """Stops file reading, then returns stored output as a list of lines."""
    self.Kill()
    return self.output_lines.getvalue().split("\n")


class TestRunner(object):
  """Runs unit tests."""

  def __init__(self, platform, config, device_id, single_target,
               args, out_directory):
    self.platform = platform
    self.config = config
    self.device_id = device_id
    self.args = args
    self.out_directory = out_directory

    # If a particular test binary has been provided, configure only that one.
    if single_target:
      self.test_targets = self._GetSingleTestTarget(platform, config,
                                                    single_target)
    else:
      self.test_targets = self._GetTestTargets(platform, config)

  def _GetSingleTestTarget(self, platform, config, single_target):
    """Sets up a single test target for a given platform and configuration.

    Args:
      platform: The platform on which the tests are run, ex. "linux-x64x11"
      config:  The configuration of the binary, ex. "devel" or "qa".
      single_target:  The name of a test target to run.

    Returns:
      A mapping from the test binary name to a list of filters for that binary.
      If the test has no filters, its list is empty.

    Raises:
      RuntimeError:  The specified test binary has been disabled for the given
        platform and configuration.
    """
    gyp_module = abstract_launcher.GetGypModuleForPlatform(platform)
    platform_filters = gyp_module.CreatePlatformConfig().GetTestFilters()

    final_targets = {}
    final_targets[single_target] = []

    for platform_filter in platform_filters:
      if platform_filter.target_name == single_target:
        # Only filter the tests specifying our config or all configs.
        if platform_filter.config == config or not platform_filter.config:
          if platform_filter.test_name == test_filter.FILTER_ALL:
            # If the provided target name has been filtered,
            # nothing will be run.
            sys.stderr.write(
                "\"{}\" has been filtered; no tests will be run.\n".format(
                    platform_filter.target_name))
            del final_targets[platform_filter.target_name]
          else:
            final_targets[single_target].append(
                platform_filter.test_name)

    return final_targets

  def _GetTestTargets(self, platform, config):
    """Collects all test targets for a given platform and configuration.

    Args:
      platform: The platform on which the tests are run, ex. "linux-x64x11".
      config:  The configuration of the binary, ex. "devel" or "qa".

    Returns:
      A mapping from names of test binaries to lists of filters for
        each test binary.  If a test binary has no filters, its list is
        empty.
    """
    gyp_module = abstract_launcher.GetGypModuleForPlatform(platform)
    platform_filters = gyp_module.CreatePlatformConfig().GetTestFilters()

    final_targets = {}

    all_targets = environment.GetTestTargets()
    for target in all_targets:
      final_targets[target] = []

    for platform_filter in platform_filters:
      # Only filter the tests specifying our config or all configs.
      if platform_filter.config == config or not platform_filter.config:
        if platform_filter.test_name == test_filter.FILTER_ALL:
          # Filter the whole test binary
          del final_targets[platform_filter.target_name]
        else:
          final_targets[platform_filter.target_name].append(
              platform_filter.test_name)

    return final_targets

  def _BuildSystemInit(self):
    """Runs GYP on the target platform/config."""
    subprocess.check_call([os.path.abspath(os.path.join(
        os.path.dirname(__file__), os.pardir, os.pardir, os.pardir,
        "cobalt", "build", "gyp_cobalt")), self.platform])

  def _BuildTests(self, ninja_flags):
    """Builds all specified test binaries.

    Args:
      ninja_flags: Command line flags to pass to ninja.
    """
    args_list = ["ninja", "-C",
                 abstract_launcher.DynamicallyBuildOutDirectory(
                     self.platform, self.config)]
    args_list.extend([test_name for test_name in self.test_targets])
    if ninja_flags:
      args_list.append(ninja_flags)
    sys.stderr.write("{}\n".format(args_list))
    # We set shell=True because otherwise Windows doesn't recognize
    # PATH properly.
    #   https://bugs.python.org/issue15451
    # We flatten the arguments to a string because with shell=True, Linux
    # doesn't parse them properly.
    #   https://bugs.python.org/issue6689
    subprocess.check_call(" ".join(args_list), shell=True)

  def _RunTest(self, target_name):
    """Runs a single unit test binary and collects all of the output.

    Args:
      target_name: The name of the test target being run.

    Returns:
      A tuple containing tests results (See "_CollectTestResults()").
    """

    # Set up a pipe for processing test output
    read_fd, write_fd = os.pipe()
    read_pipe = os.fdopen(read_fd, "r")
    write_pipe = os.fdopen(write_fd, "w")

    # Filter the specified tests for this platform, if any
    if self.test_targets[target_name]:
      self.args["target_params"] = ["--gtest_filter=-{}".format(":".join(
          self.test_targets[target_name]))]

    launcher = abstract_launcher.LauncherFactory(
        self.platform, target_name, self.config,
        self.device_id, self.args, output_file=write_pipe,
        out_directory=self.out_directory)

    reader = TestLineReader(read_pipe, write_pipe)
    #  If we need to manually exit the test runner at any point,
    #  ensure that the launcher is killed properly first.
    def Abort(signum, frame):
      launcher.Kill()
      reader.Kill()
      sys.stderr.write("TEST RUN STOPPED VIA MANUAL EXIT\n")
      sys.exit(1)

    signal.signal(signal.SIGINT, Abort)
    sys.stdout.write("Starting {}\n".format(target_name))
    launcher.Run()
    output = reader.GetLines()

    return self._CollectTestResults(output, target_name)

  def _CollectTestResults(self, results, target_name):
    """Collects passing and failing tests for one test binary.

    Args:
      results: A list containing each line of test results.
      target_name: The name of the test target being run.

    Returns:
      A tuple of length 5, of the format (target_name, number_of_total_tests,
        number_of_passed_tests, number_of_failed_tests, list_of_failed_tests).
    """

    total_count = 0
    passed_count = 0
    failed_count = 0
    failed_tests = []

    for idx, line in enumerate(results):
      total_tests_match = re.search(_TOTAL_TESTS_REGEX, line)
      if total_tests_match:
        total_count = int(total_tests_match.group(1))

      passed_match = re.search(_TESTS_PASSED_REGEX, line)
      if passed_match:
        passed_count = int(passed_match.group(1))

      failed_match = re.search(_TESTS_FAILED_REGEX, line)
      if failed_match:
        failed_count = int(failed_match.group(1))
        # Descriptions of all failed tests appear after this line
        failed_tests = self._CollectFailedTests(results[idx + 1:])

    return (target_name, total_count, passed_count, failed_count, failed_tests)

  def _CollectFailedTests(self, lines):
    """Collects the names of all failed tests.

    Args:
      lines: The lines of log output where failed test names are located.

    Returns:
      A list of failed test names.
    """
    failed_tests = []
    for line in lines:
      test_failed_match = re.search(_SINGLE_TEST_FAILED_REGEX, line)
      if test_failed_match:
        failed_tests.append(test_failed_match.group(1))
    return failed_tests

  def _ProcessAllTestResults(self, results):
    """Collects and returns output for all selected tests.

    Args:
      results: List containing tuples corresponding to test results

    Returns:
      True if the test run succeeded, False if not.
    """
    total_run_count = 0
    total_passed_count = 0
    total_failed_count = 0

    # If the number of run or passed tests from a test binary cannot be
    # determined, assume an error occured while running it.
    error = False

    print "\nTEST RUN COMPLETE. RESULTS BELOW:\n"

    for result_set in results:

      target_name = result_set[0]
      run_count = result_set[1]
      passed_count = result_set[2]
      failed_count = result_set[3]
      failed_tests = result_set[4]

      print "{}:".format(target_name)
      if run_count == 0 or passed_count == 0:
        error = True
        print "  ERROR OCCURED DURING TEST RUN (Did the test binary crash?)"
      else:
        print "  TOTAL TESTS RUN: {}".format(run_count)
        total_run_count += run_count
        print "  PASSED: {}".format(passed_count)
        total_passed_count += passed_count
        if failed_count > 0:
          print "  FAILED: {}".format(failed_count)
          total_failed_count += failed_count
          print "\n  FAILED TESTS:"
          for line in failed_tests:
            print "    {}".format(line)
      # Print a single newline to separate results from each test run
      print

    status = "SUCCEEDED"
    result = True

    if total_failed_count > 0 or error:
      status = "FAILED"
      result = False

    print "TEST RUN {}.".format(status)
    print "  TOTAL TESTS RUN: {}".format(total_run_count)
    print "  TOTAL TESTS PASSED: {}".format(total_passed_count)
    print "  TOTAL TESTS FAILED: {}".format(total_failed_count)

    return result

  def BuildAllTests(self, ninja_flags):
    """Runs build step for all specified unit test binaries.

    Args:
      ninja_flags: Command line flags to pass to ninja.

    Returns:
      True if the build succeeds, False if not.
    """
    result = True

    try:
      self._BuildSystemInit()
      self._BuildTests(ninja_flags)
    except subprocess.CalledProcessError as e:
      result = False
      sys.stderr.write("Error occurred during building.\n")
      sys.stderr.write("{}\n".format(e))

    return result

  def RunAllTests(self):
    """Runs all specified test binaries.

    Returns:
      True if the test run succeeded, False if not.
    """
    results = []
    # Sort the targets so they are run in alphabetical order
    for test_target in sorted(self.test_targets.keys()):
      results.append(self._RunTest(test_target))

    return self._ProcessAllTestResults(results)


def main():

  arg_parser = command_line.CreateParser()
  arg_parser.add_argument(
      "-b",
      "--build",
      action="store_true",
      help="Specifies whether to build the tests.")
  arg_parser.add_argument(
      "-r",
      "--run",
      action="store_true",
      help="Specifies whether to run the tests."
      " If both the \"--build\" and \"--run\" flags are not"
      " provided, this is the default.")
  arg_parser.add_argument(
      "--ninja_flags",
      help="Flags to pass to the ninja build system. Provide them exactly"
      " as you would on the command line between a set of double quotation"
      " marks.")

  extra_args = {}
  args = arg_parser.parse_args()
  runner = TestRunner(args.platform, args.config, args.device_id,
                      args.target_name, extra_args,
                      args.out_directory)
  # If neither build nor run has been specified, assume the client
  # just wants to run.
  if not args.build and not args.run:
    args.run = True

  build_success = True
  run_success = True

  if args.build:
    build_success = runner.BuildAllTests(args.ninja_flags)
    # If the build fails, don't try to run the tests.
    if not build_success:
      return 1

  if args.run:
    run_success = runner.RunAllTests()

  # If either step has failed, count the whole test run as failed.
  if not build_success or not run_success:
    return 1
  else:
    return 0

if __name__ == "__main__":
  sys.exit(main())

