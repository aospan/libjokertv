# libjokertv project

User-level driver for Joker TV card (https://tv.jokersys.com) using libusb.
Supported platforms: Linux, Mac OSx (more platforms coming soon ... )

'linux' folder contains stripped Linux kernel header and Linux media subsystem
drivers (cxd2841er, helene, etc).

(c) Abylay Ospan <aospan@jokersys.com>, 2017, https://jokersys.com

LICENSE: GPLv2

# Compilation
```
mkdir build
cd build
cmake ../
make
```

# Run

example tune to DVB-C (J.83A) on 150MHz with bandwidth 8MHz
```
./joker-tv -d 1 -f 150000000 -b 8000000
```

example tune to US cable (J.83B, DVB-C annex B) on 150MHz with bandwidth 8MHz
```
./joker-tv -d 2 -f 150000000 -b 8000000 -m 3
```

example tune to ATSC on 575MHz with bandwidth 6MHz and modulation 8VSB
```
./joker-tv -d 11 -f 575000000 -b 6000000 -m 7
```

example tune to DVB-T on 650MHz with bandwidth 8MHz
```
./joker-tv -d 3 -f 650000000 -b 8000000
```

example tune to ISDB-T on 473MHz with bandwidth 6MHz
```
./joker-tv -d 8 -f 473000000 -b 6000000
```

example tune to DTMB on 650MHz
```
./joker-tv -d 13 -f 650000000
```

example tune to DVB-S on Galaxy 19 satellite (97W). Frequency 12152 MHz, Horizontal
polarization (18V), symbol rate 20000, LNB settings 10750,10750,11600
```
./joker-tv -d 5 -f 12152000000 -s 20000000 -y 18 -p -z 10750,10750,11600
```

example tune to DVB-S on Galaxy 18 satellite (123W). Frequency 12033 MHz, Horizontal
polarization (18V), symbol rate 20000, LNB settings 10750,10750,11600
```
./joker-tv -d 5 -f 12033000000 -s 8200000 -y 18 -p -z 10750,10750,11600
```

if everything is fine then you can see progress. Something like this:
```
usb device found
Sony HELENE Ter attached on addr=61 at I2C adapter 0x7fff5f915d78
TUNE done
WAITING LOCK. status=0 error=Undefined error: 0 
USB ISOC: all/complete=7999.868002/2381.460706 transfer/sec 18.586992 mbits/sec 
USB ISOC: all/complete=8000.020000/2369.505924 transfer/sec 18.493687 mbits/sec
...
```

resulting TS stream should apear in ./out.ts file.

## TS stream generator
choose TS stream generated inside FPGA:
```
./build/joker-tv -t
```

generated TS will be saved into 'out.ts' file. Check TS stream correctness with
following command:
```
./build/tscheck -f out.ts -p
```

## I2C bus scan
```
./i2c-scan
```
