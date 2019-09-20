#!/usr/bin/env python3
#
#===- gen_tests.py - clang-doc test generator ----------------*- python -*--===#
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
"""
clang-doc test generator
==========================

Generates tests for clang-doc given a certain set of flags, a prefix for the
test file, and a given clang-doc binary. Please check emitted tests for
accuracy before using.

To generate all current tests:
- Generate mapper tests:
    gen_tests.py -flag='--dump-mapper' -flag='--doxygen' -prefix mapper

- Generate reducer tests:
    gen_tests.py -flag='--dump-intermediate' -flag='--doxygen' -prefix bc
    
- Generate yaml tests:
    gen_tests.py -flag='--format=yaml' -flag='--doxygen' -prefix yaml
    
This script was written on/for Linux, and has not been tested on any other
platform and so it may not work.

"""

import argparse
import glob
import os
import shutil
import subprocess

RUN_CLANG_DOC = """
// RUN: clang-doc {0} -p %t %t/test.cpp -output=%t/docs
"""
RUN = """
// RUN: {0} %t/{1} | FileCheck %s --check-prefix CHECK-{2}
"""

CHECK = '// CHECK-{0}: '

CHECK_NEXT = '// CHECK-{0}-NEXT: '


def clear_test_prefix_files(prefix, tests_path):
    if os.path.isdir(tests_path):
        for root, dirs, files in os.walk(tests_path):
            for filename in files:
                if filename.startswith(prefix):
                    os.remove(os.path.join(root, filename))


def copy_to_test_file(test_case_path, test_cases_path):
    # Copy file to 'test.cpp' to preserve file-dependent USRs
    test_file = os.path.join(test_cases_path, 'test.cpp')
    shutil.copyfile(test_case_path, test_file)
    return test_file


def run_clang_doc(args, out_dir, test_file):
    # Run clang-doc.
    current_cmd = [args.clangdoc]
    current_cmd.extend(args.flags)
    current_cmd.append('--output=' + out_dir)
    current_cmd.append(test_file)
    print('Running ' + ' '.join(current_cmd))
    return_code = subprocess.call(current_cmd)
    if return_code:
        return 1
    return 0


def get_test_case_code(test_case_path, flags):
    # Get the test case code
    code = ''
    with open(test_case_path, 'r') as code_file:
        code = code_file.read()

    code += RUN_CLANG_DOC.format(flags)
    return code


def get_output(root, out_file, case_out_path, flags, checkname, bcanalyzer):
    output = ''
    run_cmd = ''
    if '--dump-mapper' in flags or '--dump-intermediate' in flags:
        # Run llvm-bcanalyzer
        output = subprocess.check_output(
            [bcanalyzer, '--dump',
             os.path.join(root, out_file)])
        output = output[:output.find('Summary of ')].rstrip()
        run_cmd = RUN.format('llvm-bcanalyzer --dump',
                             os.path.join('docs', 'bc', out_file), checkname)
    else:
        # Run cat
        output = subprocess.check_output(['cat', os.path.join(root, out_file)])
        run_cmd = RUN.format(
            'cat',
            os.path.join('docs', os.path.relpath(root, case_out_path),
                         out_file), checkname)

    # Format output.
    output = output.replace('blob data = \'test\'', 'blob data = \'{{.*}}\'')
    output = CHECK.format(checkname) + output.rstrip()
    output = run_cmd + output.replace('\n',
                                      '\n' + CHECK_NEXT.format(checkname))

    return output + '\n'


def main():
    parser = argparse.ArgumentParser(description='Generate clang-doc tests.')
    parser.add_argument(
        '-flag',
        action='append',
        default=[],
        dest='flags',
        help='Flags to pass to clang-doc.')
    parser.add_argument(
        '-prefix',
        type=str,
        default='',
        dest='prefix',
        help='Prefix for this test group.')
    parser.add_argument(
        '-clang-doc-binary',
        dest='clangdoc',
        metavar="PATH",
        default='clang-doc',
        help='path to clang-doc binary')
    parser.add_argument(
        '-llvm-bcanalyzer-binary',
        dest='bcanalyzer',
        metavar="PATH",
        default='llvm-bcanalyzer',
        help='path to llvm-bcanalyzer binary')
    args = parser.parse_args()

    flags = ' '.join(args.flags)

    clang_doc_path = os.path.dirname(__file__)
    tests_path = os.path.join(clang_doc_path, '..', 'test', 'clang-doc')
    test_cases_path = os.path.join(tests_path, 'test_cases')

    clear_test_prefix_files(args.prefix, tests_path)

    for test_case_path in glob.glob(os.path.join(test_cases_path, '*')):
        if test_case_path.endswith(
                'compile_flags.txt') or test_case_path.endswith(
                    'compile_commands.json'):
            continue

        # Name of this test case
        case_name = os.path.basename(test_case_path).split('.')[0]

        test_file = copy_to_test_file(test_case_path, test_cases_path)
        out_dir = os.path.join(test_cases_path, case_name)

        if run_clang_doc(args, out_dir, test_file):
            return 1

        # Retrieve output and format as FileCheck tests
        all_output = ''
        num_outputs = 0
        for root, dirs, files in os.walk(out_dir):
            for out_file in files:
                # Make the file check the first 3 letters (there's a very small chance
                # that this will collide, but the fix is to simply change the decl name)
                usr = os.path.basename(out_file).split('.')
                # If the usr is less than 2, this isn't one of the test files.
                if len(usr) < 2:
                    continue
                all_output += get_output(root, out_file, out_dir, args.flags,
                                         num_outputs, args.bcanalyzer)
                num_outputs += 1

        # Add test case code to test
        all_output = get_test_case_code(test_case_path,
                                        flags) + '\n' + all_output

        # Write to test case file in /test.
        test_out_path = os.path.join(
            tests_path, args.prefix + '-' + os.path.basename(test_case_path))
        with open(test_out_path, 'w+') as o:
            o.write(all_output)

        # Clean up
        shutil.rmtree(out_dir)
        os.remove(test_file)


if __name__ == '__main__':
    main()
