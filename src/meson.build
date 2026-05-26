vcodex_sources = [
  'main.c',
  'vcodex_window.c',
  'editor_tab.c',
  'file_explorer.c',
  'explorer_context_menu.c',
  'explorer_dnd.c',
  'search_panel.c',
  'terminal_panel.c',
  'bottom_panel.c',
  'layout_controls.c',
  'ai_settings.c',
  'ai_panel.c',
  'ai_agent.c',
  'ai_tools.c',
  'lsp_client.c'
]

vcodex_deps = [
  gtk_dep,
  gtksourceview_dep,
  gio_dep,
  vte_dep,
  soup_dep,
  json_dep
]

inc = include_directories('../include', '.')

executable('vcodex',
  vcodex_sources,
  include_directories: inc,
  dependencies: vcodex_deps,
  install: true
)
