#!/bin/zsh

# Reuses Platform.io build products, still build with PIO, but our custom boot setup is probably not supported
PARTFILE=../wqvblink_partition_table.csv

PIO_ENV=esp32_s3_supermini
APP0_BIN=../wqv12/.pio/build/$PIO_ENV/firmware.bin
APP1_BIN=../wqv310/.pio/build/$PIO_ENV/firmware.bin
COMMON=../wqv310/.pio/build/$PIO_ENV
BOOTLOADER_BIN=$COMMON/bootloader.bin
PARTITIONS_BIN=$COMMON/partitions.bin

APP0_OFFSET=$(awk -F, '$1=="app0" {print $4}' $PARTFILE)
APP1_OFFSET=$(awk -F, '$1=="app1" {print $4}' $PARTFILE)

esptool --chip esp32s3 merge-bin -o release.bin \
  0x0 $BOOTLOADER_BIN \
  0x8000 $PARTITIONS_BIN \
  $APP0_OFFSET $APP0_BIN \
  $APP1_OFFSET $APP1_BIN
