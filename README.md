# vscpl1drv-can4vscp - VSCP Level I CAN4VSCP Serial Driver

<img src="https://vscp.org/images/logo.png" width="100">

vscpl1drv-can4vscp is a VSCP level I driver ([CANAL driver](https://docs.vscp.org/#canal)) for hardware devices that export there inner functionality with the [VSCP standard serial protocol (CAN4VSCP)](https://docs.vscp.org/spec/latest/#/./vscp_over_a_serial_channel_rs-232?id=general-frame-format). A typical such device is the [Frankfurt RS-232 module](http://www.grodansparadis.com/frankfurt/rs232/frankfurt-rs232.html) from [Grodans Paradis AB](https://www.grodansparadis.com).

Several drivers can be loaded allowing simultaneous communication with several devices on different busses.

Filters/masks can be set to limit/select sub amount of events.

As the VSCP serial protocol is very generic this free serial protocol may also be the protocol to use for your own hardware which have some sort of serial port available.

## Platforms

* Linux
* Windows

## Driver for Linux

```bash
vscpl1drv-can4vscp.so
```

## Driver for Windows

vscpl1drv-can4vscp.dll 

## Install location

### Linux

From version 14.0.0 the driver is installed in */var/lib/vscp/drivers/level1*

### Windows

From version 14.0.0 the driver is installed in */program files/vscpd/drivers/level1*

## Configuration string - Windows

All level I drivers are configured using a semicolon separated configuration string.

```bash
port[;nBaud]
```

### port

The first parameter is the serial port to use (**COM1**, **COM2** and so on). 

This parameter is mandatory.

### nBaud

The second parameter is the serial baudrate and defaults to **5** which is the code for 115200 Baud.

## Configuration string -  Linux

All level I drivers are configured using a semicolon separated configuration string.

```bash
port[;nBaud]
```

### port

The first parameter is the serial port to use (*/dev/ttyS0*, */dev/ttyS1*, */dev/ttyUSB0*, */dev/ttyUSB1* and so on). 

This parameter is mandatory.

### nBaud

The second parameter is the serial baud rate and defaults to **5** which is the code for 115200 Baud.

| Baudrate | Code | Error  | Windows | Linux |
 | :--------: | :----: | :-----:  | :-------: | :-----: |
 | 115200   | 0    | -1.36% | yes     | yes   |
 | 128000   | 1    | -2.34% | yes     | no    |
 | 230400   | 2    | -1.36% | no      | yes   |
 | 256000   | 3    | -2.34% | yes     | no    |
 | 460800   | 4    | 8.51%  | no      | no    |
 | 500000   | 5    | 0%     | yes     | yes   |
 | 625000   | 6    | 0%     | bad     | no    |
 | 921600   | 7    | -9.58% | no      | bad   |
 | 1000000  | 8    | 16.67% | no      | bad   |
 | 9600     | 9    | 0.16%  | yes     | yes   |
 | 19200    | 10   | 0,16%  | yes     | yes   |
 | 38400    | 11   | 0,16%  | yes     | yes   |
 | 57600    | 12   | 0.94%  | yes     | yes   |

Tests on Windows and Linux has been done on a Windows 10 machine and on a Ubuntu machine with the USB serial adapter that ship with [Frankfurt RS-232](http://www.grodansparadis.com/frankfurt/rs232/frankfurt-rs232.html).

### Typical settings for VSCP daemon config for Linux

```xml
<!-- The can4vscp driver -->
<driver enable="false"
        name="can4vscp"
        config="/dev/ttyUSB0"
        flags="0"
        translation="0x02"
        path="/var/lib/vscp/drivers/level1/vscpl1drv-can4vscp.so"
        guid="FF:FF:FF:FF:FF:FF:FF:F5:01:00:00:00:00:00:00:02"
/>
```

### Typical settings for VSCP daemon config for Windows

```xml
<!-- The can4vscp driver -->
<driver enable="false"
        name="can4vscp"
        config="com1"
        flags="0"
        translation="0x02"
        path="/program files/vscpd/drivers/level1/vscpl1drv-can4vscp.so"
        guid="FF:FF:FF:FF:FF:FF:FF:F5:01:00:00:00:00:00:00:02"
/>
```

## Flags

 | Bits | Usage |
 | ----  | ----- |
 | Bit 0,1  | **Open Mode** <br> **0** - normal <br> **1** - Listen mode <br> **2** - Loopback mode <br> **3** - Reserved |
 | Bit 2    | If set the driver will not switch to VSCP mode. That is it must be in VSCP mode. Open will be faster. |
 | Bit 3    | If set the driver will wait for an ACK from the physical device for every sent frame. This will slow down sending but make transmission it very secure. |
 | Bit 4    | Enable timestamp. The timestamp will be written by the hardware instead of the driver. |
 | Bit 5    | Enable hardware handshake.  |
 | Bit 6 | Enable strict mode. Driver will terminate on all errors.  |
 | Bit 7-30 | Reserved.  |
 | Bit 31 | Enable debug messages to LOG_DEBUG, syslog (0x80000000).  |

## Status return

The **CanalGetStatus** call returns the status structure with the channel_status member having the following meaning:

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

## Serial Protocol

You can find the description of the VSCP serial protocol in the [VSCP specification](https://docs.vscp.org/spec/latest/#/./vscp_over_a_serial_channel_rs-232?id=general-frame-format).

## Install the driver on Linux

Install Debian package

```bash
> sudo apt install ./vscpl2drv-can4vscp_1.1.0-1_amd64.deb
```

using the latest version from the repositories [release section](https://github.com/grodansparadis/vscpl1drv-can4vscp/releases).

or

```
./configure 
./make
sudo make install
```

use the switch **--enable-debug** if you want a debug build.

### Privileges on serial port

Depending on which user you run the VSCP daemon as you may have to give the vscp daemon read/write access to the serial port where the CAN4VSCP adapter is connected to (ttyS0, ttyS1, ttyUSB0, ttyUSB1....). 

The VSCP daemon is by default running as the user _vscp_ and a serial adapter usually have read/write access to a group called _dialout_ so we need to add the _vscp_ user to this group. This we can do with

```bash
sudo usermod -a -G dialout vscp
```
now restart the VSCP daemon with

```bash
sudo systemctl restart vscpd
```

Another method is to define a rule for USB serial adapters. You do this by adding a rule file to _/etc/dev/rules.d_

```bash
cd /etc/uidev/rules.d
sudo touch  50-myusb.rules
sudo vim 50-myusb.rules 
```

add 

```
KERNEL=="ttyUSB[0-9]*",MODE="0666"
```

to the file and reboot the machine. Note that this gives read/write permissions for all serial USB adapters 0-9.

Even another solution is to run the VSCP daemon as root user but this is not recommended for security reasons.

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
git clone https://github.com/grodansparadis/vscpl1drv-can4vscp.git
cd vscpl1drv-can4vscp
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

## Troubleshooting

### Enable debug output

Set bit 32 of flags (0x8000000) to enable debug output to syslog. This will give diagnostic info that will help to solve many problems. Check the log with

```bash
cat /var/log/syslog | grep vscpd
```

### Access rights

The VSCP daemon, VSCP Works or other software that use this driver need to have read/write privileges to any serial ports or similar. For a serial port the easiest way is to issue

```
chmod a+rw /dev/ttyUSB0
```

replacing the port with the port you want to use. If this solve the problem you know it is an access rights problem and you can configure correct access rights. In most cases it may not be a good choice to give read/write access to everyone as we do here.

---

There are many Level I drivers (CANAL drivers) available in VSCP & Friends framework that can be used with both VSCP Works and the VSCP Daemon (vscpd) and other tools that interface the drivers using the CANAL standard interface. Added to that many Level II and Level III drivers are available that can be used with the VSCP Daemon.

Level I drivers is documented [here](https://docs.vscp.org/vscpd/latest/#/level_i_drivers).

Level II drivers is documented [here](https://docs.vscp.org/vscpd/latest/#/level_ii_drivers)


The VSCP project homepage is here <https://www.vscp.org>.

The [manual](https://docs.vscp.org/vscpd/latest) for vscpd contains full documentation. Other documentation can be found on the  [documentaion portal](https://docs.vscp.org).

The vscpd source code may be downloaded from <https://github.com/grodansparadis/vscp>. Source code for other system components of VSCP & Friends are here <https://github.com/grodansparadis>

# COPYRIGHT

Copyright © 2000-2020 Ake Hedman, Grodans Paradis AB - MIT license.
