#!/usr/bin/expect

if { 1 != $argc && 2 != $argc } {
    puts "usage: upload.tcl <filename> \[port\]"
} else {
    set remote_server    localhost
    set remote_port      2860
    if { 2 == $argc } {
        set remote_port [lindex $argv 1]
    }
    set username         #1
    set password         potrzebie

    set filename [lindex $argv 0]

    set fp [open "$filename" r]
    set file_data [read $fp]
    close $fp

    spawn telnet $remote_server $remote_port
    send "connect $username $password\r"

    set send_slow {100 .001}
    send -s "$file_data\r"

    set timeout 300
    expect "Uploaded."
    send "@shutdown\r"
    expect eof
}
