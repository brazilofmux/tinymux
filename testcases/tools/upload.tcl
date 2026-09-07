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
# Text, decoded as UTF-8, and explicitly so: expect's send encodes what it
# is given as UTF-8 no matter how the channel is configured, so a file
# read as bytes (-translation binary) is re-encoded on the way out and
# every byte >= 0x80 goes on the wire as two (#2260).  Read as real code
# points, the same send reproduces the original bytes exactly.  Naming
# the encoding keeps that true under a C or Latin-1 locale as well.
fconfigure $fp -encoding utf-8
set file_data [read $fp]
close $fp

if { [catch {socket $remote_server $remote_port} sock] } {
    puts stderr "upload.tcl: cannot connect to $remote_server:$remote_port: $sock"
    exit 1
}
# No newline translation on the socket, so the server gets exactly the
# line endings in the file.  Output encoding is expect's affair, not the
# channel's (see the file read above).
fconfigure $sock -translation binary -buffering none

# Telnet charset negotiation (RFC 2066), in plain Tcl before expect takes
# the socket, where the bytes are exact.  A client that never negotiates
# is read as Latin-1 -- default_charset has no UTF-8 value -- so without
# this every non-ASCII byte in the installer is reinterpreted on arrival
# no matter how faithfully it was sent (#2260).  The old telnet client
# never negotiated either; literal UTF-8 in a test source simply never
# worked on this path.  The server offers DO CHARSET and WILL CHARSET on
# connect, then sends a REQUEST list once both sides agree; we answer
# ACCEPTED UTF-8, the exact spelling telnet.cpp compares against.
#
set IAC "\xff"; set DO "\xfd"; set WILL "\xfb"; set SB "\xfa"; set SE "\xf0"
set CHARSET "\x2a"; set REQUEST "\x01"; set ACCEPTED "\x02"
fconfigure $sock -blocking 0
set deadline [expr {[clock milliseconds] + 3000}]
set buf ""
set negotiated 0
set answered_do 0
set answered_will 0
while {[clock milliseconds] < $deadline && !$negotiated} {
    append buf [read $sock]
    if {!$answered_do && [string first "$IAC$DO$CHARSET" $buf] >= 0} {
        puts -nonewline $sock "$IAC$WILL$CHARSET"
        set answered_do 1
    }
    if {!$answered_will && [string first "$IAC$WILL$CHARSET" $buf] >= 0} {
        puts -nonewline $sock "$IAC$DO$CHARSET"
        set answered_will 1
    }
    if {[string first "$IAC$SB$CHARSET$REQUEST" $buf] >= 0} {
        puts -nonewline $sock "$IAC$SB$CHARSET${ACCEPTED}UTF-8$IAC$SE"
        set negotiated 1
    }
    flush $sock
    after 50
}
fconfigure $sock -blocking 1
if {!$negotiated} {
    puts stderr "upload.tcl: charset negotiation did not complete; the server will read non-ASCII test text as Latin-1"
}
spawn -open $sock

send "connect $username $password\n"

# A plain send, not the old `send -s` pacing: that was telnet-era
# throttling, and expect's slow send counts characters while advancing
# bytes, so it padded every non-ASCII chunk with NULs on the wire
# (three of them for one "é中").  The server's input buffering and
# the command_quota_increment in smoke.conf are what absorb the paste.
send "$file_data\n"

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
# The same guard as the wait above: a peer that takes the installer and
# then holds the connection open is a stall, not a success, and
# Makesmoke would otherwise only find out ten seconds later from its
# own pidfile wait, as a symptom rather than the cause (#2269).
expect {
    eof { }
    timeout {
        puts stderr "upload.tcl: timed out after ${timeout}s waiting for the server to close after @shutdown"
        exit 1
    }
}
exit 0
