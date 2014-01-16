The MODBUS filesystem
=====================

Modbusfs allow users to see all MODBUS clients connected with a MODBUS
master as a filesystem tree where directories are named as the
client's MODBUS address and files are the clients' registers.

With modbusfs users can use their preferred programming languages even
if they do not have any dedicated MODBUS support! In fact users simply
use file related system calls (i.e. open(), close(), read(), write(),
etc.) to get access to the MODBUS clients' register.


Note: ths projects is still in beta version so refer to the "known
      bugs" section for further info! DO NOT USE in production
      environment!!!


Usage
-----

Usage is quite simple, just use command:

    $ ./modbusfs rtu:/dev/ttyUSB0,115200,8E1 serial_0/
    $ ls serial_0/
    exports

The connection type argument is self explanatory. It asks for a RTU
MODBUS connection throught device /dev/ttyUSB0 at 115200 baud transfer
rate with 8 bits, even parity and 1 stop bit.

After the mount you have only the "exports" file which can be used to add
new "clients" to the filesystem (that is new directories). So, for instance,
if you have a client at address 10 you can add it by using:

    $ echo 10 0755 > serial_0/exports 

where "10" is the slave's address and "0755" is the file access
permissions. Here the result:

    $ ls -l serial_0   
    total 0
    drwxr-xr-x 2 giometti staff 0 Jan  1  1970 10/
    --w------- 1 giometti staff 0 Jan  1  1970 exports

Now we have a new directory referring to the MODBUS slave at address
10! Let's that a look at it:

    $ ls -l serial_0/10
    total 0
    --w------- 1 giometti staff 0 Jan  1  1970 exports

This time the "exports" file can be used to add new files referring to
the client's register. For instance I can add a read-only register at
address 8 by using:

    $ echo 8 0444 > serial_0/10/exports
    $ ls -l serial_0/10/
    total 0
    -r--r--r-- 1 giometti staff 4 Jan  1  1970 8
    --w------- 1 giometti staff 0 Jan  1  1970 exports

Now you can easily read the client's register by using standard file
related system calls! For instance, using bash you can do:

    $ cat serial_0/10/8 ; echo -e
    0

[The echo command is just to add a "newline" at the end of cat's output
for better readability]

On the other way if you need writing on read-write register 13 you can
do:

    $ echo 13 0666 > serial_0/10/exports
    $ cat serial_0/10/13 ; echo -e
    afc8
    $ echo afc9 > serial_0/10/13
    $ cat serial_0/10/13 ; echo -e
    afc9

Debugging
---------

You may enable specific modbusfs debugging message by adding "-d -f"
options to the command line:

    $ ./modbusfs -d -f rtu:/dev/ttyUSB0,115200,8E1 serial_0/
    *** methods.c[ 177]: client_connect: RTU on /dev/ttyUSB0 at 115200 8E1
    *** methods.c[ 290]: modbusfs_getattr: path=/.Trash
    *** methods.c[ 290]: modbusfs_getattr: path=/.xdg-volume-info
    *** methods.c[ 290]: modbusfs_getattr: path=/autorun.inf
    *** methods.c[ 221]: modbusfs_readdir: path=/
    *** methods.c[ 290]: modbusfs_getattr: path=/.Trash-415
    *** methods.c[ 290]: modbusfs_getattr: path=/exports
    *** methods.c[ 221]: modbusfs_readdir: path=/
    ...

If you need fuse debugging messages too use double "-d" option into
the command line:

    $ ./mousfs -d -d rtu:/dev/ttyUSB0,115200,8E1 serial_0/
    *** methods.c[ 177]: client_connect: RTU on /dev/ttyUSB0 at 115200 8E1
    FUSE library version: 2.9.0
    nullpath_ok: 0
    nopath: 0
    utime_omit_ok: 0
    unique: 1, opcode: INIT (26), nodeid: 0, insize: 56, pid: 0
    INIT: 7.20
    flags=0x000017fb
    max_readahead=0x00020000
       INIT: 7.18
       flags=0x00000011
       max_readahead=0x00020000
       max_write=0x00020000
       max_background=0
    ,,,

Known bugs
----------

* Only RTU connection is supported.
* Only read/write of holding registers are supported
* Broadcast messages are not supported
* File access permissions should be better managed (only user permissions
  are currently managed)

TODO
----

* Add access/modify time for all files
* Better implementation of command line parsing
* Add symbolic links for clients' names for better usability
