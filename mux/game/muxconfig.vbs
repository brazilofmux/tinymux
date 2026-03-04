'       muxconfig.vbs - constants for mux scripts
'
'       Change these variables as appropriate
'
bin      = ".\bin"
text     = ".\text"
data     = ".\data"
logdir   = "."
gamename = "netmux"
owner    = "mux_admin@your_site.your_domain"
'
'       You should never need to change these.
'
new_db    = gamename & ".db.new"
input_db  = gamename & ".db"
gdbm_db   = gamename
crash_db  = gamename & ".db.CRASH"
save_db   = gamename & ".db.old"
pidfile   = gamename & ".pid"
