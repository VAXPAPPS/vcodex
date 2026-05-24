aether_ide_sources = [
  'main.c',
  'aether_ide_window.c',
  'lsp_client.c'
]

aether_ide_deps = [
  gtk_dep,
  gtksourceview_dep,
  gio_dep,
  vte_dep
]

executable('aether-ide',
  aether_ide_sources,
  dependencies: aether_ide_deps,
  install: true
)
