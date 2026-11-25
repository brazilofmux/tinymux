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
# Read and write the installer as bytes; it never passes through Tcl's
# character layer.  expect's send encodes through the system encoding
# (LC_ALL=C turns non-Latin-1 text into '?'), and Tcl 8.5 cannot decode
# 4-byte UTF-8 at all (#2289).
fconfigure $fp -translation binary
set file_data [read $fp]
close $fp

if { [catch {socket $remote_server $remote_port} sock] } {
    puts stderr "upload.tcl: cannot connect to $remote_server:$remote_port: $sock"
    exit 1
}
# No newline translation or character encoding on the socket, so the server
# gets exactly the line endings and UTF-8 bytes from the file.
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

# Write the login and installer on the binary channel before Expect takes
# the socket.  The server's input buffer and command_quota_increment in
# smoke.conf let it handle the paste without client-side pacing.
if {[catch {
    puts -nonewline $sock "connect $username $password\n"
    puts -nonewline $sock $file_data
    puts -nonewline $sock "\n"
    flush $sock
} err]} {
    puts stderr "upload.tcl: failed to send the installer: $err"
    exit 1
}
spawn -open $sock

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
