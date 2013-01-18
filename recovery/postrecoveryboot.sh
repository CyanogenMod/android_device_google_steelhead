#!/sbin/sh

sleep 3

# Use first supported res
RES=$(cat /sys/devices/omapdss/display0/timings | cut -d, -f2,3 | cut -d / -f1,4 | sed -e 's/\/.*,/,/g' | head -n1)

if [ ! -z $RES ]; then
   echo $RES > /sys/devices/platform/omapdss/overlay1/output_size
else
   ## Try one more time, some TVs take time to sync
   sleep 3
   RES=$(cat /sys/devices/omapdss/display0/timings | cut -d, -f2,3 | cut -d / -f1,4 | sed -e 's/\/.*,/,/g' | head -n1)
   if [ ! -z $RES ]; then
      echo $RES > /sys/devices/platform/omapdss/overlay1/output_size
   fi
fi
