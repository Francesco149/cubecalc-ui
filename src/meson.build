project('cubecalc', 'c',
  license : 'unlicense',
  version: '0.2.0-dev',
  default_options: [
    'b_lto=true',
    'b_asneeded=true',
    'c_std=gnu11',
  ])

cc = meson.get_compiler('c')

extra_args = []
incdir = include_directories('thirdparty')

deps = [
  dependency('GL'),
  dependency('glfw3'),

  # not every platform has a dedicated math library. this finds it, optionally
  cc.find_library('m', required : false, static : static),
]

if get_option('enable-threads')
  deps += [
    dependency('threads', static : static),
  ]
else
  extra_args += [
    '-DNO_MULTITHREAD',
  ]
endif

executable('cubecalc-ui', 'compilation-units/monolith.c',
  dependencies: deps,
  c_args : extra_args,
  include_directories : incdir,
  install : true,
)

install_data(
  '../resources/cubecalc-ui.desktop',
  install_dir : 'share/applications'
)

install_data(
  '../resources/cubecalc-ui.png',
  install_dir : 'share/icons/hicolor/512x512/apps'
)

install_data(
  '../resources/cubecalc-ui-48.png',
  install_dir : 'share/icons/hicolor/48x48/apps'
)
