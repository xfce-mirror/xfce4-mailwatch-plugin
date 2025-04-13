plugin_sources = [
  'mailwatch-plugin.c',
  xfce_revision_h,
]

plugin_install_subdir = 'xfce4' / 'panel' / 'plugins'

plugin_lib = shared_module(
  'mailwatch',
  plugin_sources,
  gnu_symbol_visibility: 'hidden',
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format('xfce4-mailwatch-plugin'),
  ],
  include_directories: [
    include_directories('..'),
  ],
  dependencies: [
    exo,
    glib,
    gnutls,
    gthread,
    gtk,
    libxfce4panel,
    libxfce4ui,
    libxfce4util,
  ],
  link_with: [
    mailwatch_core,
  ],
  install: true,
  install_dir: get_option('prefix') / get_option('libdir') / plugin_install_subdir,
)

i18n.merge_file(
  input: 'mailwatch.desktop.in',
  output: 'mailwatch.desktop',
  po_dir: '..' / 'po',
  type: 'desktop',
  install: true,
  install_dir: get_option('prefix') / get_option('datadir') / plugin_install_subdir,
)
