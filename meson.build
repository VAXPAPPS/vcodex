project('vcodex', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'c_std=c11'])

cc = meson.get_compiler('c')

gtk_dep = dependency('gtk+-3.0')
gtksourceview_dep = dependency('gtksourceview-4')
gio_dep = dependency('gio-2.0')
vte_dep = dependency('vte-2.91')

subdir('src')
