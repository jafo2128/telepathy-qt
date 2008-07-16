#!/usr/bin/python
#
# Copyright (C) 2008 Collabora Limited <http://www.collabora.co.uk>
# Copyright (C) 2008 Nokia Corporation
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

from sys import argv
import xml.dom.minidom
from getopt import gnu_getopt

from libtpcodegen import NS_TP, get_descendant_text, get_by_path
from libqt4codegen import format_docstring

class Generator(object):
    def __init__(self, opts):
        try:
            self.group = opts['--group']
            self.outputfile = opts['--outputfile']
            self.namespace = opts['--namespace']
            self.realinclude = opts['--realinclude']
            self.prettyinclude = opts['--prettyinclude']
            self.typesinclude = opts['--typesinclude']
            ifacedom = xml.dom.minidom.parse(opts['--ifacexml'])
            specdom = xml.dom.minidom.parse(opts['--specxml'])
        except KeyError, k:
            assert False, 'Missing required parameter %s' % k.args[0]

        self.output = []
        self.ifacenodes = ifacedom.getElementsByTagName('node')
        self.spec = get_by_path(specdom, "spec")[0]

    def __call__(self):
        self.o("""\
/*
 * This file contains D-Bus client proxy classes generated by qt4-client-gen.py.
 *
 * This file can be distributed under the same terms as the specification from
 * which it was generated.
 */

#include <QDBusAbstractInterface>

#include <%s>
""" % self.typesinclude)

        open(self.outputfile, 'w').write(''.join(self.output))

    def o(self, str):
        self.output.append(str)

if __name__ == '__main__':
    options, argv = gnu_getopt(argv[1:], '',
            ['group=',
             'namespace=',
             'outputfile=',
             'ifacexml=',
             'specxml=',
             'realinclude=',
             'prettyinclude=',
             'typesinclude='])

    Generator(dict(options))()
