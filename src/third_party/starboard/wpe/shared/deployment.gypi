{
  'variables': {
    'generic_headers': [
      '<(DEPTH)/starboard/*.h',
    ],
    'wpe_shared_headers': [
      '<(DEPTH)/third_party/starboard/wpe/shared/*.h',
    ],
    'wpe_platform_headers': [
        '<(DEPTH)/third_party/starboard/wpe/<(wpe_platform_dir)/*.h',
    ],
    'target_dir': '<!(echo $COBALT_INSTALL_DIR)',
    'product': '<(PRODUCT_DIR)/cobalt',
    'output_product_dir' : '<(sysroot)/usr/bin',
    'install_dir' : '<(target_dir)/usr/bin',
    'install_contents_dir' : '<(target_dir)/usr/share',
    'output_generic_include_dir' : '<(sysroot)/usr/include/starboard',
    'output_wpe_shared_include_dir' : '<(sysroot)/usr/include/third_party/starboard/wpe/shared',
    'output_wpe_platform_include_dir' : '<(sysroot)/usr/include/third_party/starboard/wpe/<(wpe_platform_dir)',
    'conditions': [
      ['final_executable_type == "shared_library"', {
        'product': '<(PRODUCT_DIR)/lib/libcobalt.so',
        'output_product_dir' : '<(sysroot)/usr/lib',
        'install_dir' : '<(target_dir)/usr/lib',
      }],
    ],
  },
  'copies': [
    {
      'destination' : '<(output_generic_include_dir)',
      'files': [
        '<!@pymod_do_main(third_party.starboard.wpe.shared.expand_file_list <(generic_headers))'
      ]
    },
    {
      'destination' : '<(output_wpe_shared_include_dir)',
      'files': [
        '<!@pymod_do_main(third_party.starboard.wpe.shared.expand_file_list <(wpe_shared_headers))'
      ]
    },
    {
      'destination' : '<(output_wpe_platform_include_dir)',
      'files': [
        '<!@pymod_do_main(third_party.starboard.wpe.shared.expand_file_list <(wpe_platform_headers))'
      ]
    },
    {
      'destination' : '<(output_product_dir)',
      'files': [ '<(product)' ]
    },
    {
      'destination' : '<(install_dir)',
      'files': [ '<(product)' ]
    },
  ],
  'actions': [
    {
      'action_name': 'copy_contents',
      'inputs': [
        '<!@pymod_do_main(starboard.build.copy_data --inputs <(sb_static_contents_output_base_dir))',
      ],
      'outputs': [
        '<!@pymod_do_main(starboard.build.copy_data -o <(install_contents_dir) --outputs <(sb_static_contents_output_base_dir))',
      ],
      'action': [
        'python',
        '<(DEPTH)/starboard/build/copy_data.py',
        '-o', '<(install_contents_dir)',
        '<@(sb_static_contents_output_base_dir)',
      ],
    },
  ],
}
