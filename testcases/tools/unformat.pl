#!/usr/bin/perl
#############################################################################
# Define the above so that it points to your perl program
#############################################################################
#
#             Unformat 1.1
#             Copyright (C) 1997 Adam Dray / adam@fey.netgsi.com
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 675 Mass Ave, Cambridge, MA 02139, USA.
#
# The author can be contacted by emailing adam@fey.netgsi.com or by mail:
#
#                         Adam Dray
#                         c/o NetGSI, Inc.
#                         115 West Bel Air Avenue
#                         Aberdeen, Maryland 21001
#                         USA
#
#
#       What it does:
#
# Basically, this program takes a file containing some pretty formatted
# code (presumably softcode for a MUSH or somesuch), and turns it into
# the terribly hard-to-read, unformatted stuff that the MUSH servers like.
# For example (ignoring my leading '# '), the following code:
#
# &CMD-TEST me = $@test:
#   @switch hasflag(%#, wizard) = 1, {
#     @pemit %# =
#        Test worked. You're a wizard.
#   }, {
#     @pemit %# = You ain't no wizard!
#   }
# -
#
# And turns it into (I'll use \ to signify lines that are continued,
# but note that the unformatter will put the following all on one long
# line):
#
# &CMD-TEST me = $@test: @switch hasflag(%#, wizard) = 1. {@pemit %# = \
# Test worked. You're a wizard.}, {@pemit %# = You ain't no wizard!}
#
#
#
#       How to use it:
#
# I'm assuming you've named this program unformat.pl, and that you've
# put it in a directory that's guaranteed to be in your executable path...
#
# Define a macro in TinyFugue (tf) called /upload in the following manner:
#         /def upload=/quote -0 !unformat.pl %*
#
# Then, in tf, you can upload file 'commands.mux' (for example) by
# typing '/upload commands.mux' into tf.  Commands.mux would contain
# formatted code.
#
#
#       Formatting Rules:
#
# 1. Any line starting with a '#' is a comment, and is totally ignored.
# 2. Any line not starting with white space starts a command.
# 3. Once in a command, whitespace at the beginning of a line is
#    ignored, and subsequent lines are appended to the first until
#    a line containing a '-' as the first and only character is reached.
# 4. Once the '-' marker is reached, the command is output and the
#    unformatter looks for a new command.
# 5. Inline '#' comments are handled properly.
# 6. Exception to comments: a line saying '#include <something>'
#    (with the '#include' at the beginning of the line) includes the
#    entire text of a file called '<something>' at that point.
#
# Multiple files can be unformatted at once by listing them on the
# unformat.pl command line: e.g., '/unformat a.mux b.mux c.mux' will
# unformat and concatenate three files in order.
#
#
#       Warning and Disclaimer:
#
# This program is untested.  I don't guarantee it will work correctly.
# I don't guarantee that it will work at all.  I don't even guarantee
# that it will compile.  Be careful.  If you break something with this,
# it isn't my fault.  You've been warned.
#
#############################################################################
# Configuration stuff
#############################################################################
#
# Define $extraspace if you want \n\n between commands; otherwise,
# comment-out the line.  Extra space is usually ignored by mu* servers.

$extraspace = Yes;

# Output command. Comment-out all but one.
$outputcommand = "think";
# $outputcommand = "/echo -w";
# $outputcommand = "@pemit %# =";

# Command to notify user at end. Comment-out all but one.
$donecommand = "think Uploaded.";
# $donecommand = "/echo -w Uploaded.";
# $donecommand = "@pemit %#=Uploaded.";

#############################################################################
# End of configuration stuff
#############################################################################

# $filetable is a global associative array of files that have been visited


$numargs = $#ARGV+1;

if ($numargs) {
        foreach $file (@ARGV) {
                &command( $file, 'filehandle00');
                print "\n";
        }
} else {
        &command( '', 'file');
}

print "\n", $donecommand, "\n";         # print the $donecommand
exit 0;                                 # and finish



#############################################################################

sub command {
        local($file, $input) = @_;

        $input++;                       # string increment for filename;

        if (!$file) {
                if ( ! open($input, ">&STDIN") ) {
                        print "$outputcommand Can't open stdin.";
                        die "Can't open stdin\n";
                }
        } else {
                unless(open($input, $file)) {
                        print "$outputcommand Can't open $file: $!\n";
                        return;
                }
        }

        GETTEXT:
        while (<$input>) {
                chop;
                next GETTEXT if /^\s*$/;        # skip empty lines

                # handle includes
                if ( /^#include\s+(\S.*)/ ) {
                        if ( !$filetable{$1} ) {
                                $filetable{$1} ++;
                                &command($1, $input);
                        } else {
                                print "$outputcommand Attempted to include file
 '$1' more than once. Ignored.\n";
                                next;
                        }
                }

                if ( /^#.*/ ) {
                        next GETTEXT;   # skip comments
                }
                elsif ( /^\S/ ) {       # start reading a command
                        print;          # print the first part of it

                        GETCOMMAND:     # process the rest of a command
                        while (<$input>) {

                                chop;

                                # end if '-'
                                last GETCOMMAND if /^-$/;

                                # skip comments
                                next GETCOMMAND if /^#/;

                                # handle includes
                                if ( /^#include\s+(\S.*)/ ) {
                                        if ( !$filetable{$1} ) {
                                                $filetable{$1} ++;
                                                &command($1, $input);
                                        } else {
                                                print "$outputcommand Attempted
 to include file '$1' more than once. Ignored.\n";
                                                next;
                                        }
                                }

                                # remove leading space and print the rest
                                /^\s*(.*)/ && print $1;
                        }
                        print "\n";                     # flush with a newline
                        print "\n" if $extraspace;      # or two
                }
        }
        close $input;
}








