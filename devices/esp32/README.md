# ESP32 Sinter example

Runs SVML programs, that's about it.

## Building

First, export the ESP-IDF envs:

```
. path/to/esp-idf/export.sh
```

Then build:

```
rm -r build
./build.sh
```

## Flashing

```
idf.py flash
```

## Usage

```
./run.sh /dev/ttyUSBx ../../test_programs/return_1.svm
```
