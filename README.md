# ircprobe

Dirty utility for testing IRCd

**WORK IN PROGRESS**

modprobe test an IRC daemon creating thousands of connections tah wiil send hundred of messages, measuring lag between receptions.

Lag is write in a data file. Format is:

``
<seconds from start> <lag in miliseconds>
``

## Options

``
usage: ircprobe [-bd] [-h host/ip] [-n connections] [-p port] [-c channel]
                [--min=minimum] [--max=maximum] [--connin=interval]
                [--chnum=channels] [--cprefix=prefix]
                [--nprefix=prefix] [--help]
        Command Summary:
        -b                 Disable tests.
        -d                 Enable debug. Writes all irc server ouput in a file
                           with name <host>.log.
        -h host            mandatory option. Set irc host to connect.
        -n length          mandatory option. Set how many connections.
        -p port            IRC port.
        -c channel         Connect all sockets only to this channel.
        --chnum=channels   Number of channels to distribute connections.
        --connin=interval  Interval between sequential connections
        --min=milis        Minimal range of time in which each connection will
                           send a message
        --max=milis        Maximum range of time in which each connection will
                           send a message
        --cprefix=prefix   Nick prefix for connections
        --nprefix=prefix   Channel prefix for connections
        --help             This message
        
``

## Build

Linux only.

``
g++ ircprobe.cpp -o ircprobe
`` 

## Examples

- Connect 1000 sockets each 100 miliseconds and sending messages between 1 and 10 seconds.

``
$ ./modprobe -h irc.net -n 1000 --connin=100 --min=1000 --max=10000
``

- Connect only one socket and test lag.

``
$ ./modprobe -h irc.net -n 1
``

