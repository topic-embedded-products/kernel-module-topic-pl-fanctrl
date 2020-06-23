#!/bin/sh

case "$1" in
start)
	echo -n "Start FAN control"
	/usr/bin/pwm-fancontrol.sh -d -i /run/pwm-fancontrol.pid
	echo "."
	;;
stop)
	echo -n "Stop FAN control"
	kill `cat /run/pwm-fancontrol.pid`
	echo "."
	;;
restart)
	echo -n "Restart FAN control"
	kill `cat /run/pwm-fancontrol.pid` || true
	/usr/bin/pwm-fancontrol.sh -d -i /run/pwm-fancontrol.pid
	;;
*)
	echo "Usage: $0 {start|stop}"
	exit 1
	;;
esac
