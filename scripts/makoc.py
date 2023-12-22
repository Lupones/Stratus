#!/usr/bin/env python3
# Script to convert template.mako file to config file

import sys
import yaml
import argparse
from mako.template import Template
from mako.lookup import TemplateLookup
from mako.exceptions import RichTraceback


def main():
    parser = argparse.ArgumentParser(description = 'Line plot of csv files.')
    parser.add_argument('template', help='Mako template.')
    parser.add_argument('--lookup', default="templates", help='Lookup dir.')
    parser.add_argument('--defs', type=yaml.safe_load, help='Dict with variables passed to the temlate.', default=dict())
    args = parser.parse_args()

    lookup = TemplateLookup(directories=[args.lookup])
    template = Template(filename=args.template, lookup=lookup)
    try:
        print(template.render(**args.defs))
    except:
        traceback = RichTraceback()
        for (filename, lineno, function, line) in traceback.traceback:
            print("File %s, line %s, in %s" % (filename, lineno, function))
            print(line, "\n")


if __name__ == "__main__":
        main()

