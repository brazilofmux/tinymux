#!/usr/bin/expect

if { 1 != $argc } {
    puts "This upload.tcl script requires a filename"
} else {
    set remote_server    localhost
    set remote_port      2860
    set username         #1
    set password         potrzebie

    set filename [lindex $argv 0]

    set fp [open "$filename" r]
    set file_data [read $fp]
    close $fp

    spawn telnet $remote_server $remote_port
    send "connect $username $password\r"

    send "$file_data\r"

    expect "Uploaded."
    send "@shutdown\r"
    expect eof
}
