# vscpl1drv-can232
VSCP Level I driver for devices

<img src="https://vscp.org/images/logo.png" width="100">

vscpl1drv-can232 is a VSCP level I driver ([CANAL driver](https://docs.vscp.org/#canal)) for the **Lawicel CAN232** CAN adapter and other compatible **slcan** devices.

The linux port of this driver is created by (ice@geomi.org)

Info about the adapter is available at (https://www.can232.com)

This driver interface is for the can232 adapter from Lawicel (or other slcan drivers). This is a low cost CAN adapter that connects to one of the serial communication ports on a computer. The driver can handle the adapter in both polled and non polled mode, which handled transparently to the user. It is recommended however that the following settings are made before real life use.

* Set the baud rate for the device to 115200. You do this with the U1 command. This is the default baud rate used by this driver.
* Set auto poll mode by issuing the X1 command.
* Enable the time stamp by issuing the Z1 command.

## Platforms
  * Linux
  * Windows

## Driver for Windows

```bash
vscpl1drv_can232.dll
```

## Driver for Linux

```bash
vscpl1drv-can232.so
```
## Install location

### Linux

From version 14.0.0 the driver is installed in */var/lib/vscp/drivers/level1*

### Windows

From version 14.0.0 the driver is installed in */program files/vscpd/drivers/level1*

## Configuration string

All level I drivers are configured using a semicolon separated configuration string.

The driver string has the following format (note that all values can be entered in either decimal or hexadecimal form (for hex precede with 0x).

```bash
> comport;baudrate;mask;filter;bus-speed[;btr0;btr1]
```

###  comport
The serial communication port to use. 

For windows use *1,2,3...* 

For Linux use */dev/ttyS0, /dev/ttyUSB1* etc.

### baudrate
A valid baud rate for the **serial interface** ( for example. 9600 ).

### mask (optional)
The mask for the adapter. Read the Lawicel CAN232 manual on how to set this. It is not the same mask as for CANAL/VSCP.

#### filter (optional)
The filter for the adapter. Read the Lawicel CAN232 manual on how to set this. It is not the same as for CANAL/VSCP.

### bus-speed
is the speed or the **CAN interface**. Valid values are

| Setting | Bus-speed |
| :-----: | :---------: |
| 10 | 10Kbps |
| 20 | 20Kbps |
| 50 | 50Kbps |
| 100 |  100Kbps |
| 125 | 125Kbps |
| 250 | 250Kbps |
| 500 | 500Kbps |
| 800 | 800Kbps |
| 1000 | 1Mbps |

### btr0/btr1 (Optional.)
Instead of setting a bus-speed you can set the SJA1000 BTR0/BTR1 values directly. If both are set the bus_speed parameter is ignored.

### Defaults

If no device string is given COM1/ttyS0 will be used. Baud rate will be set to 115200 baud and the filter/mask to fully open. The CAN bit rate will be 500Kbps.

## Flags

 Not used. Set to zero.

## Status return

The CanalGetStatus call returns the status structure with the channel_status member having the following meaning:

 | Bit      | Description |
 | ---      | ----------- |
 | Bit 0-7  | TX Error Counter. |
 | Bit 8-15 | RX Error Counter. |
 | Bit 16   | Overflow. Cleard by status read. |
 | Bit 17   | RX Warning. |
 | Bit 18   | TX Warning. |
 | Bit 19   | TX bus passive. |
 | Bit 20   | RX bus passive. |
 | Bit 21   | Reserved. |
 | Bit 22   | Reserved. |
 | Bit 23   | Reserved. |
 | Bit 24   | Reserved. |
 | Bit 25   | Reserved. |
 | Bit 26   | Reserved. |
 | Bit 27   | Reserved. |
 | Bit 28   | Reserved. |
 | Bit 29   | Bus Passive. |
 | Bit 30   | Bus Warning status. |
 | Bit 31   | Bus off status |

## Example configurations

```bash
> 5;115200;0;0;1000
```

Uses COM5 at 115200 with filters/masks open to receive all messages and with 1Mbps CAN bit rate.

```bash
> /dev/ttyUSB1;57600;0;0;0;0x09;0x1C
```

On Linux uses serial USB adapter 1 at 57600 baud with filters/masks open to receive all messages and with a CAN bit-rate set to 50Kbps using btr0/btr1

## Typical settings for VSCP daemon config

### Linux

```xml
<!-- The can232 driver -->
<driver enable="false" 
        name="can232"
        config="/dev/ttyS0;19200;0;0;125"
        flags="0"
        translation="0x02"
        path="/var/lib/vscp/drivers/level1/vscpl1drv-can232.so"
        guid="FF:FF:FF:FF:FF:FF:FF:F5:01:00:00:00:00:00:00:04"
/>
```

### Windows

```xml
<!-- The can232 driver -->
<driver enable="false"
        name="can232"
        config="com1"
        flags="0"
        translation="0x02"
        path="/program files/vscpd/drivers/level1/vscpl1drv-can232.so"
        guid="FF:FF:FF:FF:FF:FF:FF:F5:01:00:00:00:00:00:00:04"
/>
```


## Install the driver on Linux

Install Debian package

```bash
> sudo apt install ./vscpl2drv-can232_1.1.0-1_amd64.deb
```

using the latest version from the repositories [release section](https://github.com/grodansparadis/vscpl1drv-can232/releases).

or

```
./configure 
./make
sudo make install
```

use the switch **--enable-debug** if you want a debug build.

## Install the driver on Linux using vscp private repository

**Warning !!!** *Currently this is very much experimental*

```bash
wget -O - http://apt.vscp.org/apt.vscp.org.gpg.key | sudo apt-key add -
```

then add

```bash
deb http://apt.vscp.org/debian buster main
deb http://apt.vscp.org/debian eoan main
```

to the file

```bash
/etc/apt/sources.list
```

replace **eoan** with the os-release you have installed and **debian** to *debian*, *ubuntu* or *raspian*

## Install the driver on Windows
Install using the binary install file in the release section.

## How to build the driver on Linux

```bash
git clone https://github.com/grodansparadis/vscpl1drv-can232.git
cd vscpl1drv-can232
git submodule update --init
./configure
make
make install
```

Default install folder is **/var/lib/vscp/drivers/level1**

You need *build-essentials* and *git* installed on your system.

```bash
sudo apt update && sudo apt -y upgrade
sudo apt install build-essential git
```


## How to build the driver on Windows
The source contains a Visual Studio project. Use this project to build the driver.

---

There are many Level I drivers (CANAL drivers) available in VSCP & Friends framework that can be used with both VSCP Works and the VSCP Daemon (vscpd) and other tools that interface the drivers using the CANAL standard interface. Added to that many Level II and Level III drivers are available that can be used with the VSCP Daemon.

Level I drivers is documented [here](https://docs.vscp.org/vscpd/latest/#/level_i_drivers).

Level II drivers is documented [here](https://docs.vscp.org/vscpd/latest/#/level_ii_drivers)


The VSCP project homepage is here <https://www.vscp.org>.

The [manual](https://docs.vscp.org/vscpd/latest) for vscpd contains full documentation. Other documentation can be found on the  [documentaion portal](https://docs.vscp.org).

The vscpd source code may be downloaded from <https://github.com/grodansparadis/vscp>. Source code for other system components of VSCP & Friends are here <https://github.com/grodansparadis>

## Trouble shooting

### Linux

#### Rights to use port

The user you run the VSCP daemon under (normally "vscp") must have rights to use the serial port. This is accomplished by adding the user to the "dialout" group. Like this

´´´bash
sudo adduser vscp dialout
´´´
Another method is to create udev rule that allow users access to the ports. Create a file _/etc/udev/rules.d/50-myusb.rules_ and add

```bash
KERNEL=="ttyUSB[0-9]*",MODE="0666"
KERNEL=="ttyACM[0-9]*",MODE="0666"
```

to it and restart or unplug/replug the device.  Beware that right 666 is as devilish as it looks and give all rights to everyone.

#### Port name/path change after configuration

As with all USB serial ports on Linux there is a chance that they change position in the USB enumeration tree. That is a port you known as /dev/ttyUSB0 becomes /dev/ttyUSB1 or something like that when you do a change in your configuration. 

The solution to this problem is to make a udev rule. Check [this writeup](https://askubuntu.com/questions/1021547/writing-udev-rule-for-usb-device) on how to do this. A typical example is from one of the test machines here with two USB serial ports. One for an electric meter and one for a CAN4VSCP serial device. It is setup like this

```bash
ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", KERNELS=="1-1.4", SYMLINK+="electric_meter"
ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", KERNELS=="1-1.5", SYMLINK+="vscp_serial0"
```

The serial adepter has the same vendor id and the same product id so we use the enumeration tree to distinguish between them.

## COPYRIGHT
Copyright © 2000-2021 Ake Hedman, Grodans Paradis AB - MIT license.
