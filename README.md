# CCTVplexer

This is a simple CCTV multiplexer for the Raspberry Pi.

With this you can view multiple IP cameras at the same time and switch between different views using your TV remote control.

If any of your cameras are PTZs then you might also be able to control them using your TV remote control.

# Building

## Install dependancies
```
sudo apt install libcurl4-openssl-dev
sudo apt install libcec-dev
sudo apt install liblirc-dev
```
## Clone repository
```
git clone https://github.com/SynAckFin/CCTVplexer.git
```
## Build
```
cd CCTVplexer/src
make
```
# Configuring
## cctvplexer
The file ``config.cfg`` contains an example configuration for 4 cameras and 10 different views.
The file ``user-example.cfg`` should be renamed to user.cfg and the username and passwords for your cameras added.
The configuration file uses [libconfig](https://hyperrealm.github.io/libconfig/) for its syntax.
COnfiguring the plexer is a complex operation and for the moment I'll leave you to your own devices, but if enough people are interested in this I will create a wiki for it.
## lircd
``cctvplexer`` connects to the ``lircd`` socket and listens for remote control events so lircd needs to be running.
``cecremote`` simulates remote control events and injects them into ``lircd`` (which then passes them to
``cctvplexer``) so the ``allow-simulate`` option needs to be ``Yes``. The other option that needs to be set is ``release``
and this should also be set to ``Yes``.
Here is a sample configuration file ``/etc/lirc/lirc_options.conf``:
```
[lircd]
nodaemon        = False
driver          = file
device          = /dev/null
output          = /var/run/lirc/lircd
pidfile         = /var/run/lirc/lircd.pid
plugindir       = /usr/lib/arm-linux-gnueabihf/lirc/plugins
permission      = 666
allow-simulate  = Yes
repeat-max      = 600
#effective-user =
#listen         = [address:]port
#connect        = host[:port]
#loglevel       = 6
release         = true
release_suffix  = _EVUP
#logfile        = ...
#driver-options = ...

[lircmd]
uinput          = False
nodaemon        = False

```
# Running
There are 2 programs that need to run and they are:
### cecremote
This connects to the HDMI bus. By default it doesn't need any command line arguments but you can see the available options
by using ``--help``. You might need to change the TV channel for your TV to give it focus.
### cctvplexer
There are no command line arguments for this yet. It looks in the current working directory for its configuration file.

