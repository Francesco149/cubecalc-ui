project('cubecalc', 'c',
  license : 'unlicense',
  version: '0.2.0-dev',
  default_options: [
    'b_lto=true',
    'b_asneeded=true',
    'c_std=gnu11',
  ])

cc = meson.get_compiler('c')

# not respecting struct aliasing allows me to DRY some stuff.
# I can live without the optimization
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

extra_args = []
link_args = []
incdir = include_directories('thirdparty')
static = get_option('build-config') == 'mingw'

if get_option('build-config') == 'mingw'
  link_args += [ '-fstack-protector' ]
endif

deps = [
  dependency('glfw3', static : static),

  # not every platform has a dedicated math library. this finds it, optionally
  cc.find_library('m', required : false),
]


if get_option('build-config') == 'mingw'
  # threads with static = true is not actually static on wangblows
  deps += [
    cc.find_library('libwinpthread', static : static),
    cc.find_library('libssp', static : static),
  ]
endif

if get_option('enable-threads')
  if get_option('build-config') != 'mingw'
    deps += [
      dependency('threads'),
    ]
  endif
else
  extra_args += [
    '-DNO_MULTITHREAD',
  ]
endif

if get_option('debug') == true
  extra_args += [
    '-DCUBECALC_DEBUG'
  ]
endif

executable('cubecalc-ui', 'compilation-units/monolith.c',
  dependencies: deps,
  c_args : extra_args,
  link_args : link_args,
  include_directories : incdir,
  install : true,
)

if get_option('build-config') == 'default'
  install_data(
    'resources/cubecalc-ui.desktop',
    install_dir : 'share/applications'
  )

  install_data(
    'resources/cubecalc-ui.png',
    install_dir : 'share/icons/hicolor/512x512/apps'
  )

  install_data(
    'resources/cubecalc-ui-48.png',
    install_dir : 'share/icons/hicolor/48x48/apps'
  )
endif
