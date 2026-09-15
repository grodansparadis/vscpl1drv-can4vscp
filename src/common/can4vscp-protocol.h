// can4vscp-protocol.h:
//
// CAN4VSCP serial protocol: constants, byte stuffing, frame
// construction and CRC checking. Pure protocol layer with no
// driver state.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version
// 2 of the License, or (at your option) any later version.
//
// This file is part of the VSCP (http://www.vscp.org)
//
// Copyright (C) 2000-2026 Ake Hedman and contributors, [the VSCP project](https://www.vscp.org)
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this file see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.
//

#if !defined(CAN4VSCP_PROTOCOL_H__INCLUDED_)
#define CAN4VSCP_PROTOCOL_H__INCLUDED_

#include <stdint.h>

// Byte stuffing start and end characters
#define DLE 0x10
#define STX 0x02
#define ETX 0x03

// RX State machine
#define INCOMING_STATE_NONE 0     // Waiting for <STX>
#define INCOMING_STATE_STX 1      // Reading data
#define INCOMING_STATE_ETX 2      // <ETX> has been received
#define INCOMING_STATE_COMPLETE 3 // Frame received

#define INCOMING_SUBSTATE_NONE 0 // Idle
#define INCOMING_SUBSTATE_DLE 1  // <DLE> received

// CAN4VSCP Commands
#define RESET_NOOP 0x00     // No Operation
#define GET_TX_ERR_CNT 0x02 // Get TX error count
#define GET_RX_ERR_CNT 0x03 // Get RX error count
#define GET_CANSTAT 0x04    // Get CAN statistics
#define GET_COMSTAT 0x05
#define GET_MSGFILTER1 0x06 // Get message filter 1
#define GET_MSGFILTER2 0x07 // Get message filter 2
#define SET_MSGFILTER1 0x08 // Set message filter 1
#define SET_MSGFILTER2 0x09 // Set message filter 2

// Emergency flags
#define EMERGENCY_OVERFLOW 0x01
#define EMERGENCY_RCV_WARNING 0x02
#define EMERGENCY_TX_WARNING 0x04
#define EMERGENCY_TXBUS_PASSIVE 0x08
#define EMERGENCY_RXBUS_PASSIVE 0x10
#define EMERGENCY_BUS_OFF 0x20

// VSCP Driver positions in frame
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_TYPE 0
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_CHANNEL 1
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_SEQUENCY 2
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_SIZE_PAYLOAD_MSB 3
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_SIZE_PAYLOAD_LSB 4
#define VSCP_CAN4VSCP_DRIVER_POS_FRAME_PAYLOAD 5

// VSCP driver commands
#define VSCP_CAN4VSCP_DRIVER_COMMAND_NOOP 0
#define VSCP_CAN4VSCP_DRIVER_COMMAND_OPEN 1
#define VSCP_CAN4VSCP_DRIVER_COMMAND_LISTEN 2
#define VSCP_CAN4VSCP_DRIVER_COMMAND_LOOPBACK 3
#define VSCP_CAN4VSCP_DRIVER_COMMAND_CLOSE 4
#define VSCP_CAN4VSCP_DRIVER_COMMAND_SET_FILTER 5
#define VSCP_CAN4VSCP_DRIVER_COMMAND_SET_MASK 6

// VSCP driver configuration
#define VSCP_DRIVER_CONFIG_NOOP 0
#define VSCP_DRIVER_CONFIG_MODE 1
#define VSCP_DRIVER_CONFIG_TIMESTAMP 2
#define VSCP_DRIVER_CONFIG_BAUDRATE 3

// Baudrate codes
#define SET_BAUDRATE_115200 0
#define SET_BAUDRATE_128000 1
#define SET_BAUDRATE_230400 2
#define SET_BAUDRATE_256000 3
#define SET_BAUDRATE_460800 4
#define SET_BAUDRATE_500000 5
#define SET_BAUDRATE_625000 6
#define SET_BAUDRATE_921600 7
#define SET_BAUDRATE_1000000 8
#define SET_BAUDRATE_9600 9
#define SET_BAUDRATE_19200 10
#define SET_BAUDRATE_38400 11
#define SET_BAUDRATE_57600 12

#define SET_BAUDRATE_MAX 13

#ifdef __cplusplus
extern "C"
{
#endif

    /*!
        Add a byte to a frame buffer with DLE byte stuffing.

        @param p Pointer to position in frame buffer to write at.
        @param c Byte to add.
        @param pcrc Running CRC that is updated with c. Can be NULL.
        @return Number of bytes written (1, or 2 if c was escaped).
    */
    uint8_t can4vscp_addWithEscape(uint8_t *p, uint8_t c, uint8_t *pcrc);

    /*!
        Build a complete byte stuffed CAN4VSCP serial frame
        (DLE STX | type | channel | seq | payload size | payload | crc | DLE ETX).

        @param pbuf Buffer to write the frame to. Must be large enough to hold
            the worst case stuffed frame (2 * (7 + lenPayload) bytes).
        @param frameType Frame type (VSCP_SERIAL_DRIVER_FRAME_TYPE_...).
        @param channel Channel.
        @param seq Frame sequency number.
        @param declaredSizePayload Payload size written to the frame header.
        @param pPayload Payload bytes. Can be NULL if lenPayload is zero.
        @param lenPayload Number of payload bytes to add.
        @return Length of the built frame in bytes.
    */
    uint16_t can4vscp_buildFrame(uint8_t *pbuf,
                                 uint8_t frameType,
                                 uint8_t channel,
                                 uint8_t seq,
                                 uint16_t declaredSizePayload,
                                 const uint8_t *pPayload,
                                 uint16_t lenPayload);

    /*!
        Check the CRC of a destuffed frame (last byte is the CRC).

        @param pbuf Frame content.
        @param len Frame length including the CRC byte.
        @return Non zero if the CRC is valid.
    */
    int can4vscp_checkCRC(const uint8_t *pbuf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // CAN4VSCP_PROTOCOL_H__INCLUDED_
