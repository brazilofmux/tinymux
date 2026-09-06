#!/usr/bin/expect
#
#   upload.tcl - paste an unformatted test-case installer into a running
#   SmokeMUX and wait for it to say "Uploaded."
#
#   usage: upload.tcl <filename> [port]
#
#   The connection is a plain Tcl socket handed to expect, not a spawned
#   telnet client: telnet has not shipped on macOS since 2017 and is not
#   installed by default on current Linux distributions either, and the
#   only thing the harness ever needed from it was a TCP stream (#2254).
#
#   Exit status is the point of this script.  Makesmoke checks it, and a
#   zero here on a timeout used to mean smoke.flat was built from whatever
#   fraction of the installer had been pasted -- reported as success.
#     0  "Uploaded." seen and @shutdown sent
#     1  could not connect, connection closed early, or timed out
#     2  bad arguments or unreadable file
#
if { 1 != $argc && 2 != $argc } {
    puts stderr "usage: upload.tcl <filename> \[port\]"
    exit 2
}

set remote_server    localhost
set remote_port      2860
if { 2 == $argc } {
    set remote_port [lindex $argv 1]
}
set username         #1
set password         potrzebie

set filename [lindex $argv 0]
if { [catch {open "$filename" r} fp] } {
    puts stderr "upload.tcl: $fp"
    exit 2
}
# Bytes, not text.  The socket below is binary, and a file read as text
# would be re-encoded on the way out -- every non-Latin-1 character in the
# installer (the CJK width and grapheme tests) would arrive as '?'.
fconfigure $fp -translation binary
set file_data [read $fp]
close $fp

if { [catch {socket $remote_server $remote_port} sock] } {
    puts stderr "upload.tcl: cannot connect to $remote_server:$remote_port: $sock"
    exit 1
}
# Raw bytes both ways: the server gets exactly the line endings in the
# file, and its telnet option negotiation arrives as data we ignore.
fconfigure $sock -translation binary -buffering none
spawn -open $sock

send "connect $username $password\n"

set send_slow {100 .001}
send -s "$file_data\n"

# 300s is generous for the installer; SMOKE_UPLOAD_TIMEOUT overrides it,
# which is how the timeout path itself gets tested.
set timeout 300
if { [info exists env(SMOKE_UPLOAD_TIMEOUT)] } {
    set timeout $env(SMOKE_UPLOAD_TIMEOUT)
}
expect {
    "Uploaded." { }
    timeout {
        puts stderr "upload.tcl: timed out after ${timeout}s waiting for 'Uploaded.'"
        exit 1
    }
    eof {
        puts stderr "upload.tcl: connection closed before 'Uploaded.'"
        exit 1
    }
}
send "@shutdown\n"
expect eof
exit 0
