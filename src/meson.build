vcodex_sources = [
  'main.c',
  'vcodex_window.c',
  'lsp_client.c'
]

vcodex_deps = [
  gtk_dep,
  gtksourceview_dep,
  gio_dep,
  vte_dep
]

executable('vcodex',
  vcodex_sources,
  dependencies: vcodex_deps,
  install: true
)
