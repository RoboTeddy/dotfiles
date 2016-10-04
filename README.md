# If you don't want to use this whole setup, and just want to limit your internet on a schedule...

(it'll be a bit messy, because this setup wasn't designed with anyone else in mind)

## Scripts to add to a directory in your system's `PATH`

### internet-off
(the path inside this file will need to be modified-- just follow the instructions in order and you'll get to it)

https://github.com/RoboTeddy/dotfiles/blob/master/bin/internet-off

### internet-on
https://github.com/RoboTeddy/dotfiles/blob/master/bin/internet-on

### on-system-login
This script is run when you log in; it calls internet-off

https://github.com/RoboTeddy/dotfiles/blob/master/bin/on-system-login


### internet-temporarily
Call this like `internet-temporarily 3` to get 3 minutes of internet. If you kill the script early with CTRL-C, the internet will turn back off.

https://github.com/RoboTeddy/dotfiles/blob/master/bin/internet-temporarily

## Firewall configuration scripts

Put thesetwo  files somewhere:

https://github.com/RoboTeddy/dotfiles/blob/master/pf/workmode.conf
https://github.com/RoboTeddy/dotfiles/blob/master/pf/workmode.pf.rules

And modify the path inside the first to point to the second. Also update the path inside `internet-off`.

## Launchd files to run the above scripts on a schedule
Edit these to control the time that the internet turns on or off. For documentation on the syntax, see http://launchd.info/

Copy these files:

https://github.com/RoboTeddy/dotfiles/blob/master/cron/local.internet-off.plist
https://github.com/RoboTeddy/dotfiles/blob/master/cron/local.internet-on.plist
https://github.com/RoboTeddy/dotfiles/blob/master/cron/local.on-system-login.plist

into the directory `~/Library/LaunchAgents/`

## Configure system permissions

Because firewall changes will need to happen in the background, it won't work if you need to be prompted for your password. You can give admin users on your system permission to modify the firewall without being prompted by running `sudo visudo` and adding this line to the end of the file:

`%admin ALL = NOPASSWD: /sbin/pfctl`

(note: if your user isn't an admin on the system, the line will need to be different)
