'       muxconfig.vbs - constants for mux scripts
'
'       Change these variables as appropriate
'
bin      = ".\bin"
text     = ".\text"
data     = ".\data"
gamename = "tinymux"
owner    = "mux_admin@your_site.your_domain"
'
'       If you use compression, uncomment this and put the extension here.
'
'compression=.gz
compression = ""
'
'       You should never need to change these.
'
new_db    = gamename & ".db.new" & compression
input_db  = gamename & ".db" & compression
gdbm_db   = gamename
crash_db  = gamename & ".db.CRASH"
save_db   = gamename & ".db.old" & compression
backup_db = gamename & ".db.bk"
logname   = gamename & ".log"
savename  = gamename & ".tar.Z"
