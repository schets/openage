# Copyright 2013-2014 the openage authors. See copying.md for legal info.

import argparse
from openage.log import set_verbosity

from . import datafile
from . import mediafile

def main():
    # the convert script has 1 mode:
    # mode 0: generate media files
    #         this requires the aoe installation
    #         database files as csv will be generated, as well as usable
    #         media files like .png and .opus.
    #         see `mediafile.py` for the implementation.

    #construct argument parser
    p = argparse.ArgumentParser(description='openage conversion script. allows usage of original media files.')

    #common options
    p.add_argument("-v", "--verbose", help="Turn on verbose log messages", action='count', default=0)
    #p.set_defaults(handler=lambda x: p.print_help())

    #convert script has multiple subsystems
    sp = p.add_subparsers(dest='module', help="available convert subsystems")


    #media conversion:
    media_cmd = sp.add_parser("media", help="convert media files to free formats")

    media_cmd.add_argument("-e", "--extrafiles", help = "Extract extra files that are not needed, but useful (mainly visualizations).", action='store_true')
    media_cmd.add_argument("--no-opus", help="Don't use opus conversion for audio files", action='store_true')
    media_cmd.add_argument("--use-dat-cache", help="Potentially use a pickle cache file for the read empires.dat file", action='store_true')

    mcmd_g0 = media_cmd.add_mutually_exclusive_group(required=True)
    mcmd_g0.add_argument("-o", "--output", metavar="output_directory", help="The data output directory")
    mcmd_g0.add_argument("-l", "--list-files", help="List files in the game archives", action='store_true')

    media_cmd.add_argument("srcdir", help="The Age of Empires II root directory")
    media_cmd.add_argument("extract", metavar="resource", nargs="*", help="A specific extraction rule, such as graphics:*.slp, terrain:15008.slp or *:*.wav. If no rules are specified, *:*.* is assumed")

    #set handler for media conversion
    media_cmd.set_defaults(handler=mediafile.media_convert)

    #actually parse argv and run main
    args = p.parse_args()

    set_verbosity(args.verbose)

    if args.module == None:
        p.print_help()
    else:
        args.handler(args)

if __name__ == "__main__":
    main()
