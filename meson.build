project('slash', 'c',
  version: '0.2.0',
  license: 'MIT',
  default_options: ['c_std=gnu11', 'buildtype=minsize'])

compiler = meson.get_compiler('c')
if compiler.check_header('termios.h')
  add_global_arguments('-DSLASH_HAVE_TERMIOS_H', language: 'c')
endif

linkerscript_dir = join_paths(meson.source_root(), 'linkerscript')
add_global_link_arguments([f'-Wl,-L@linkerscript_dir@', '-Tslash.ld'], language: 'c')

slash_inc = include_directories('include')
slash_lib = library('slash', 'src/slash.c', include_directories: slash_inc)
slash_dep = declare_dependency(link_with: slash_lib, include_directories: slash_inc)

if not meson.is_subproject()
  # Test application
  cmocka_dep = dependency('cmocka')
  slash_test = executable('slash-test', 'test/test.c',  dependencies: [slash_dep, cmocka_dep])
  test('test', slash_test)

  # Example application
  slash_example = executable('slash-example', 'test/example.c', dependencies: slash_dep)
endif
