=====================================
Extra Clang Tools 7.0.0 Release Notes
=====================================

.. contents::
   :local:
   :depth: 3

Written by the `LLVM Team <https://llvm.org/>`_


Introduction
============

This document contains the release notes for the Extra Clang Tools, part of the
Clang release 7.0.0. Here we describe the status of the Extra Clang Tools in
some detail, including major improvements from the previous release and new
feature work. All LLVM releases may be downloaded from the `LLVM releases web
site <https://llvm.org/releases/>`_.

For more information about Clang or LLVM, including information about
the latest release, please see the `Clang Web Site <https://clang.llvm.org>`_ or
the `LLVM Web Site <https://llvm.org>`_.

What's New in Extra Clang Tools 7.0.0?
======================================

Some of the major new features and improvements to Extra Clang Tools are listed
here. Generic improvements to Extra Clang Tools as a whole or to its underlying
infrastructure are described first, followed by tool-specific sections.

Improvements to clang-tidy
--------------------------

- The checks profiling info can now be stored as JSON files for futher
  post-processing and analysis.

- New module `abseil` for checks related to the `Abseil <https://abseil.io>`_
  library.

- New module ``portability``.

- New module ``zircon`` for checks related to Fuchsia's Zircon kernel.

- New :doc:`abseil-string-find-startswith
  <clang-tidy/checks/abseil-string-find-startswith>` check.

  Checks whether a ``std::string::find()`` result is compared with 0, and
  suggests replacing with ``absl::StartsWith()``.

- New :doc:`android-comparison-in-temp-failure-retry
  <clang-tidy/checks/android-comparison-in-temp-failure-retry>` check.

  Diagnoses comparisons that appear to be incorrectly placed in the argument to
  the ``TEMP_FAILURE_RETRY`` macro.

- New :doc:`bugprone-exception-escape
  <clang-tidy/checks/bugprone-exception-escape>` check

  Finds functions which may throw an exception directly or indirectly, but they
  should not.

- New :doc:`bugprone-parent-virtual-call
  <clang-tidy/checks/bugprone-parent-virtual-call>` check.

  Detects and fixes calls to grand-...parent virtual methods instead of calls
  to overridden parent's virtual methods.

- New :doc:`bugprone-terminating-continue
  <clang-tidy/checks/bugprone-terminating-continue>` check

  Checks if a ``continue`` statement terminates the loop.

- New :doc:`bugprone-throw-keyword-missing
  <clang-tidy/checks/bugprone-throw-keyword-missing>` check.

  Diagnoses when a temporary object that appears to be an exception is
  constructed but not thrown.

- New :doc:`bugprone-unused-return-value
  <clang-tidy/checks/bugprone-unused-return-value>` check.

  Warns on unused function return values.

- New :doc:`cert-msc32-c
  <clang-tidy/checks/cert-msc32-c>` check

  Detects inappropriate seeding of ``srand()`` function.

- New :doc:`cert-msc51-cpp
  <clang-tidy/checks/cert-msc51-cpp>` check

  Detects inappropriate seeding of C++ random generators and C ``srand()`` function.

- New :doc:`cppcoreguidelines-avoid-goto
  <clang-tidy/checks/cppcoreguidelines-avoid-goto>` check.

  The usage of ``goto`` for control flow is error prone and should be replaced
  with looping constructs. Every backward jump is rejected. Forward jumps are
  only allowed in nested loops.

- New :doc:`cppcoreguidelines-narrowing-conversions
  <clang-tidy/checks/cppcoreguidelines-narrowing-conversions>` check

  Checks for narrowing conversions, e.g. ``int i = 0; i += 0.1;``.

- New :doc:`fuchsia-multiple-inheritance
  <clang-tidy/checks/fuchsia-multiple-inheritance>` check.

  Warns if a class inherits from multiple classes that are not pure virtual.

- New `fuchsia-restrict-system-includes
  <https://clang.llvm.org/extra/clang-tidy/checks/fuchsia-restrict-system-includes.html>`_ check

  Checks for allowed system includes and suggests removal of any others.

- New `fuchsia-statically-constructed-objects
  <https://clang.llvm.org/extra/clang-tidy/checks/fuchsia-statically-constructed-objects.html>`_ check

  Warns if global, non-trivial objects with static storage are constructed,
  unless the object is statically initialized with a ``constexpr`` constructor
  or has no explicit constructor.

- New :doc:`fuchsia-trailing-return
  <clang-tidy/checks/fuchsia-trailing-return>` check.

  Functions that have trailing returns are disallowed, except for those
  using ``decltype`` specifiers and lambda with otherwise unutterable
  return types.

- New :doc:`hicpp-multiway-paths-covered
  <clang-tidy/checks/hicpp-multiway-paths-covered>` check.

  Checks on ``switch`` and ``if`` - ``else if`` constructs that do not cover all possible code paths.

- New :doc:`modernize-use-uncaught-exceptions
  <clang-tidy/checks/modernize-use-uncaught-exceptions>` check.

  Finds and replaces deprecated uses of ``std::uncaught_exception`` to
  ``std::uncaught_exceptions``.

- New :doc:`portability-simd-intrinsics
  <clang-tidy/checks/portability-simd-intrinsics>` check.

  Warns or suggests alternatives if SIMD intrinsics are used which can be replaced by
  ``std::experimental::simd`` operations.

- New :doc:`readability-simplify-subscript-expr
  <clang-tidy/checks/readability-simplify-subscript-expr>` check.

  Simplifies subscript expressions like ``s.data()[i]`` into ``s[i]``.

- New :doc:`zircon-temporary-objects
  <clang-tidy/checks/zircon-temporary-objects>` check.

  Warns on construction of specific temporary objects in the Zircon kernel.

- Added the missing bitwise assignment operations to
  :doc:`hicpp-signed-bitwise <clang-tidy/checks/hicpp-signed-bitwise>`.

- New option `MinTypeNameLength` for :doc:`modernize-use-auto
  <clang-tidy/checks/modernize-use-auto>` check to limit the minimal length of
  type names to be replaced with ``auto``. Use to skip replacing short type
  names like ``int``/``bool`` with ``auto``. Default value is 5 which means
  replace types with the name length >= 5 letters only (ex. ``double``,
  ``unsigned``).

- Add `VariableThreshold` option to :doc:`readability-function-size
  <clang-tidy/checks/readability-function-size>` check.

  Flags functions that have more than a specified number of variables declared
  in the body.

- The `AnalyzeTemporaryDtors` option was removed, since the corresponding
  `cfg-temporary-dtors` option of the Static Analyzer now defaults to `true`.

- New alias :doc:`fuchsia-header-anon-namespaces
  <clang-tidy/checks/fuchsia-header-anon-namespaces>` to :doc:`google-build-namespaces
  <clang-tidy/checks/google-build-namespaces>`
  added.

- New alias :doc:`hicpp-avoid-goto
  <clang-tidy/checks/hicpp-avoid-goto>` to :doc:`cppcoreguidelines-avoid-goto
  <clang-tidy/checks/cppcoreguidelines-avoid-goto>`
  added.

- Removed the `google-readability-redundant-smartptr-get` alias of the
  :doc:`readability-redundant-smartptr-get
  <clang-tidy/checks/readability-redundant-smartptr-get>` check.

- The 'misc-forwarding-reference-overload' check was renamed to :doc:`bugprone-forwarding-reference-overload
  <clang-tidy/checks/bugprone-forwarding-reference-overload>`

- The 'misc-incorrect-roundings' check was renamed to :doc:`bugprone-incorrect-roundings
  <clang-tidy/checks/bugprone-incorrect-roundings>`

- The 'misc-lambda-function-name' check was renamed to :doc:`bugprone-lambda-function-name
  <clang-tidy/checks/bugprone-lambda-function-name>`

- The 'misc-macro-parentheses' check was renamed to :doc:`bugprone-macro-parentheses
  <clang-tidy/checks/bugprone-macro-parentheses>`

- The 'misc-macro-repeated-side-effects' check was renamed to :doc:`bugprone-macro-repeated-side-effects
  <clang-tidy/checks/bugprone-macro-repeated-side-effects>`

- The 'misc-misplaced-widening-cast' check was renamed to :doc:`bugprone-misplaced-widening-cast
  <clang-tidy/checks/bugprone-misplaced-widening-cast>`

- The 'misc-sizeof-container' check was renamed to :doc:`bugprone-sizeof-container
  <clang-tidy/checks/bugprone-sizeof-container>`

- The 'misc-sizeof-expression' check was renamed to :doc:`bugprone-sizeof-expression
  <clang-tidy/checks/bugprone-sizeof-expression>`

- The 'misc-string-compare' check was renamed to :doc:`readability-string-compare
  <clang-tidy/checks/readability-string-compare>`

- The 'misc-string-integer-assignment' check was renamed to :doc:`bugprone-string-integer-assignment
  <clang-tidy/checks/bugprone-string-integer-assignment>`

- The 'misc-string-literal-with-embedded-nul' check was renamed to :doc:`bugprone-string-literal-with-embedded-nul
  <clang-tidy/checks/bugprone-string-literal-with-embedded-nul>`

- The 'misc-suspicious-enum-usage' check was renamed to :doc:`bugprone-suspicious-enum-usage
  <clang-tidy/checks/bugprone-suspicious-enum-usage>`

- The 'misc-suspicious-missing-comma' check was renamed to :doc:`bugprone-suspicious-missing-comma
  <clang-tidy/checks/bugprone-suspicious-missing-comma>`

- The 'misc-suspicious-semicolon' check was renamed to :doc:`bugprone-suspicious-semicolon
  <clang-tidy/checks/bugprone-suspicious-semicolon>`

- The 'misc-suspicious-string-compare' check was renamed to :doc:`bugprone-suspicious-string-compare
  <clang-tidy/checks/bugprone-suspicious-string-compare>`

- The 'misc-swapped-arguments' check was renamed to :doc:`bugprone-swapped-arguments
  <clang-tidy/checks/bugprone-swapped-arguments>`

- The 'misc-undelegated-constructor' check was renamed to :doc:`bugprone-undelegated-constructor
  <clang-tidy/checks/bugprone-undelegated-constructor>`

- The 'misc-unused-raii' check was renamed to :doc:`bugprone-unused-raii
  <clang-tidy/checks/bugprone-unused-raii>`

- The 'google-runtime-member-string-references' check was removed.
