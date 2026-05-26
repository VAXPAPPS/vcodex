vcodex_sources = [
  'main.c',
  'vcodex_window.c',
  'editor_tab.c',
  'file_explorer.c',
  'search_panel.c',
  'terminal_panel.c',
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
