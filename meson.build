project('sched_with_batsim', 'cpp',
  version: '0.1.0',
  license: 'LGPL-3.0',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.40.0'
)

batprotocol_cpp_dep = dependency('batprotocol-cpp')
intervalset_dep = dependency('intervalset')
nlohmann_json_dep = dependency('nlohmann_json')
deps = [
  batprotocol_cpp_dep,
  intervalset_dep,
  nlohmann_json_dep,
]

common = ['src/batsim_edc.h']


easy_variants = shared_library('easy_variants', common + ['src/easy_variants.cpp'],
  dependencies: deps,
  install: true,
)