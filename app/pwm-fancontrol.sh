#!/bin/sh
for d in /sys/class/hwmon/hwmon*
do
  N=`cat $d/name`
  if [ "$N" = "iio_hwmon_ams_temp" ]
  then
    PWMFANARGS="${PWMFANARGS} -t $d/temp1_input"
  fi
  if [ "$N" = "topicfan" ]
  then
    PWMFANARGS="${PWMFANARGS} -p $d/pwm1"
  fi
done
exec pwm-fancontrol ${PWMFANARGS} $*
