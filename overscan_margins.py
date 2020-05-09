#!/usr/bin/python3
#+
# Inkscape plugin to generate shapes highlighting the margins that
# should be left empty for video overscan. Copy this to your Inkscape
# extensions folder, restart Inkscape, and you should see "Overscan"
# appear in the "Video" submenu under the "Extensions" menu. Select this,
# enter percentages of the drawing dimensions for the sizes of the horizontal
# and vertical margins, and this extension will add a new layer to the
# document, with shading in the specified margins indicating the area of
# the design to leave empty to avoid encroaching into the overscan area.
#
# Copyright 2010-2020 Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#-

import sys
import lxml
import inkex

#+
# Useful stuff
#-

def new_layer(svg, layer_name) :
    # adds a new layer to the SVG document and returns it.
    the_layer = lxml.etree.SubElement(svg, "g")
    the_layer.set(inkex.addNS("groupmode", "inkscape"), "layer")
    the_layer.set(inkex.addNS("label", "inkscape"), layer_name)
    return the_layer
#end new_layer

#+
# The effect
#-

class OverscanEffect(inkex.extensions.EffectExtension) :

    def __init__(self) :
        super().__init__()
        self.arg_parser.add_argument \
          (
            "--horizontal",
            type = float,
            action = "store",
            dest = "hormargin"
          )
        self.arg_parser.add_argument \
          (
            "--vertical",
            type = float,
            action = "store",
            dest = "vertmargin"
          )
    #end __init__

    def effect(self) :
        # actually performs the effect
        svg = self.document.getroot()
        drawing_width = inkex.units.convert_unit(svg.attrib["width"], "pt")
        drawing_height = inkex.units.convert_unit(svg.attrib["height"], "pt")
        hor_margin = drawing_width * self.options.hormargin / 100
        vert_margin = drawing_width * self.options.vertmargin /100
        the_layer = new_layer \
          (
            svg,
            "Overscan %.2f%%x%.2f%%" % (self.options.hormargin, self.options.vertmargin)
          )
        lxml.etree.SubElement \
          (
            the_layer,
            inkex.addNS("path", "svg"),
            {
                "style" : inkex.Style({"stroke" : "none", "fill" : "#808080"}).to_str(),
                "d" :
                    "".join
                      ((
                        "M%.2f %.2f" % (0, 0),
                        "H%.2f" % drawing_width,
                        "V%.2f" % drawing_height,
                        "H%.2f" % 0,
                        # "V%.2f" % 0,
                        "Z",
                        "M %.2f %.2f" % (hor_margin, vert_margin),
                        "V%.2f" % (drawing_height - vert_margin),
                        "H%.2f" % (drawing_width - hor_margin),
                        "V%.2f" % vert_margin,
                        # "H%.2f" % hor_margin,
                        "Z",
                      )),
            }
          )
    #end effect

#end OverscanEffect

#+
# Mainline
#-

OverscanEffect().run()
