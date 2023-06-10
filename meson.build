project(
   'libliftoff_rpi',
   'c',
   version: '0.0.4-dev',
   license: 'MIT',
   meson_version: '>=0.52.0',
   default_options: [
      'c_std=c11',
      'warning_level=3',
      'werror=true',
   ],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
   '-Wundef',
   '-Wmissing-prototypes',
   '-Walloca',
   '-Wdeclaration-after-statement',
   '-Wfloat-conversion',
   '-Wsign-conversion',
   '-Wno-missing-braces',
   '-Wno-unused-parameter',
]), language: 'c')

liftoff_rpi_inc = include_directories('include')

drm = dependency('libdrm', include_type: 'system')

liftoff_rpi_deps = [drm]

liftoff_rpi_lib = library(
   'liftoff_rpi',
   files(
      'log.c',
      'list.c',
      'device.c',
      'output.c',
      'layer.c',
      'plane.c',
      'alloc.c',
   ),
   include_directories: liftoff_rpi_inc,
   version: meson.project_version().split('-')[0],
   dependencies: liftoff_rpi_deps,
   install: true,
)

liftoff_rpi = declare_dependency(
   link_with: liftoff_rpi_lib,
   include_directories: liftoff_rpi_inc,
   dependencies: liftoff_rpi_deps,
)

install_headers('include/libliftoff_rpi.h')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
   liftoff_rpi_lib,
   version: meson.project_version(),
   filebase: meson.project_name(),
   name: meson.project_name(),
   description: 'KMS plane library for Raspberry Pi',
)
