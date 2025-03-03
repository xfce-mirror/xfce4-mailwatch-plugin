mailwatch_core_sources = [
  'mailwatch-common.c',
  'mailwatch-common.h',
  'mailwatch-mailbox-gmail.c',
  'mailwatch-mailbox.h',
  'mailwatch-mailbox-imap.c',
  'mailwatch-mailbox-maildir.c',
  'mailwatch-mailbox-mbox.c',
  'mailwatch-mailbox-mh.c',
  'mailwatch-mailbox-pop3.c',
  'mailwatch-net-conn.c',
  'mailwatch-net-conn.h',
  'mailwatch-utils.c',
  'mailwatch-utils.h',
  'mailwatch.c',
  'mailwatch.h',
  xfce_revision_h,
]

if have_ssl_support
  mailwatch_core_sources += [
    'mailwatch-mailbox-gmail.c',
  ]
endif

mailwatch_core = static_library(
  'mailwatch_core',
  mailwatch_core_sources,
  include_directories: [
    include_directories('..'),
  ],
  dependencies: [
    glib,
    gnutls,
    gthread,
    gtk,
    libxfce4ui,
    libxfce4util,
  ],
  install: false,
)
