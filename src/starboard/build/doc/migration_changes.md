# GYP to GN Migration Changes

This file tracks changes to configuration meta build configuration variables in
the GYP to GN migration. Reference the table below to find the correct GN
equivalent to a changed variable, deprecated GYP variables not in GN, and added
variables.

## Variable Changes

*GYP*                                     | *GN*                                                 | *GN import*
:---------------------------------------- | :--------------------------------------------------- | :----------
`OS` ("starboard"/other)                  | `is_starboard` (true/false)                          | (global)
`clang` (0/1)                             | `is_clang` (true/false)                              | (global)
`sb_deploy_output_dir`                    | `sb_install_output_dir`                              | `//starboard/build/config/base_configuration.gni`
`sb_evergreen` (0/1)                      | `sb_is_evergreen` (true/false)                       | `//starboard/build/config/base_configuration.gni`
`sb_evergreen_compatible` (0/1)           | `sb_is_evergreen_compatible` (true/false)            | `//starboard/build/config/base_configuration.gni`
`sb_evergreen_compatible_libunwind` (0/1) | `sb_evergreen_compatible_use_libunwind` (true/false) | `//starboard/build/config/base_configuration.gni`
`sb_evergreen_compatible_lite` (0/1)      | `sb_evergreen_compatible_enable_lite` (true/false)   | `//starboard/build/config/base_configuration.gni`
`sb_disable_cpp14_audit`                  | (none)                                               |
`sb_disable_microphone_idl`               | (none)                                               |
`starboard_path`                          | (none)                                               |
`tizen_os`                                | (none)                                               |
`includes_starboard`                      | (none)                                               |
(none)                                    | `has_platform_tests` (true/false)                    | `//starboard/build/config/base_configuration.gni`
(none)                                    | `has_platform_targets` (true/false)                  | `//starboard/build/config/base_configuration.gni`
(none)                                    | `install_target_path` (true/false)                   | `//starboard/build/config/base_configuration.gni`

## Other Changes

*GYP*                           | *GN*                                                  | *Notes* (see below)
:------------------------------ | :---------------------------------------------------- | :------------------
`'STARBOARD_IMPLEMENTATION'`    | `"//starboard/build/config:starboard_implementation"` | Starboard Implementation
`optimize_target_for_speed` (0) | `"//starboard/build/config:size"`                     | Optimizations
`optimize_target_for_speed` (1) | `"//starboard/build/config:speed"`                    | Optimizations
`compiler_flags_*_speed`        | `speed_config_path`                                   | Optimizations
`compiler_flags_*_size`         | `size_config_path`                                    | Optimizations

Notes:

*   *Starboard Implementation:* If your platform defined
    `STARBOARD_IMPLENTATION` in its implementation, you would now add the above
    config with `configs +=
    ["//starboard/build/config:starboard_implementation"]`.

*   *Optimizations:* Cobalt defaults to building targets to optimize for size.
    If you need to optimize a target for speed, remove the size config and add
    the speed config with `configs -= [ "//starboard/build/config:size" ]` and
    `configs += [ "//starboard/build/config:speed" ]`. You can define these
    configurations for your platform by creating `config`s and pointing to the
    correct ones for `speed_config_path` and `size_config_path` in your
    platform's `platform_configuration/configuration.gni` file.
