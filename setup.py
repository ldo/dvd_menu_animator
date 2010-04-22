#+
# Distutils script to build and install DVD Menu Animator and the Overscan Margins
# plugin for Inkscape. Invoke from the command line in this directory as follows:
#
#     python setup.py install
#
# Written by Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
#-

import distutils.core

distutils.core.setup \
  (
	name = "DVD Menu Animator",
	version = "0.1",
	description = "Tool to build DVD-Video menus",
	author = "Lawrence D'Oliveiro",
	author_email = "ldo@geek-central.gen.nz",
	url = "http://github.com/ldo/dvd_menu_animator",
	scripts = ["dvd_menu_animator"],
	ext_modules =
		[
			distutils.core.Extension
			  (
				name = "spuhelper",
				sources = ["spuhelper.c"],
				libraries = ["png"],
			  ),
		],
	data_files =
		[
			("/usr/share/inkscape/extensions", ["overscan_margins.py", "overscan_margins.inx"]),
		],
  )
