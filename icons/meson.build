names = [
  'xfce-newmail',
  'xfce-nomail',
]

foreach name : names
  install_data(
    '48x48' / '@0@.png'.format(name),
    install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / '48x48' / 'apps',
  )

  install_data(
    'scalable' / '@0@.svg'.format(name),
    install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  )
endforeach
