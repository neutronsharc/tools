# ASAN debug
## generate a coredump when asan trips.
- export ASAN_OPTIONS=abort_on_error=1
- ulimit -c unlimited
-  run your binary

# Perf a process
- git clone https://github.com/brendangregg/FlameGraph
- edit the "flamechart.sh" to point to the tool dir.
- run "./flamechart.sh  thread-id  <output filename>"
- Use firefox to view the generated *.svg file.


# Ubuntu laptop:  change the behaviors when lid is closed
## config systemd-logind
Edit "/etc/systemd/logind.conf",  change "HandleLidSwitch".
- HandleLidSwitch=poweroff: shutdown / power off when lid is closed.
- HandleLidSwitch=hibernate: hibernate when lid is closed (need to test if hibernate works).
- HandleLidSwitch=ignore: do nothing.
- HandleLidSwitch=suspend: suspend laptop when lid is closed.

## install tweaks
- sudo apt-get install gnome-tweak-tool,  then run "tweaks"  => power => change the value at "suspend when laptop lid is closed"

# Java config at macos

## JAVA_HOME
At ~/.bash_profile export java_home: `export JAVA_HOME="$(/usr/libexec/java_home)"`

## build a module at maven
To build only one module with all its dependencies:  `mvn package -pl <mod name> -am`

# tools
Some useful tools and config files.

# How to set up git autocompletion
## On Windows
- (1) install clink (download exe from here: https://chrisant996.github.io/clink/)
- (2) Copy git-autocomplete.lua into C:\Users\<username>\AppData\local\clink\
(from here: https://github.com/ztomm/git-autocomplete-for-windows/blob/main/git-autocomplete.lua)
- (3) open clink app on Windows (like a replacement for powershell)

## On Macos
Install Git and bash-completion: 
```
brew install git bash-completion
```

Add bash-completion to your .bash_profile:
```
if [ -f `brew --prefix`/etc/bash_completion ]; then
    . `brew --prefix`/etc/bash_completion
fi
```

# How to setup passwordless ssh
(1) generate pub key at your local home .ssh
```
cd ~/.ssh
ssh-keygen -t rsa
```
(2) copy the pub key to remote server with the username you want.
```
cat ./id_rsa.pub | ssh <username>@<server> 'cat >> .ssh/authorized_keys'
```
or,
```
ssh-copy-id <user>@<hostname>
```


# How to use watcher
watcher.py helps to sync your local dir (on MacOS or Linux)  to a remote dir. This is useful
when you want to develop on local laptop with IDE, and build/debug on remote servers without GUI (Linux, etc).

First, set up passwordless ssh (as directed in previous section).

Second, install python, pip
```
sudo easy_install pip
sudo pip install watchdog
```
For MacOS add one more package:
```
sudo pip install macfsevents
```
(*For MacOS only*): "macfsevents" is optional. It's supposed to reduce cpu usage when watcher runs
in background.

If you get unknown errors when installing watch dog, do this:
sudo ARCHFLAGS=-Wno-error=unused-command-line-argument-hard-error-in-future pip install watchdog

(*Optional*) Third, set up a background ssh connection to your server box. Run this once a 
day at beginning of day.
`ssh -MNf <server>`


Finally, run watcher
```
python ./watcher.py -s [local dir] [user]@[server]:[remote dir] --exclude-git --exclude="*.jar"
```

# How to use YouCompleteMe vim plugin on Ubuntu

- Install vundle  (vim bundle), a vim plugin manager
```
git clone https://github.com/VundleVim/Vundle.vim.git  ~/.vim/bundle/Vundle.vim
```

- Install plugin YCM
```
sudo apt-get install build-essential cmake
sudo apt-get install python-dev

cd ~/.vim/bundle
git clone https://github.com/Valloric/YouCompleteMe.git
cd YouCompleteMe
git submodule update --init --recursive
./install.py --clang-completer

cp third_party/ycmd/cpp/ycm/.ycm_extra_conf.py ~/.vim/

```

- config plugin:  copy .vimrc at this repo to your home dir.
- copy cscope_maps.vim to ~/.vim/plugin/

# Cscope shortcuts in Vim
Cscope is a very powerful interface allowing you to easily navigate C-like code files. Vim provides the capability to navigate code without ever leaving the editor.
```
:cs help  //  cscope helper

// Inside vim, move your cursor to a symbol and type ctrl + \, then:

c: Find functions calling this function
d: Find functions called by this function
e: Find this egrep pattern
f: Find this file
g: Find this definition
i: Find files #including this file
s: Find this C symbol
t: Find this text string
```

# How to config network (static, dynamic IP, nameservers, default gw)

## Ubuntu
Edit /etc/network/interfaces.

```
# interfaces(5) file used by ifup(8) and ifdown(8)
auto lo
iface lo inet loopback

 // DHCP
auto eth1
iface eth1 inet dhcp

 // static IP for 10g, rm default gw
allow-hotplug eth2
iface eth2 inet static
        address 192.168.0.152
        netmask 255.255.255.0
        broadcast 192.168.0.255
        dns-search hcdata.local
        post-up route del default dev eth2

  // static 1g, use it as default gw
allow-hotplug eth0
iface eth0 inet static
        address 172.16.1.152
        netmask 255.255.255.0
        broadcast 172.16.1.255
        network 172.16.1.0
        gateway 172.16.1.1
        dns-search hcdata.local
        dns-nameservers 172.16.1.3 172.16.1.3
        post-up route add default via 172.16.1.1 dev eth0
```

Then `ifconfig eth0 down/up ` to apply the changes. 

## Ubuntu: add name servers and domain name search
"/etc/resolv.conf" is a link to "/run/resolvconf/resolv.conf".  Remove this link, and create a real file "/etc/resolv.conf" with following content:
```
nameserver <ns ip>
nameserver <ns ip>
search local
search <domain2>
search <domain 3>
```

## CentOS: config static IP, add name servers

Config IP at:  ` /etc/sysconfig/network-scripts/ifcfg-<iface name>`.

Then, add dns servers to `/etc/resolv.conf`:
```
search <domain1>
search local
nameserver 172.16.1.3
nameserver 172.16.1.4
```

Then, restart network server:
`systemctl restart network.service`

