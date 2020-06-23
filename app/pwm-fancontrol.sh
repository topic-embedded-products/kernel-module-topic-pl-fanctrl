#!/bin/sh

# Expect config file to set PWMFANARGS
. /etc/defaults/pwm-fancontrol

case "$1" in
start)
	echo -n "Start FAN control"
	pwm-fancontrol -d -i /run/pwm-fancontrol.pid $PWMFANARGS
	echo "."
	;;
stop)
	echo -n "Stop FAN control"
	kill `cat /run/pwm-fancontrol.pid`
	echo "."
	;;
*)
	echo "Usage: $0 {start|stop}"
	exit 1
	;;
esac
