project('cubecalc', 'c',
  version: '0.2.0-dev',
  default_options: ['b_lto=true', 'c_std=gnu11'])

cc = meson.get_compiler('c')

deps = [
  dependency('GL'),
  dependency('glfw3'),

  # not every platform has a dedicated math library. this finds it, optionally
  cc.find_library('m', required : false),
]
extra_args = []

incdir = include_directories('thirdparty')

if cc.get_id() != 'emcc'
  if get_option('enable-threads')
    deps += [
      dependency('threads'),
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
else
  extra_args += [
    '-sUSE_WEBGL2=1',
    '-sUSE_GLFW=3',
    '-sFULL_ES2',
    '--cache ./emcc-cache',
    '-sALLOW_MEMORY_GROWTH',
    '-lidbfs.js',
    '-sEXPORTED_FUNCTIONS=_main,_storageAfterInit,_storageAfterCommit',
  ]

  st_args = [
    '-DNO_MULTITHREAD',
  ]

  if get_option('enable-threads')
    mt_args = [
      '-sWASM_WORKERS',
      '-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency+1',
    ]
  else
    mt_args = st_args
  endif

  nowasm_args = [
    '-sWASM=0',
    '-sLEGACY_VM_SUPPORT=1',
    '-sMIN_IE_VERSION=11',
  ]

  if get_option('enable-wasm')
    wasm_args = [
      '-sWASM=1',
    ]
  else
    wasm_args = nowasm_args
  endif

  mt_deps = [
    dependency('threads'),
  ]

  executable('main', 'compilation-units/monolith.c',
    dependencies: deps + mt_deps,
    c_args : extra_args + mt_args + wasm_args,
    install_dir : get_option('web-dir'),
    include_directories : incdir,
  )

  executable('main-singlethread', 'compilation-units/monolith.c',
    dependencies: deps,
    c_args : extra_args + st_args + wasm_args,
    install_dir : get_option('web-dir'),
    include_directories : incdir,
  )

  executable('main-nowasm', 'compilation-units/monolith.c',
    dependencies: deps,
    c_args : extra_args + st_args + nowasm_args,
    install_dir : get_option('web-dir'),
    include_directories : incdir,
  )

  install_data(
    [
      'index.html',
      'coi-serviceworker.js',
    ],
    install_dir : get_option('web-dir'),
  )
endif
