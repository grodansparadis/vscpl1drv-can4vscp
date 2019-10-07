# vscpl1drv-can4vscp - VSCP Level I CAN4VSCP Serial Driver

<img src="https://vscp.org/images/logo.png" width="100">

VSCP level I driver (CANAL driver) for hardware devices that export there inner functionality with the VSCP standard serial protocol (CAN4VSCP). A typical such device is the [Frankfurt RS-232 module](http://www.grodansparadis.com/frankfurt/rs232/frankfurt-rs232.html) from Grodans Paradis.

Several drivers can be loaded allowing simultaneous communication with several devices on different busses.

Filters/masks can be used to limit/select sub amount of events.

As the VSCP serial protocol is very generic this free serial protocol may also be the protocol to use for your own hardware which have some sort of serial port available.

**Available for:** Windows, Linux

**Driver for Linux**
vscpl1drv-can4vscp.so (vscpl1drv-can4vscp.lib)

**Driver for Windows** vscpl1drv-can4vscp.dll (vscpl1drv-can4vscp.lib)

## Configuration string

### Windows

> port[;nBaud]

#### port
The first parameter is the serial port to use (COM1, COM2 and so on). This parameter is mandatory.

#### nBaud
The second parameter is the serial baudrate and defaults to 5 which is the cod6e for 115200 Baud.

### Linux

> port[;nBaud]

#### port
The first parameter is the serial port to use (*/dev/ttyS0*, */dev/ttyS1*, */dev/ttyUSB0*, */dev/ttyUSB1* and so on). This parameter is mandatory.

#### nBaud
The second parameter is the serial baudrate and defaults to 5 which is the code for 115200 Baud.

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

Typical settings for VSCP daemon config

```xml
    <driver enable="true" >
        <name>can4vscp</name>
        <config>/dev/ttyUSB0</config>
        <path>/usr/local/lib/vscpl1_can4vscpdrv.so</path>
        <flags>0</flags>
        <guid>00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00</guid>
    </driver>
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
 | Bit 31 | Enable debug messages to LOG_DEBUG, syslog.  |

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

## Serial Protocol

You can find the description of the VSCP serial protocol in the [VSCP specification](http://www.vscp.org/docs/vscpspec/doku.php?id=physical_level_lower_level_protocols#vscp_over_a_serial_channel_rs-232).

## Install the driver on Linux
tbd

## Install the driver on Windows
tbd

## How to build the driver on Linux
To build this driver you have to first install the vscp common code. You do this with

```bash
git clone https://github.com/grodansparadis/vscp.git
```

In your .bashrc you then need to add the path to the location where the common code is located. Add

```bash
export VSCP_PATH=path-to-above-folder
```

Now clone the driver source

```bash
git clone https://github.com/grodansparadis/vscpl1drv-can4vscp.git
cd vscpl1drv-can4vscp
./configure
make
make install
```
Default install folder is **/usr/lib**

You need *build-essentials* and *git* installed on your system

```bash
sudo apt update && sudo apt -y upgrade
sudo apt install build-essential git
```


## How to build the driver on Windows
tbd

---

There are many Level I drivers (CANAL drivers) available in VSCP & Friends framework that can be used with both VSCP Works and the VSCP Daemon (vscpd) and other tools that interface the drivers using the CANAL standard interface. Added to that many Level II and Level III drivers are available that can be used with the VSCP Daemon.

Level I drivers is documented [here](https://grodansparadis.gitbooks.io/the-vscp-daemon/level_i_drivers.html).

Level II drivers is documented [here](https://grodansparadis.gitbooks.io/the-vscp-daemon/level_ii_drivers.html)

Level III drivers is documented [here](https://grodansparadis.gitbooks.io/the-vscp-daemon/level_iii_drivers.html)

# SEE ALSO

`vscpd` (8).
`uvscpd` (8).
`vscpworks` (1).
`vscpcmd` (1).
`vscp-makepassword` (1).
`vscphelperlib` (1).

The VSCP project homepage is here <https://www.vscp.org>.

The [manual](https://grodansparadis.gitbooks.io/the-vscp-daemon) for vscpd contains full documentation. Other documentation can be found here <https://grodansparadis.gitbooks.io>.

The vscpd source code may be downloaded from <https://github.com/grodansparadis/vscp>. Source code for other system components of VSCP & Friends are here <https://github.com/grodansparadis>

# COPYRIGHT
Copyright 2000-2019 Åke Hedman, Grodans Paradis AB - MIT license.