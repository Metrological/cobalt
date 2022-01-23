{
  'targets': [
    {
      'target_name': 'deploy',
      'hard_dependency': 1,
      'type' : 'none',
      'variables': {
        'wpe_platform_dir': [
            'rpi',
        ],
      },
      'includes': [
        '<(DEPTH)/third_party/starboard/wpe/shared/deployment.gypi',
      ],
    },
  ],
}
