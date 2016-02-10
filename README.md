# tools
Some useful tools and config files.

# How to set up git autocompletion

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

# How to use watcher
watcher.py helps to sync your local dir (on MacOS)  to another remote dir. This is useful
when you want to develop on local laptop with nice IDE, and build/debug on
remote servers (Linux, etc).



```
First, install python, pip, and macfsevents

sudo easy_install pip
sudo pip install watchdog
sudo pip install macfsevents

"macfsevents" is optional. It's supposed to reduce cpu usage when watcher runs
in background.

If you get unknown errors when installing watch dog, do this:
sudo ARCHFLAGS=-Wno-error=unused-command-line-argument-hard-error-in-future pip install watchdog


Second, set up a background ssh connection to your server box. Run this once a 
day at beginning of day.

ssh -MNf <server>

Finally, run watcher

python ./watcher.py -s [local dir] [user]@[server]:[remote dir] --exclude-git --exclude="*.jar"



```

# How to use YouCompleteMe vim plugin on Ubuntu

1. Install vundle  (vim bundle), a vim plugin manager

git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim

2. config plugin:  copy .vimrc to your home dir

3. Install plugin YCM
```
sudo apt-get install build-essential cmake
sudo apt-get install python-dev

cd ~/.vim/bundle
git clone https://github.com/Valloric/YouCompleteMe.git
cd YouCompleteMe
git submodule update --init --recursive

./install.py --clang-completer
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
auto eth2
iface eth2 inet static
        address 192.168.0.152
        netmask 255.255.255.0
        broadcast 192.168.0.255
        dns-search hcdata.local
        post-up route del default dev eth2

  // static 1g, use it as default gw
auto eth0
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

