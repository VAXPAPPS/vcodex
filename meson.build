project('vcodex', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'c_std=c11'])

cc = meson.get_compiler('c')

gtk_dep          = dependency('gtk+-3.0')
gtksourceview_dep = dependency('gtksourceview-4')
gio_dep          = dependency('gio-2.0')
vte_dep          = dependency('vte-2.91')
soup_dep         = dependency('libsoup-3.0')
json_dep         = dependency('json-glib-1.0')

# Optional: libsecret for secure API key storage in GNOME Keyring
secret_dep = dependency('libsecret-1', required: false)
if secret_dep.found()
  add_project_arguments('-DHAVE_LIBSECRET', language: 'c')
  message('libsecret found — API keys will be stored in GNOME Keyring')
else
  message('libsecret not found — API keys will be stored in config file (chmod 600)')
endif

subdir('src')
