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
  # AI subsystem — ordered by dependency layer
  'ai_http.c',         # Layer 1: HTTP transport
  'ai_provider.c',     # Layer 2: Provider adapters
  'ai_settings.c',     # Layer 3: Settings (depends on provider)
  'ai_context.c',      # Layer 3: IDE context capture
  'ai_conversation.c', # Layer 3: Conversation persistence
  'ai_tools.c',        # Layer 4: Native tools
  'ai_agent.c',        # Layer 5: Orchestrator
  'ai_panel.c',        # Layer 6: UI
  # Other modules
  'theme_manager.c',
  'lsp_client.c'
]

vcodex_deps = [
  gtk_dep,
  gtksourceview_dep,
  gio_dep,
  vte_dep,
  soup_dep,
  json_dep,
]

# Add libsecret if available
if secret_dep.found()
  vcodex_deps += [secret_dep]
endif

inc = include_directories('../include', '.')

executable('vcodex',
  vcodex_sources,
  include_directories: inc,
  dependencies: vcodex_deps,
  install: true
)
