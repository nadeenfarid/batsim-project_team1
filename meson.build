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

easy_fcfs_fcfs = shared_library('easy_fcfs_fcfs', common + ['src/easy_fcfs_fcfs.cpp'],
  dependencies: deps,
  install: true,
)

easy_lcfs_lcfs = shared_library('easy_lcfs_lcfs', common + ['src/easy_lcfs_lcfs.cpp'],
  dependencies: deps,
  install: true,
)

easy_lpf_lpf = shared_library('easy_lpf_lpf', common + ['src/easy_lpf_lpf.cpp'],
  dependencies: deps,
  install: true,
)

easy_lqf_lqf = shared_library('easy_lqf_lqf', common + ['src/easy_lqf_lqf.cpp'],
  dependencies: deps,
  install: true,
)

easy_spf_spf = shared_library('easy_spf_spf', common + ['src/easy_spf_spf.cpp'],
  dependencies: deps,
  install: true,
)
easy_sqf_sqf = shared_library('easy_sqf_sqf', common + ['src/easy_sqf_sqf.cpp'],
  dependencies: deps,
  install: true,
)

easy_exp_exp = shared_library('easy_exp_exp', common + ['src/easy_exp_exp.cpp'],
  dependencies: deps,
  install: true,
)