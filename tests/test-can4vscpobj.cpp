// test-can4vscpobj.cpp
//
// Test application for the CCan4VSCPObj class.
//
// Part 1 tests pure logic (protocol framing, CRC, filter/mask and
// error paths) without any hardware.
//
// Part 2 emulates a CAN4VSCP adapter on the master side of a
// pseudo-terminal (pty) and drives the real driver object end-to-end:
// open, command/caps handshake, transmit, loopback receive and close.
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

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <canal.h>
#include <crc8.h>

#include "can4vscp-protocol.h"
#include "can4vscpobj.h"

// ---------------------------------------------------------------------------
// Tiny test framework
// ---------------------------------------------------------------------------

static int gTestCount = 0;
static int gFailCount = 0;

static void
check(bool ok, const char *fmt, ...)
{
  char msg[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  gTestCount++;
  if (ok) {
    printf("  [PASS] %s\n", msg);
  }
  else {
    gFailCount++;
    printf("  [FAIL] %s\n", msg);
  }
}

static void
section(const char *name)
{
  printf("\n=== %s ===\n", name);
}

// ---------------------------------------------------------------------------
// CAN4VSCP adapter emulator
//
// Runs on the pty master. Parses DLE-stuffed frames from the driver,
// checks CRC and answers like a real CAN4VSCP adapter:
//   - COMMAND (255)      -> COMMAND_REPLY (254), payload {0, cmdcode}
//   - CAPS_REQUEST (8)   -> CAPS_RESPONSE (9), 4 byte capabilities
//   - CONFIGURE (3)      -> ACK (251), payload {0, configcode}
//   - CANAL (2)          -> ACK (251) + frame looped back as a received
//                           CANAL frame so that readMsg() gets it
// ---------------------------------------------------------------------------

class AdapterEmulator {
public:
  explicit AdapterEmulator(int fdMaster)
    : m_fd(fdMaster)
  {
  }

  void start()
  {
    m_bRun = true;
    m_thread = std::thread(&AdapterEmulator::run, this);
  }

  void stop()
  {
    m_bRun = false;
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  // Statistics collected by the emulator
  std::atomic<int> cntNoop{ 0 };      // NOOP commands seen
  std::atomic<int> cntOpen{ 0 };      // OPEN commands seen
  std::atomic<int> cntClose{ 0 };     // CLOSE commands seen
  std::atomic<int> cntCaps{ 0 };      // Capability requests seen
  std::atomic<int> cntCanal{ 0 };     // CANAL frames seen
  std::atomic<int> cntCrcErrors{ 0 }; // Frames with bad CRC

private:
  void writeAll(const uint8_t *pbuf, size_t len)
  {
    size_t pos = 0;
    while (pos < len) {
      ssize_t n = write(m_fd, pbuf + pos, len - pos);
      if (n <= 0) {
        if ((EAGAIN == errno) || (EINTR == errno)) {
          usleep(1000);
          continue;
        }
        return;
      }
      pos += (size_t)n;
    }
  }

  void sendFrame(uint8_t frameType,
                 uint8_t channel,
                 uint8_t seq,
                 uint16_t declaredSizePayload,
                 const uint8_t *pPayload,
                 uint16_t lenPayload)
  {
    uint8_t buf[1024];
    const uint16_t len = can4vscp_buildFrame(
      buf, frameType, channel, seq, declaredSizePayload, pPayload, lenPayload);
    writeAll(buf, len);
  }

  // A complete destuffed frame is in m_frame (type ... payload crc)
  void handleFrame()
  {
    if (m_frame.size() < 6) {
      return; // Runt frame
    }

    if (!can4vscp_checkCRC(m_frame.data(), (uint16_t)m_frame.size())) {
      cntCrcErrors++;
      return;
    }

    const uint8_t frameType = m_frame[VSCP_CAN4VSCP_DRIVER_POS_FRAME_TYPE];
    const uint8_t channel = m_frame[VSCP_CAN4VSCP_DRIVER_POS_FRAME_CHANNEL];
    const uint8_t seq = m_frame[VSCP_CAN4VSCP_DRIVER_POS_FRAME_SEQUENCY];
    const uint16_t sizePayload =
      ((uint16_t)m_frame[VSCP_CAN4VSCP_DRIVER_POS_FRAME_SIZE_PAYLOAD_MSB]
       << 8) +
      m_frame[VSCP_CAN4VSCP_DRIVER_POS_FRAME_SIZE_PAYLOAD_LSB];
    const uint8_t *payload = m_frame.data() + VSCP_CAN4VSCP_DRIVER_POS_FRAME_PAYLOAD;

    switch (frameType) {

      case 255: { // VSCP_SERIAL_DRIVER_FRAME_TYPE_COMMAND
        const uint8_t cmdcode = payload[0];
        switch (cmdcode) {
          case VSCP_CAN4VSCP_DRIVER_COMMAND_NOOP:
            cntNoop++;
            break;
          case VSCP_CAN4VSCP_DRIVER_COMMAND_OPEN:
          case VSCP_CAN4VSCP_DRIVER_COMMAND_LISTEN:
          case VSCP_CAN4VSCP_DRIVER_COMMAND_LOOPBACK:
            cntOpen++;
            break;
          case VSCP_CAN4VSCP_DRIVER_COMMAND_CLOSE:
            cntClose++;
            break;
          default:
            break;
        }
        // COMMAND_REPLY (254): payload = {0 == OK, cmdcode}
        const uint8_t reply[2] = { 0, cmdcode };
        sendFrame(254, channel, seq, 2, reply, 2);
        break;
      }

      case 8: { // VSCP_SERIAL_DRIVER_FRAME_TYPE_CAPS_REQUEST
        cntCaps++;
        // CAPS_RESPONSE (9): maxVscpFrames=2, maxCanalFrames=10
        const uint8_t caps[4] = { 0, 2, 0, 10 };
        sendFrame(9, channel, seq, 4, caps, 4);
        break;
      }

      case 3: { // VSCP_SERIAL_DRIVER_FRAME_TYPE_CONFIGURE
        // ACK (251): payload = {0 == OK, configcode}
        const uint8_t reply[2] = { 0, payload[0] };
        sendFrame(251, channel, seq, 2, reply, 2);
        break;
      }

      case 2: { // VSCP_SERIAL_DRIVER_FRAME_TYPE_CANAL
        cntCanal++;

        // Driver frame payload: id(4) + data. dlc is implicit.
        if (sizePayload < 4) {
          break;
        }
        const uint8_t dlc = (uint8_t)(sizePayload - 4);

        // ACK the transmission
        sendFrame(251, channel, seq, 0, NULL, 0);

        // Loop the frame back as a frame received from the CAN bus.
        // Received CANAL frame payload: id(4) + dlc(1) + data.
        uint8_t loop[4 + 1 + 8];
        memcpy(loop, payload, 4); // id
        loop[4] = dlc;
        if (dlc) {
          memcpy(loop + 5, payload + 4, (dlc > 8) ? 8 : dlc);
        }
        sendFrame(2, 0, m_txseq++, (uint16_t)(4 + 1 + dlc), loop, (uint16_t)(4 + 1 + dlc));
        break;
      }

      default:
        break;
    }
  }

  void feed(uint8_t c)
  {
    if (!m_bInFrame) {
      if (m_bLastWasDLE && (STX == c)) {
        m_bInFrame = true;
        m_frame.clear();
      }
      m_bLastWasDLE = (DLE == c);
      return;
    }

    if (m_bPendingDLE) {
      m_bPendingDLE = false;
      if (DLE == c) {
        m_frame.push_back(DLE); // Stuffed data byte
      }
      else if (ETX == c) {
        m_bInFrame = false;
        m_bLastWasDLE = false;
        handleFrame();
      }
      else if (STX == c) {
        m_frame.clear(); // Frame restart
      }
      else {
        m_bInFrame = false; // Protocol error
        m_bLastWasDLE = false;
      }
      return;
    }

    if (DLE == c) {
      m_bPendingDLE = true;
    }
    else {
      m_frame.push_back(c);
    }
  }

  void run()
  {
    uint8_t buf[256];

    while (m_bRun) {

      struct pollfd pfd;
      pfd.fd = m_fd;
      pfd.events = POLLIN;

      const int rv = poll(&pfd, 1, 20);
      if (rv <= 0) {
        continue;
      }

      const ssize_t n = read(m_fd, buf, sizeof(buf));
      if (n <= 0) {
        continue;
      }

      for (ssize_t i = 0; i < n; i++) {
        feed(buf[i]);
      }
    }
  }

  int m_fd;
  std::atomic<bool> m_bRun{ false };
  std::thread m_thread;

  // Frame parser state
  std::vector<uint8_t> m_frame;
  bool m_bInFrame = false;
  bool m_bPendingDLE = false;
  bool m_bLastWasDLE = false;

  uint8_t m_txseq = 0; // Sequency for frames sent to the driver
};

// ---------------------------------------------------------------------------
// Part 1: Pure logic tests (no hardware, no pty)
// ---------------------------------------------------------------------------

static void
testProtocolFraming()
{
  section("Protocol framing and CRC");

  uint8_t frame[128];
  const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };

  // Simple frame round trip
  uint16_t len = can4vscp_buildFrame(frame, 2, 0, 42, 4, payload, 4);
  check(len == (2 + 5 + 4 + 1 + 2), "buildFrame length is %u for 4 byte payload", len);
  check(DLE == frame[0] && STX == frame[1], "frame starts with DLE STX");
  check(DLE == frame[len - 2] && ETX == frame[len - 1], "frame ends with DLE ETX");
  check(frame[2] == 2 && frame[3] == 0 && frame[4] == 42,
        "frame header holds type, channel and sequency");

  // CRC check on the destuffed content (type..payload crc)
  check(0 != can4vscp_checkCRC(frame + 2, (uint16_t)(len - 4)),
        "checkCRC accepts a valid frame");

  // Corrupt one payload byte - CRC must fail
  uint8_t bad[128];
  memcpy(bad, frame, len);
  bad[7] ^= 0xff;
  check(0 == can4vscp_checkCRC(bad + 2, (uint16_t)(len - 4)),
        "checkCRC rejects a corrupted frame");

  // Zero length is invalid
  check(0 == can4vscp_checkCRC(frame, 0), "checkCRC rejects zero length");

  // DLE byte stuffing: a payload containing DLE must be escaped
  const uint8_t dlePayload[] = { DLE, DLE, 0x55 };
  len = can4vscp_buildFrame(frame, 2, 0, 1, 3, dlePayload, 3);
  int cntDLE = 0;
  for (uint16_t i = 2; i < (uint16_t)(len - 2); i++) {
    if (DLE == frame[i]) {
      cntDLE++;
    }
  }
  check(cntDLE >= 4, "DLE payload bytes are byte stuffed (%d DLE in body)", cntDLE);

  // addWithEscape
  uint8_t buf[4];
  uint8_t crc = 0;
  check(2 == can4vscp_addWithEscape(buf, DLE, &crc), "addWithEscape doubles DLE");
  check(DLE == buf[0] && DLE == buf[1], "escaped DLE is DLE DLE");
  crc = 0;
  check(1 == can4vscp_addWithEscape(buf, 0x55, &crc), "addWithEscape passes normal byte");
}

static void
testFilterMask()
{
  section("Filter and mask logic");

  CCan4VSCPObj obj;

  canalMsg msg;
  memset(&msg, 0, sizeof(msg));
  msg.id = 0x12345;
  msg.flags = CANAL_IDFLAG_EXTENDED;

  // Default: mask == 0 lets everything through
  check(obj.doFilter(&msg), "mask 0 accepts every id");

  check(CANAL_ERROR_SUCCESS == obj.setMask(0xffffffff), "setMask returns success");
  check(CANAL_ERROR_SUCCESS == obj.setFilter(0), "setFilter returns success");

  // filter 0 with full mask rejects a non matching id
  check(!obj.doFilter(&msg), "full mask with filter 0 rejects nonzero id");

  // Back to open filter
  obj.setMask(0);
  check(obj.doFilter(&msg), "resetting mask to 0 accepts the id again");
}

static void
testErrorPaths()
{
  section("Error paths on a closed driver object");

  CCan4VSCPObj obj;
  canalMsg msg;
  memset(&msg, 0, sizeof(msg));
  canalStatus status;
  canalStatistics stats;

  check(CANAL_ERROR_PARAMETER == obj.writeMsg(NULL), "writeMsg(NULL) -> PARAMETER");
  check(CANAL_ERROR_NOT_OPEN == obj.writeMsg(&msg), "writeMsg on closed -> NOT_OPEN");
  check(CANAL_ERROR_PARAMETER == obj.readMsg(NULL), "readMsg(NULL) -> PARAMETER");
  check(CANAL_ERROR_NOT_OPEN == obj.readMsg(&msg), "readMsg on closed -> NOT_OPEN");
  check(CANAL_ERROR_NOT_OPEN == obj.readMsgBlocking(&msg, 10),
        "readMsgBlocking on closed -> NOT_OPEN");
  check(CANAL_ERROR_NOT_OPEN == obj.writeMsgBlocking(&msg, 10),
        "writeMsgBlocking on closed -> NOT_OPEN");
  check(CANAL_ERROR_PARAMETER == obj.getStatus(NULL), "getStatus(NULL) -> PARAMETER");
  check(CANAL_ERROR_NOT_OPEN == obj.getStatus(&status), "getStatus on closed -> NOT_OPEN");
  check(CANAL_ERROR_PARAMETER == obj.getStatistics(NULL),
        "getStatistics(NULL) -> PARAMETER");
  check(CANAL_ERROR_SUCCESS == obj.getStatistics(&stats),
        "getStatistics works without open");
  check(0 == obj.dataAvailable(), "dataAvailable is 0 on closed driver");
  check(CANAL_ERROR_SUCCESS == obj.close(), "close on a never opened object is a no-op");
}

// ---------------------------------------------------------------------------
// Part 2: End-to-end test against the pty adapter emulator
// ---------------------------------------------------------------------------

static void
testLiveDriver()
{
  section("End-to-end driver test on emulated adapter (pty)");

  // Create the pseudo terminal pair
  const int fdMaster = posix_openpt(O_RDWR | O_NOCTTY);
  if ((fdMaster < 0) || (0 != grantpt(fdMaster)) || (0 != unlockpt(fdMaster))) {
    check(false, "unable to create pty pair");
    return;
  }

  const char *pSlaveName = ptsname(fdMaster);
  if (NULL == pSlaveName) {
    check(false, "unable to get pty slave name");
    ::close(fdMaster);
    return;
  }
  printf("  Emulated adapter on %s\n", pSlaveName);

  AdapterEmulator emulator(fdMaster);
  emulator.start();

  {
    CCan4VSCPObj obj;

    // Config string: "device;baudcode" - baud code 0 == 115200
    std::string config = std::string(pSlaveName) + ";0";

    check(CANAL_ERROR_SUCCESS ==
            obj.open(config.c_str(), CAN4VSCP_FLAG_ENABLE_STRICT),
          "open succeeds against emulated adapter");
    check(obj.m_bOpen, "driver reports open state");
    check(emulator.cntNoop > 0, "emulator received initial NOOP command");
    check(emulator.cntCaps > 0, "emulator received capabilities request");
    check(emulator.cntOpen > 0, "emulator received OPEN command");
    check(2 == obj.m_caps.maxVscpFrames, "capabilities: maxVscpFrames == 2");
    check(10 == obj.m_caps.maxCanalFrames, "capabilities: maxCanalFrames == 10");

    canalStatus status;
    check(CANAL_ERROR_SUCCESS == obj.getStatus(&status), "getStatus on open driver");

    // Transmit a frame - the emulator loops it back
    canalMsg tx;
    memset(&tx, 0, sizeof(tx));
    tx.id = 0x0a123456 & 0x1fffffff;
    tx.flags = CANAL_IDFLAG_EXTENDED;
    tx.sizeData = 5;
    tx.data[0] = 0x11;
    tx.data[1] = 0x22;
    tx.data[2] = 0x33;
    tx.data[3] = 0x44;
    tx.data[4] = 0x55;

    check(CANAL_ERROR_SUCCESS == obj.writeMsg(&tx), "writeMsg succeeds");

    canalMsg rx;
    memset(&rx, 0, sizeof(rx));
    const int rvRead = obj.readMsgBlocking(&rx, 3000);
    check(CANAL_ERROR_SUCCESS == rvRead, "readMsgBlocking got looped back frame (rv=%d)", rvRead);
    if (CANAL_ERROR_SUCCESS == rvRead) {
      check(tx.id == (rx.id & 0x1fffffff), "looped back id matches (0x%08lX)", rx.id);
      check(tx.sizeData == rx.sizeData, "looped back dlc matches (%d)", rx.sizeData);
      check(0 == memcmp(tx.data, rx.data, tx.sizeData), "looped back data matches");
      check(0 != (rx.flags & CANAL_IDFLAG_EXTENDED), "received frame is extended");
    }
    check(emulator.cntCanal > 0, "emulator received the CANAL frame");

    // Zero length frame round trip
    canalMsg txEmpty;
    memset(&txEmpty, 0, sizeof(txEmpty));
    txEmpty.id = 0x99;
    txEmpty.flags = CANAL_IDFLAG_EXTENDED;
    txEmpty.sizeData = 0;
    check(CANAL_ERROR_SUCCESS == obj.writeMsgBlocking(&txEmpty, 1000),
          "writeMsgBlocking succeeds for zero length frame");

    memset(&rx, 0, sizeof(rx));
    check(CANAL_ERROR_SUCCESS == obj.readMsgBlocking(&rx, 3000),
          "zero length frame is looped back");
    check(0 == rx.sizeData && 0x99 == (rx.id & 0x1fffffff),
          "zero length frame id/dlc are correct");

    // Queue several frames, verify dataAvailable + readMsg
    for (int i = 0; i < 3; i++) {
      canalMsg burst;
      memset(&burst, 0, sizeof(burst));
      burst.id = (unsigned long)(0x100 + i);
      burst.flags = CANAL_IDFLAG_EXTENDED;
      burst.sizeData = 1;
      burst.data[0] = (unsigned char)i;
      obj.writeMsg(&burst);
    }

    // Wait for the three loopbacks to arrive
    int waitloops = 0;
    while ((obj.dataAvailable() < 3) && (waitloops++ < 300)) {
      usleep(10000);
    }
    check(3 == obj.dataAvailable(), "dataAvailable reports 3 queued frames (got %d)",
          obj.dataAvailable());

    for (int i = 0; i < 3; i++) {
      memset(&rx, 0, sizeof(rx));
      check(CANAL_ERROR_SUCCESS == obj.readMsg(&rx), "readMsg drains frame %d", i);
      check((unsigned long)(0x100 + i) == (rx.id & 0x1fffffff),
            "frame %d received in FIFO order", i);
    }
    check(CANAL_ERROR_FIFO_EMPTY == obj.readMsg(&rx),
          "readMsg on drained queue -> FIFO_EMPTY");
    check(CANAL_ERROR_TIMEOUT == obj.readMsgBlocking(&rx, 100),
          "readMsgBlocking on empty queue times out");

    // Statistics
    canalStatistics stats;
    check(CANAL_ERROR_SUCCESS == obj.getStatistics(&stats), "getStatistics succeeds");
    check(stats.cntTransmitFrames >= 5, "statistics: cntTransmitFrames >= 5 (got %lu)",
          stats.cntTransmitFrames);
    check(stats.cntReceiveFrames >= 5, "statistics: cntReceiveFrames >= 5 (got %lu)",
          stats.cntReceiveFrames);

    // Close
    check(CANAL_ERROR_SUCCESS == obj.close(), "close succeeds");
    check(!obj.m_bOpen, "driver reports closed state");
    check(emulator.cntClose > 0, "emulator received CLOSE command");
    check(0 == emulator.cntCrcErrors, "no CRC errors seen by emulator");
  }

  emulator.stop();
  ::close(fdMaster);
}

// ---------------------------------------------------------------------------
// Live stream mode: open a real serial device and dump received frames
// ---------------------------------------------------------------------------

static volatile sig_atomic_t gAbort = 0;

static void
sigHandler(int)
{
  gAbort = 1;
}

static int
liveStream(const char *pConfig, unsigned long flags, long maxFrames)
{
  printf("Live stream mode\n");
  printf("  config : %s\n", pConfig);
  printf("  flags  : 0x%08lX\n", flags);
  printf("Press Ctrl+C to stop.\n\n");

  signal(SIGINT, sigHandler);
  signal(SIGTERM, sigHandler);

  CCan4VSCPObj obj;

  const int rv = obj.open(pConfig, flags);
  if (CANAL_ERROR_SUCCESS != rv) {
    fprintf(stderr, "Failed to open device [%s] rv=%d\n", pConfig, rv);
    return EXIT_FAILURE;
  }
  printf("Device open.\n\n");

  long cntFrames = 0;
  while (!gAbort && ((maxFrames <= 0) || (cntFrames < maxFrames))) {

    canalMsg msg;
    memset(&msg, 0, sizeof(msg));

    const int rvRead = obj.readMsgBlocking(&msg, 500);
    if (CANAL_ERROR_TIMEOUT == rvRead) {
      continue;
    }
    if (CANAL_ERROR_SUCCESS != rvRead) {
      fprintf(stderr, "readMsgBlocking failed rv=%d\n", rvRead);
      break;
    }

    cntFrames++;
    printf("%6ld  ts=%10lu  id=0x%08lX  %s%sdlc=%u  data=[",
           cntFrames,
           msg.timestamp,
           msg.id & 0x1fffffff,
           (msg.flags & CANAL_IDFLAG_EXTENDED) ? "ext " : "",
           (msg.flags & CANAL_IDFLAG_RTR) ? "rtr " : "",
           msg.sizeData);
    for (unsigned i = 0; i < msg.sizeData; i++) {
      printf("%s%02X", i ? " " : "", msg.data[i]);
    }
    printf("]\n");
  }

  canalStatistics stats;
  if (CANAL_ERROR_SUCCESS == obj.getStatistics(&stats)) {
    printf("\nStatistics\n");
    printf("  received frames : %lu\n", stats.cntReceiveFrames);
    printf("  received bytes  : %lu\n", stats.cntReceiveData);
    printf("  overruns        : %lu\n", stats.cntOverruns);
  }

  obj.close();
  printf("Device closed.\n");

  return EXIT_SUCCESS;
}

static void
usage(const char *pName)
{
  printf("Test application for the CAN4VSCP driver object (CCan4VSCPObj)\n\n");
  printf("Usage: %s [options]\n\n", pName);
  printf("Without options the self test (logic + pty adapter emulator) is run.\n\n");
  printf("Options:\n");
  printf("  --logic-only          Run only the pure logic tests (no pty)\n");
  printf("  --device <config>     Live stream mode: open a real device and dump\n");
  printf("                        received frames. Config is the driver string\n");
  printf("                        \"device[;baudcode[;udphost[:udpport][;loglevel]]]\"\n");
  printf("                        e.g. \"/dev/ttyUSB0;0\" (baud code 0 = 115200)\n");
  printf("  --flags <hex>         Driver open flags for --device (default 0)\n");
  printf("                          bit 0/1  mode: 0=normal 1=listen 2=loopback\n");
  printf("                          bit 2    no switch to CAN4VSCP mode\n");
  printf("                          bit 3    wait for ACK on sent frames\n");
  printf("                          bit 4    hardware timestamp\n");
  printf("                          bit 5    hardware handshake\n");
  printf("                          bit 6    enable reopen\n");
  printf("                          bit 8    strict mode (fail on any error)\n");
  printf("                          bit 30   UDP debug output\n");
  printf("                          bit 31   debug logging\n");
  printf("  --count <n>           Stop live stream after n frames (default: endless)\n");
  printf("  -h, --help            Show this help\n\n");
  printf("Examples:\n");
  printf("  %s\n", pName);
  printf("  %s --logic-only\n", pName);
  printf("  %s --device \"/dev/ttyUSB0;0\" --count 100\n", pName);
  printf("  %s --device \"/dev/ttyUSB0;0\" --flags 80000000\n", pName);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main(int argc, char *argv[])
{
  bool bSkipLive = false;
  const char *pDeviceConfig = NULL;
  unsigned long deviceFlags = 0;
  long maxFrames = 0;

  for (int i = 1; i < argc; i++) {
    if (0 == strcmp(argv[i], "--logic-only")) {
      bSkipLive = true;
    }
    else if ((0 == strcmp(argv[i], "--device")) && ((i + 1) < argc)) {
      pDeviceConfig = argv[++i];
    }
    else if ((0 == strcmp(argv[i], "--flags")) && ((i + 1) < argc)) {
      deviceFlags = strtoul(argv[++i], NULL, 16);
    }
    else if ((0 == strcmp(argv[i], "--count")) && ((i + 1) < argc)) {
      maxFrames = atol(argv[++i]);
    }
    else if ((0 == strcmp(argv[i], "--help")) || (0 == strcmp(argv[i], "-h"))) {
      usage(argv[0]);
      return EXIT_SUCCESS;
    }
    else {
      fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  // The protocol CRC table must be initialized before use
  init_crc8();

  // Live stream mode: read from a real device instead of running tests
  if (NULL != pDeviceConfig) {
    return liveStream(pDeviceConfig, deviceFlags, maxFrames);
  }

  printf("CCan4VSCPObj test application\n");
  printf("=============================\n");

  testProtocolFraming();
  testFilterMask();
  testErrorPaths();

  if (!bSkipLive) {
    testLiveDriver();
  }

  printf("\n----------------------------------------\n");
  printf("%d tests, %d failed\n", gTestCount, gFailCount);

  return gFailCount ? EXIT_FAILURE : EXIT_SUCCESS;
}
