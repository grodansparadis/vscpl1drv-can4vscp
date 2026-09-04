// can4vscp-protocol.cpp:
//
// CAN4VSCP serial protocol framing implementation.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version
// 2 of the License, or (at your option) any later version.
//
// This file is part of the VSCP (http://www.vscp.org)
//
// Copyright (C) 2000-2026 Ake Hedman,
// Ake Hedman, Grodans Paradis AB, <akhe@grodansparadis.com>
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

#include "can4vscp-protocol.h"

#include <crc8.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// can4vscp_addWithEscape
//

uint8_t can4vscp_addWithEscape(uint8_t *p, uint8_t c, uint8_t *pcrc) {
  if (DLE == c) {
    *p = DLE;
    if (NULL != pcrc)
      crc8(pcrc, DLE);
    *(p + 1) = DLE;
    // !!! CRC only calculated over one DLE !!!
    return 2;
  } else {
    *p = c;
    if (NULL != pcrc)
      crc8(pcrc, c);
    return 1;
  }
}

///////////////////////////////////////////////////////////////////////////////
// can4vscp_buildFrame
//

uint16_t can4vscp_buildFrame(uint8_t *pbuf,
                             uint8_t frameType,
                             uint8_t channel,
                             uint8_t seq,
                             uint16_t declaredSizePayload,
                             const uint8_t *pPayload,
                             uint16_t lenPayload) {
  uint8_t crc = 0;
  uint16_t pos = 0;

  // Start of frame
  pbuf[pos++] = DLE;
  pbuf[pos++] = STX;

  // Frame type
  pbuf[pos++] = frameType;
  crc8(&crc, frameType);

  // Channel
  pbuf[pos++] = channel;
  crc8(&crc, channel);

  // Sequency number
  pos += can4vscp_addWithEscape(pbuf + pos, seq, &crc);

  // Size of payload
  pos += can4vscp_addWithEscape(
      pbuf + pos, (uint8_t)((declaredSizePayload >> 8) & 0xff), &crc);
  pos += can4vscp_addWithEscape(pbuf + pos,
                                (uint8_t)(declaredSizePayload & 0xff), &crc);

  // Payload
  for (uint16_t i = 0; i < lenPayload; i++) {
    pos += can4vscp_addWithEscape(pbuf + pos, pPayload[i], &crc);
  }

  // Checksum
  pos += can4vscp_addWithEscape(pbuf + pos, crc, NULL);

  // End of frame
  pbuf[pos++] = DLE;
  pbuf[pos++] = ETX;

  return pos;
}

///////////////////////////////////////////////////////////////////////////////
// can4vscp_checkCRC
//

int can4vscp_checkCRC(const uint8_t *pbuf, uint16_t len) {
  uint8_t crc = 0;

  if (0 == len) {
    return 0;
  }

  for (uint16_t i = 0; i < (uint16_t)(len - 1); i++) {
    crc8(&crc, pbuf[i]);
  }

  return (crc == pbuf[len - 1]);
}
