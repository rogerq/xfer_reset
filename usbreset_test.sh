#!/bin/bash

# Start and stop the CAN interface and check if bringing up and turning off passed.

ITERS=10
# for (( i = 1 ; i <= $ITERS ; i++ ))
for (( i = 1 ; i > 0 ; i++ ))
do
	echo "=== test $i"
	sudo ./xfer_reset

	sleep 1
	DEV=$(lsusb -d 0525: | wc -l)
	if [ $DEV == '0' ] ; then
		echo "=== FAILED: test device not found"
		exit 1
	fi
done
echo "=== PASSED"
