#!/usr/bin/python
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
# Copyright 2010 Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
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

from __future__ import division
import sys
sys.path.append("/usr/share/inkscape/extensions")
import inkex
import simplestyle

#+
# Useful stuff
#-

def NewLayer(svg, LayerName) :
    # adds a new layer to the SVG document and returns it.
    TheLayer = inkex.etree.SubElement(svg, "g")
    TheLayer.set(inkex.addNS("groupmode", "inkscape"), "layer")
    TheLayer.set(inkex.addNS("label", "inkscape"), LayerName)
    return TheLayer
#end NewLayer

#+
# The effect
#-

class OverscanEffect(inkex.Effect) :

    def __init__(self) :
        inkex.Effect.__init__(self)
        self.OptionParser.add_option \
          (
            "--horizontal",
            action = "store",
            type = "float",
            dest = "hormargin"
          )
        self.OptionParser.add_option \
          (
            "--vertical",
            action = "store",
            type = "float",
            dest = "vertmargin"
          )
    #end __init__

    def effect(self) :
        # actually performs the effect
        svg = self.document.getroot()
        DrawingWidth = float(svg.attrib["width"])
        DrawingHeight = float(svg.attrib["height"])
        HorMargin = DrawingWidth * self.options.hormargin / 100
        VertMargin = DrawingWidth * self.options.vertmargin /100
        TheLayer = NewLayer \
          (
            svg,
            "Overscan %.2f%%x%.2f%%" % (self.options.hormargin, self.options.vertmargin)
          )
        inkex.etree.SubElement \
          (
            TheLayer,
            inkex.addNS("path", "svg"),
            {
                "style" : simplestyle.formatStyle({"stroke" : "none", "fill" : "#808080"}),
                "d" :
                    "".join
                      ((
                        "M%.2f %.2f" % (0, 0),
                        "H%.2f" % DrawingWidth,
                        "V%.2f" % DrawingHeight,
                        "H%.2f" % 0,
                        # "V%.2f" % 0,
                        "Z",
                        "M %.2f %.2f" % (HorMargin, VertMargin),
                        "V%.2f" % (DrawingHeight - VertMargin),
                        "H%.2f" % (DrawingWidth - HorMargin),
                        "V%.2f" % VertMargin,
                        # "H%.2f" % HorMargin,
                        "Z",
                      )),
            }
          )
    #end effect

#end OverscanEffect

#+
# Mainline
#-

OverscanEffect().affect()
