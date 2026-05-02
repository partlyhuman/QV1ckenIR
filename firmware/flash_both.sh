#!/bin/zsh

PIO_ENV=esp32_s3_supermini
PARTFILE=wqvblink_partition_table.csv
APP0=wqv12/.pio/build/$PIO_ENV/firmware.bin
APP1=wqv310/.pio/build/$PIO_ENV/firmware.bin

APP0_OFFSET=$(awk -F, '$1=="app0" {print $4}' $PARTFILE)
APP1_OFFSET=$(awk -F, '$1=="app1" {print $4}' $PARTFILE)

if [[ ! -f $APP0 || ! -f $APP1 ]]; then
	echo "Couldn't find firmware bin $APP0 or $APP1"
	exit 1
fi

# DEBUG
# cat $PARTFILE
# echo
# echo "APP0 $APP0 -> offset $APP0_OFFSET"
# echo "APP1 $APP1 -> offset $APP1_OFFSET"

esptool --chip esp32s3 write-flash $APP0_OFFSET $APP0 $APP1_OFFSET $APP1

# or merge:
# esptool --chip esp32s3 merge-bin -o ota0ota1.bin $APP0_OFFSET $APP0 $APP1_OFFSET $APP1
