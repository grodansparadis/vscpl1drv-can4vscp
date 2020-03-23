# Helpers to work with a can4vscp device

Some Python3 utils that help debugging a CAN4VSCP adapter.

## Preparations before use

Install libs. with

```bash
pip3 install pyserial
pip3 install crc8
```

now the tools can be used.

## commandClose.py

Close a CAN4VSCP channel.

## commandNoop.py

Send a NOOP command to the adapter

## commandSetVerboseMode.py

Make a CAN4VSCP device go from protocol mode to text on-line mode.

## receive.py

Receive frames from a can4vscp device.

### Using minicom

Install minicom with

```bash
sudo apt install minicom
```

You can use minicom to work with a CAN4VSCP adapter. Make sure it is set in text mode and start minicom with.

```bash
minicom -b115200 -D/dev/ttyUSB0 
```

Replace /dev/ttyUSB0 with the serial channel you  actually use.


