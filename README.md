# tools
Some useful tools and config files.


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

