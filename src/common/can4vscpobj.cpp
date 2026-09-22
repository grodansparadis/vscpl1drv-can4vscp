// can4vscpobj.cpp:
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
// Boston, MA 02111-1307, USA.620
//


#include "can4vscpobj.h"
#include "dlldrvobj.h"
#include <cstdarg>
#include <crc8.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vscp-serial.h>

#ifdef WIN32
#include "callback.h"
#else
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#endif

#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <csignal>
#ifdef WIN32
#else
#include <unistd.h>
#endif



///////////////////////////////////////////////////////////////////////////////
// nextConfigToken
//
// Next ';'-separated token, preserving empty fields (unlike strtok)
//

char *
nextConfigToken(char **ppCursor)
{
  if ((NULL == ppCursor) || (NULL == *ppCursor)) {
    return NULL;
  }
  char *pToken = *ppCursor;
  char *pSep   = strchr(pToken, ';');
  if (NULL != pSep) {
    *pSep     = 0;
    *ppCursor = pSep + 1;
  }
  else {
    *ppCursor = NULL;
  }
  return pToken;
}

///////////////////////////////////////////////////////////////////////////////
// syslogPriority
//

#ifndef WIN32
// Minimum level for the syslog channel. spdlog::level::off disables it.
spdlog::level::level_enum gSyslogLevel = spdlog::level::info;

int
syslogPriority(spdlog::level::level_enum level)
{
  switch (level) {
    case spdlog::level::trace:
    case spdlog::level::debug:
      return LOG_DEBUG;
    case spdlog::level::warn:
      return LOG_WARNING;
    case spdlog::level::err:
      return LOG_ERR;
    case spdlog::level::critical:
      return LOG_CRIT;
    case spdlog::level::info:
    default:
      return LOG_INFO;
  }
}

///////////////////////////////////////////////////////////////////////////////
// parseLogLevel
//
// Parse a log level name, default "info" when absent or unknown
//

spdlog::level::level_enum
parseLogLevel(const char *pLevel)
{
  if ((NULL == pLevel) || !*pLevel) {
    return spdlog::level::info;
  }
  if (0 == strcasecmp(pLevel, "trace")) {
    return spdlog::level::trace;
  }
  if (0 == strcasecmp(pLevel, "debug")) {
    return spdlog::level::debug;
  }
  if ((0 == strcasecmp(pLevel, "warn")) || (0 == strcasecmp(pLevel, "warning"))) {
    return spdlog::level::warn;
  }
  if ((0 == strcasecmp(pLevel, "err")) || (0 == strcasecmp(pLevel, "error"))) {
    return spdlog::level::err;
  }
  if (0 == strcasecmp(pLevel, "critical")) {
    return spdlog::level::critical;
  }
  if (0 == strcasecmp(pLevel, "off")) {
    return spdlog::level::off;
  }
  return spdlog::level::info;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// semaphoreTimedWait
//
// Called to wait on a semaphore with a timeout. Returns 0 on success, -1 on failure (including timeout).
//

#ifndef WIN32
int
semaphoreTimedWait(vscp_sem_t *psem, uint32_t timeoutMs)
{
#ifdef __APPLE__
  if (0 == timeoutMs) {
    const long waitResult = dispatch_semaphore_wait(*psem, DISPATCH_TIME_NOW);
    if (0 == waitResult) {
      return 0;
    }

    errno = EAGAIN;
    return -1;
  }

  const dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeoutMs) * 1000000LL);
  const long waitResult         = dispatch_semaphore_wait(*psem, timeout);
  if (0 == waitResult) {
    return 0;
  }

  errno = EAGAIN;
  return -1;
#else
  struct timespec deadline = { 0, 0 };
  clock_gettime(CLOCK_REALTIME, &deadline);

  deadline.tv_sec += timeoutMs / 1000;
  deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
  }

  const int waitResult = sem_timedwait(psem, &deadline);
  if ((-1 == waitResult) && (ETIMEDOUT == errno)) {
    errno = EAGAIN; // normalize timeout errno with the macOS wrapper
  }
  return waitResult;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// semaphoreInit
//
// Called to initialize a semaphore. Returns 0 on success, -1 on failure.
//

int
semaphoreInit(vscp_sem_t *psem, unsigned int initialValue)
{
#ifdef __APPLE__
  *psem = dispatch_semaphore_create(static_cast<long>(initialValue));
  return (NULL != *psem) ? 0 : -1;
#else
  return sem_init(psem, 0, initialValue);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// semaphorePost
//
// Called to post (signal) a semaphore. Returns 0 on success, -1 on failure.
//

int
semaphorePost(vscp_sem_t *psem)
{
#ifdef __APPLE__
  dispatch_semaphore_signal(*psem);
  return 0;
#else
  return sem_post(psem);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// semaphoreDestroy
//
// Called to destroy a semaphore. Returns 0 on success, -1 on failure.
//

int
semaphoreDestroy(vscp_sem_t *psem)
{
#ifdef __APPLE__
#if !OS_OBJECT_USE_OBJC
  if (NULL != *psem) {
    dispatch_release(*psem);
  }
#endif
  *psem = NULL;
  return 0;
#else
  return sem_destroy(psem);
#endif
}
#endif



// Prototypes
#ifdef WIN32
void
workThreadTransmit(void *pObject);
void
workThreadReceive(void *pObject);
#else
void *
workThreadTransmit(void *pObject);
void *
workThreadReceive(void *pObject);
#endif

///////////////////////////////////////////////////////////////////////////////
// getClockMilliseconds
//
//

static uint32_t
getClockMilliSeconds()
{
#ifdef WIN32
  return (uint32_t) ((1000 * (float) clock()) / CLOCKS_PER_SEC);
#else
  timeval curTime;
  gettimeofday(&curTime, NULL);
  return 1000 * curTime.tv_sec + curTime.tv_usec / 1000;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// getClockMicroSeconds
//
//

static uint32_t
getClockMicroSeconds()
{
#ifdef WIN32
  return (uint32_t) ((1000000 * (float) clock()) / CLOCKS_PER_SEC);
#else
  timeval curTime;
  gettimeofday(&curTime, NULL);
  return 1000000 * curTime.tv_sec + curTime.tv_usec;
#endif
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// CCan4VSCPObj
//

CCan4VSCPObj::CCan4VSCPObj()
{
  m_initFlag = 0;

  // No filter mask
  m_filter = 0;
  m_mask   = 0;

  m_nBaud = SET_BAUDRATE_115200;

  // Default capabilities of connected device
  m_caps.maxVscpFrames  = 1; // We expect: One VSCP frame handled.
  m_caps.maxCanalFrames = 1; // We expect: One CANAL frame handled.

  m_bRun      = false;
  m_bOpen     = false;
  m_bStrict   = false;

  m_RxMsgState    = INCOMING_STATE_NONE;
  m_RxMsgSubState = INCOMING_SUBSTATE_NONE;

  m_sequencyno = 0; // No frames sent yet

  memset(&msgResponseInfo, 0, sizeof(msgResponseInfo));

  m_activity = getClockMilliSeconds();

#ifdef WIN32

  m_pdeviceName = "COM1"; // Set default device name

  m_hTreadReceive  = 0;
  m_hTreadTransmit = 0;

  m_receiveDataEvent = NULL;

  m_transmitDataPutEvent = NULL;
  m_transmitDataGetEvent = NULL;

  m_transmitAckNackEvent = NULL;

  // Create the device AND LIST access mutexes
  // (unnamed = process local, not initially owned)
  m_can4vscpMutex = CreateMutex(NULL, FALSE, NULL);
  m_receiveMutex  = CreateMutex(NULL, FALSE, NULL);
  m_transmitMutex = CreateMutex(NULL, FALSE, NULL);
  m_responseMutex = CreateMutex(NULL, FALSE, NULL);

  // Events
  m_receiveDataEvent     = CreateEvent(NULL, TRUE, FALSE, NULL);
  m_transmitDataPutEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  m_transmitDataGetEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  m_transmitAckNackEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

#else

  m_pdeviceName = "/dev/ttyUSB0"; // Set default device name

  pthread_mutex_init(&m_can4vscpMutex, NULL);
  pthread_mutex_init(&m_receiveMutex, NULL);
  pthread_mutex_init(&m_transmitMutex, NULL);
  pthread_mutex_init(&m_responseMutex, NULL);

  semaphoreInit(&m_receiveDataSem, 0);
  semaphoreInit(&m_transmitDataPutSem, 0);
  semaphoreInit(&m_transmitDataGetSem, 0);
  semaphoreInit(&m_transmitAckNackSem, 0);

#endif

  // Initialize lists
  dll_init(&m_transmitList, SORT_NONE);
  dll_init(&m_receiveList, SORT_NONE);
  dll_init(&m_responseList, SORT_NONE);

}

//////////////////////////////////////////////////////////////////////
// ~CCan4VSCPObj
//

CCan4VSCPObj::~CCan4VSCPObj()
{
  if (m_bRun) {
    close();
  }

  cleanup();
}



//////////////////////////////////////////////////////////////////////
// cleanup
//

void
CCan4VSCPObj::cleanup()
{
  LOCK_MUTEX(m_transmitMutex);
  dll_removeAllNodes(&m_transmitList);
  UNLOCK_MUTEX(m_transmitMutex);

  LOCK_MUTEX(m_receiveMutex);
  dll_removeAllNodes(&m_receiveList);
  UNLOCK_MUTEX(m_receiveMutex);

  LOCK_MUTEX(m_responseMutex);
  dll_removeAllNodes(&m_responseList);
  UNLOCK_MUTEX(m_responseMutex);

#ifdef WIN32

  if (NULL != m_can4vscpMutex)
    CloseHandle(m_can4vscpMutex);

  if (NULL != m_receiveMutex)
    CloseHandle(m_receiveMutex);

  if (NULL != m_transmitMutex)
    CloseHandle(m_transmitMutex);

  if (NULL != m_responseMutex)
    CloseHandle(m_responseMutex);

  // events
  if (NULL != m_receiveDataEvent)
    CloseHandle(m_receiveDataEvent);

  if (NULL != m_transmitDataPutEvent)
    CloseHandle(m_transmitDataPutEvent);

  if (NULL != m_transmitDataGetEvent)
    CloseHandle(m_transmitDataGetEvent);

  if (NULL != m_transmitAckNackEvent)
    CloseHandle(m_transmitAckNackEvent);

#else

  semaphoreDestroy(&m_receiveDataSem);
  semaphoreDestroy(&m_transmitDataPutSem);
  semaphoreDestroy(&m_transmitDataGetSem);
  semaphoreDestroy(&m_transmitAckNackSem);

  pthread_mutex_destroy(&m_can4vscpMutex);
  pthread_mutex_destroy(&m_receiveMutex);
  pthread_mutex_destroy(&m_transmitMutex);
  pthread_mutex_destroy(&m_responseMutex);

#endif
}

//////////////////////////////////////////////////////////////////////
// open
//
//
// pConfig
//-----------------------------------------------------------------------------
// Parameters for the driver as a string on the following form
//
// "comport[;nBaud[;udphost[:udpport][;loglevel]]]"
//
// Fields may be left empty to keep their defaults, e.g.
// "/dev/ttyUSB0;;;debug"
//
// comport
// =======
//	WIN32: 1 for COM1, 2 for COM2 etc
//	LINUX: /dev/ttyS1, /dev/ttyS2 etc
//
// baudrate
// ========
// Default is 115200.
//
// udphost[:udpport]
// =================
//  Optional VSCP-UDP debug target. If given, all driver debug output is
//  also sent as UDP datagrams to this host. Default port is 9999.
//
// loglevel (Linux only)
// =====================
//  Optional level for the driver syslog channel: "trace", "debug",
//  "info", "warn", "error", "critical" or "off". Default is "info".
//
// flags
//-----------------------------------------------------------------------------
//
// bit 0/1
// =======
//	00 - Normal Mode (0)
//	01 - Listen Mode (1)
//	10 - Loopback Mode (2)
//	11 - Disabled Mode (3)
//
// bit 2
// =====
//  0  - Switch to CAN4VSCP mode is carried out on startup
//  1  - No switch to CAN4VSCP mode
//
// Bit 3
// =====
//  0  - Disable wait for ACK
//  1  - Enable wait for ACK
//
// bit 4
// =====
//  0  - Timestamp is set by driver.
//  1  - Timestamp is set by hardware.
//
// bit 5
// =====
//  0  - No hardware handshake.
//  1  - Enable hardware handshake.
//
// bit 6
// =====
//  0  - Disable reopen.
//  1  - Enable reopen.
//
// bit 8
// =====
//  0  - Try to continue even if errors occurs.
//  1  - Strict mode. Give up on all errors.
//

int
CCan4VSCPObj::open(const char *pConfig, unsigned long flags)
{
  int rv;
  char szDrvParams[PATH_MAX] = { 0 }; // Driver config string
  char *p                    = NULL;
  #ifdef WIN32
  int nComPort = 1; // Default COM port for Windows
  #endif

  spdlog::debug("[vscpl1drv-can4vscp] Opening CAN4VSCP driver with config: {}", pConfig ? pConfig : "NULL");

  cmdResponseMsg Msg;
  m_nBaud    = SET_BAUDRATE_115200;
  m_initFlag = flags;

  // Enable strict mode if asked to
  if (flags & CAN4VSCP_FLAG_ENABLE_STRICT) {
    spdlog::debug("[vscpl1drv-can4vscp] Strict mode enabled.");
    m_bStrict = true;
  }

  m_RxMsgState    = INCOMING_STATE_NONE;
  m_RxMsgSubState = INCOMING_SUBSTATE_NONE;

  // Save configuration string & set default values
#ifdef WIN32
  if (NULL != pConfig) {
    strncpy(szDrvParams, pConfig, MAX_PATH);
    spdlog::debug("[vscpl1drv-can4vscp] Using driver config string: {}", szDrvParams);
  }
  else {
    strncpy(szDrvParams, "COM1;0", MAX_PATH); // Use default port
    spdlog::debug("[vscpl1drv-can4vscp] Using default driver config string: {}", szDrvParams);
  }
  strupr(szDrvParams);
#else
  if (NULL != pConfig) {
    strncpy(szDrvParams, pConfig, PATH_MAX);
    spdlog::debug("[vscpl1drv-can4vscp] Using driver config string: {}", szDrvParams);
  }
  else {
    strncpy(szDrvParams, "/dev/ttyUSB0;0", PATH_MAX); // Use default port
    spdlog::debug("[vscpl1drv-can4vscp] Using default driver config string: {}", szDrvParams);
  }
#endif

  // Initiate statistics
  m_stat.cntReceiveData    = 0;
  m_stat.cntReceiveFrames  = 0;
  m_stat.cntTransmitData   = 0;
  m_stat.cntTransmitFrames = 0;

  m_stat.cntBusOff      = 0;
  m_stat.cntBusWarnings = 0;
  m_stat.cntOverruns    = 0;

  // if open we have noting to do
  if (m_bRun) {
    spdlog::debug("[vscpl1drv-can4vscp] Driver already running, open operation skipped.");
    return CANAL_ERROR_SUCCESS;
  }

  // Serial port
#ifdef WIN32
  nComPort = 1; // Default com port
#endif
  char *pCursor = szDrvParams;
  p             = nextConfigToken(&pCursor);
  if ((NULL != p) && *p) {
#ifdef WIN32
    if (NULL != (p = strstr(p, "COM"))) {
      nComPort = atoi(p + 3);
      spdlog::debug("[vscpl1drv-can4vscp] Using COM port: {}", nComPort);
    }
#else
    m_pdeviceName = p;
    spdlog::debug("[vscpl1drv-can4vscp] Using device name: {}", m_pdeviceName);
#endif
  }

  p = nextConfigToken(&pCursor);
  if ((NULL != p) && *p) {
    m_nBaud = atoi(p);
    // Check if a valid code
    if (m_nBaud > (SET_BAUDRATE_MAX - 1)) {
      m_nBaud = SET_BAUDRATE_115200;
    }
  }
  spdlog::debug("[vscpl1drv-can4vscp] Using baud rate: {}", m_nBaud);

  // Optional UDP debug target "udphost[:udpport]"
  char udpHost[256]      = { 0 };
  unsigned short udpPort = CAN4VSCP_UDP_DEBUG_DEFAULT_PORT;
  p                      = nextConfigToken(&pCursor);
  if ((NULL != p) && *p) {
    strncpy(udpHost, p, sizeof(udpHost) - 1);
    char *pColon = strrchr(udpHost, ':');
    if (NULL != pColon) {
      *pColon           = 0;
      const int portval = atoi(pColon + 1);
      if ((portval > 0) && (portval < 65536)) {
        udpPort = (unsigned short) portval;
      }
    }
  }
  else {
    spdlog::debug("[vscpl1drv-can4vscp] No UDP debug target specified, using defaults.");
    strncpy(udpHost, "127.0.0.1", sizeof(udpHost) - 1);
  }
  spdlog::debug("[vscpl1drv-can4vscp] Using UDP debug target: {}:{}", udpHost, udpPort);

  // Optional syslog level (Linux only), default "info"
  p = nextConfigToken(&pCursor);
#ifndef WIN32
  gSyslogLevel = parseLogLevel(p);
#endif

  spdlog::debug("[vscpl1drv-can4vscp] About to open serial interface.");

  if (VSCP_ERROR_SUCCESS != (rv = OpenSerialInterface())) {
    spdlog::error("[vscpl1drv-can4vscp] Failed to open serial interface.");
    return rv;
  }

  //----------------------------------------------------------------------
  //
  //----------------------------------------------------------------------

  // Initiate statistics
  m_stat.cntReceiveData    = 0;
  m_stat.cntReceiveFrames  = 0;
  m_stat.cntTransmitData   = 0;
  m_stat.cntTransmitFrames = 0;

  m_stat.cntBusOff      = 0;
  m_stat.cntBusWarnings = 0;
  m_stat.cntOverruns    = 0;

  // Set CAN4VSCP mode in case of device in verbose mode
  if (!(m_initFlag & CAN4VSCP_FLAG_ENABLE_NO_SWITCH_TO_NEW_MODE)) {
#ifdef WIN32
    BOOL rw = m_com.writebuf((unsigned char *) "SET MODE VSCP\r\n",
                             15); // In case of garbage in queue
    SLEEP(200);
    rw = m_com.writebuf((unsigned char *) "SET MODE VSCP\r\n",
                        15); // we set CAN4VSCP mode twice
#else
    spdlog::debug("[vscpl1drv-can4vscp] Setting CAN4VSCP mode on serial interface.");
    m_com.comm_puts((char *) "\r", 1, true);
    m_com.comm_puts((char *) "\r\n", 1, true);
    m_com.comm_puts((char *) "set mode vscp\r\n", 15,
                    true); // In case of garbage in queue
    SLEEP(100);
    {
      int cnt = 1;
      char c;
      std::string str;
      // read() returns 0 on EOF so loop only while data is delivered
      while (cnt > 0) {
        c = m_com.readChar(&cnt);
        if (cnt > 0) {
          str += c;
        }
        else if (cnt < 0) {
          spdlog::error("Read char error [{}]", c);
        }
      }

      spdlog::debug("[vscpl1drv-can4vscp] SET MODE VSCP response [{}]", str.c_str());
    }

#endif
  }

  // Check that we have a CAN4VSCP device at the other end
  // ( give it four tries before giving up )
  bool bFound = false;

  // Run run run .....
  m_bRun = true;

#ifdef WIN32

  // Start write thread
  DWORD threadId;
  if (NULL ==
      (m_hTreadTransmit = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) workThreadTransmit, this, 0, &threadId))) {
    // Failure
    close();
    spdlog::error("[vscpl1drv-can4vscp] Failed to create CAN4VSCP write thread.");
    return CANAL_ERROR_INIT_FAIL;
  }

  // Start read thread
  if (NULL ==
      (m_hTreadReceive = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) workThreadReceive, this, 0, &threadId))) {
    // Failure
    close();
    spdlog::error("[vscpl1drv-can4vscp] Failed to create CAN4VSCP read thread.");
    return CANAL_ERROR_INIT_FAIL;
  }

#else // LINUX

  //----------------------------------------------------------------------
  //	Acquire mutex
  //----------------------------------------------------------------------
  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_mutex_init(&m_can4vscpObjMutex, NULL);

  // Create the log transmit thread.
  if (pthread_create(&m_threadIdTransmit, &thread_attr, workThreadTransmit, this)) {

    spdlog::error("[vscpl1drv-can4vscp] Unable to create can4vscpdrv write thread.");
  }
  else {
    spdlog::debug("[vscpl1drv-can4vscp] CAN4VSCP write thread created successfully.");
  }
  // Create the receive thread.
  if (pthread_create(&m_threadIdReceive, &thread_attr, workThreadReceive, this)) {
    spdlog::error("[vscpl1drv-can4vscp] Unable to create can4vscpdrv receive thread.");
  }
  else {
    spdlog::debug("[vscpl1drv-can4vscp] CAN4VSCP receive thread created successfully.");
  }

  // We are open
  m_bOpen = true;

#endif

  for (int i = 0; i < 3; i++) {

    // saveseq = m_sequencyno;    // Save the sequence ordinal
    spdlog::debug("[vscpl1drv-can4vscp] Sending NOOP command to check for CAN4VSCP device.");
    if (sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_NOOP, NULL, 0, &Msg, 500)) {

      bFound = true;
      spdlog::debug("[vscpl1drv-can4vscp] CAN4VSCP device found.");
      break;
    }

    SLEEP(100);
  }

  // Give up if not found
  if (!bFound) {
    // Failure
    spdlog::error("[vscpl1drv-can4vscp] NOOP initial command test failed. CAN4VSCP device not found.");
    if (m_bStrict) {
      close();
      return CANAL_ERROR_INIT_FAIL;
    }
  }
  else {
    spdlog::debug("[vscpl1drv-can4vscp] NOOP initial command test success.");
  }

  m_bOpen = true;

  // Get capabilities of device
  // We skip response and use defaults if call is not successful
  spdlog::debug("[vscpl1drv-can4vscp] Getting device capabilities.");
  getDeviceCapabilities();

  // If timestamp needs to be enabled we enabled it here
  if (m_initFlag & CAN4VSCP_FLAG_ENABLE_TIMESTAMP) {

    spdlog::debug("[vscpl1drv-can4vscp] Enabling timestamp.");

    uint8_t conf = 1;
    spdlog::debug("[vscpl1drv-can4vscp] Sending timestamp enable command.");
    if (!sendConfigWait(VSCP_DRIVER_CONFIG_TIMESTAMP, &conf, 1, &Msg, 500)) {
      // Failure
      spdlog::error("[vscpl1drv-can4vscp] Enable of timestamp failed.");
      if (m_bStrict) {
        close();
        spdlog::debug("[vscpl1drv-can4vscp] Strict mode: Closing device due to timestamp enable failure.");
        return CANAL_ERROR_INIT_FAIL;
      }
    }
    else {
      spdlog::debug("[vscpl1drv-can4vscp] Enable of timestamp success.");
    }
  }

  // If non standard baudrate we change it here
  if (SET_BAUDRATE_115200 != m_nBaud) {

    spdlog::debug("[vscpl1drv-can4vscp] Sending CAN bitrate configuration command. Baudrate: {}", m_nBaud);

    uint8_t conf = m_nBaud;
    if (!sendConfigWait(VSCP_DRIVER_CONFIG_BAUDRATE, &conf, 1, &Msg, 500)) {
      // Failure
      spdlog::error("[vscpl1drv-can4vscp] Config of CAN bitrate failed.");
      if (m_bStrict) {
        close();
        spdlog::debug("[vscpl1drv-can4vscp] Strict mode: Closing device due to CAN bitrate configuration failure.");
        return CANAL_ERROR_INIT_FAIL;
      }
    }
    else {
      spdlog::debug("[vscpl1drv-can4vscp] Config of CAN bitrate success.");
    }
  }

  // Wait for ACK
  if (m_initFlag & CAN4VSCP_FLAG_ENABLE_WAIT_FOR_ACK) {}

  spdlog::debug("[vscpl1drv-can4vscp] Waiting for ACK if enabled.");

  // Set interface in requested mode.
  switch (m_initFlag & 0x03) {

    case 1:
      spdlog::debug("[vscpl1drv-can4vscp] Setting interface to listen mode.");
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_LISTEN, NULL, 0, &Msg, 1000)) {
        // Failure
        spdlog::error("[vscpl1drv-can4vscp] Failed to open device in listen mode");
        if (m_bStrict) {
          close();
          spdlog::debug("[vscpl1drv-can4vscp] Strict mode: Closing device due to listen mode open failure.");
          return CANAL_ERROR_INIT_FAIL;
        }
      }
      else {
        spdlog::debug("[vscpl1drv-can4vscp] Open listen mode success.");
      }
      break;

    case 2:
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_LOOPBACK, NULL, 0, &Msg, 1000)) {
        // Failure
        spdlog::error("[vscpl1drv-can4vscp] Failed to open device in loopback mode.");
        if (m_bStrict) {
          close();
          spdlog::debug("[vscpl1drv-can4vscp] Strict mode: Closing device due to loopback mode open failure.");
          return CANAL_ERROR_INIT_FAIL;
        }
      }
      else {

        spdlog::debug("[vscpl1drv-can4vscp] Open loopback mode success.");
      }
      break;

    case 0:
    default:
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_OPEN, NULL, 0, &Msg, 1000)) {
        // Failure
        spdlog::error("[vscpl1drv-can4vscp] Failed to open device in standard mode.");
        if (m_bStrict) {
          close();
          spdlog::debug("[vscpl1drv-can4vscp] Strict mode: Closing device due to standard mode open failure.");
          return CANAL_ERROR_INIT_FAIL;
        }
      }
      else {
        spdlog::debug("[vscpl1drv-can4vscp] Open standard mode success.");
      }
      break;
  }

  spdlog::debug("[vscpl1drv-can4vscp] Open procedure completed.");
  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// close
//

int
CCan4VSCPObj::close(void)
{
  cmdResponseMsg Msg;

  spdlog::debug("[vscpl1drv-can4vscp] Closing driver");

  // Do nothing if already terminated
  if (!m_bRun) {
    spdlog::warn("[vscpl1drv-can4vscp] Driver already terminated.");
    return CANAL_ERROR_SUCCESS;
  }

  spdlog::debug("[vscpl1drv-can4vscp] Sending close command to the device.");
  sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_CLOSE, NULL, 0, &Msg, 1000);

  m_bRun  = false;
  m_bOpen = false;

  // Wake the worker threads so they can detect m_bRun == false
#ifdef WIN32
  SetEvent(m_receiveDataEvent);
  SetEvent(m_transmitDataPutEvent);
  SetEvent(m_transmitDataGetEvent);
  SetEvent(m_transmitAckNackEvent);
#else
  semaphorePost(&m_receiveDataSem);
  semaphorePost(&m_transmitDataPutSem);
  semaphorePost(&m_transmitDataGetSem);
  semaphorePost(&m_transmitAckNackSem);

  spdlog::debug("[vscpl1drv-can4vscp] Closing driver: Semaphores released.");
#endif

  // Wait for the worker threads to terminate before any resource
  // they use is released
  spdlog::debug("[vscpl1drv-can4vscp] Closing driver: Waiting for threads to terminate.");
#ifdef WIN32
  DWORD rv;

  // Wait for transmit thread to terminate
  while (true) {
    GetExitCodeThread(m_hTreadTransmit, &rv);
    if (STILL_ACTIVE != rv)
      break;
    SLEEP(1);
  }

  // Wait for receive thread to terminate
  while (true) {
    GetExitCodeThread(m_hTreadReceive, &rv);
    if (STILL_ACTIVE != rv)
      break;
    SLEEP(1);
  }

#else
  pthread_join(m_threadIdReceive, NULL);
  pthread_join(m_threadIdTransmit, NULL);
  spdlog::debug("[vscpl1drv-can4vscp] Closing driver: Threads joined.");
#endif

  // Close the com port if its open
  if (m_com.isOpen()) {
    spdlog::debug("[vscpl1drv-can4vscp] Closing driver: Closing channel.");
    m_com.close();
    spdlog::debug("[vscpl1drv-can4vscp] Closing driver: Channel closed.");
  }

  // Mutexes/semaphores are destroyed in the destructor so the
  // channel can be reopened after close
  spdlog::debug("[vscpl1drv-can4vscp] Driver close success.");

  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// softOpen
//

int
CCan4VSCPObj::softOpen()
{
  cmdResponseMsg Msg;

  // If timestamp needs to be enabled we enabled it here
  if (m_initFlag & CAN4VSCP_FLAG_ENABLE_TIMESTAMP) {

    uint8_t conf = 1;
    if (!sendConfigWait(VSCP_DRIVER_CONFIG_TIMESTAMP, &conf, 1, &Msg, 500)) {
      // Failure
      // We don't close the serial channel but
      // instead try again later
      return CANAL_ERROR_INIT_FAIL;
    }
  }

  // If non standard baudrate we change it here
  if (SET_BAUDRATE_115200 != m_nBaud) {

    uint8_t conf = m_nBaud;
    if (!sendConfigWait(VSCP_DRIVER_CONFIG_BAUDRATE, &conf, 1, &Msg, 500)) {
      // Failure
      // We don't close the serial channel but
      // instead try again later
      return CANAL_ERROR_INIT_FAIL;
    }
  }

  // Wait for ACK
  if (m_initFlag & CAN4VSCP_FLAG_ENABLE_WAIT_FOR_ACK) {}

  // Set interface in requested mode.
  switch (m_initFlag & 0x03) {

    case 1:
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_LISTEN, NULL, 0, &Msg, 1000)) {
        // Failure
        // We don't close the serial channel but
        // instead try again later
        return CANAL_ERROR_INIT_FAIL;
      }
      break;

    case 2:
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_LOOPBACK, NULL, 0, &Msg, 1000)) {
        // Failure
        // We don't close the serial channel but
        // instead try again later
        return CANAL_ERROR_INIT_FAIL;
      }
      break;

    case 0:
    default:
      if (!sendCommandWait(VSCP_CAN4VSCP_DRIVER_COMMAND_OPEN, NULL, 0, &Msg, 1000)) {
        // Failure
        // We don't close the serial channel but
        // instead try again later
        return CANAL_ERROR_INIT_FAIL;
      }
      break;
  }

  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// OpenSerialInterface
//

int
CCan4VSCPObj::OpenSerialInterface(void)
{
  spdlog::debug("[vscpl1drv-can4vscp] Entering OpenSerialInterface.");


#ifdef WIN32
  int nComPort = 1; // COM1 is default
  char szDrvParams[MAX_PATH];
  DWORD baud = 115200;
#else
  char szDrvParams[PATH_MAX];
  strncpy(szDrvParams, "/dev/ttyUSB0", PATH_MAX);
  char szBaud[PATH_MAX];
  strcpy(szBaud, "115200");
#endif

#ifdef WIN32
  switch (m_nBaud) {

    case SET_BAUDRATE_128000:
      baud = 128000;
      break;

    case SET_BAUDRATE_230400:
      baud = 230400;
      break;

    case SET_BAUDRATE_256000:
      baud = 256000;
      break;

    case SET_BAUDRATE_460800:
      baud = 460800;
      break;

    case SET_BAUDRATE_500000:
      baud = 500000;
      break;

    case SET_BAUDRATE_625000:
      baud = 625000;
      break;

    case SET_BAUDRATE_921600:
      baud = 921600;
      break;

    case SET_BAUDRATE_1000000:
      baud = 1000000;
      break;

    case SET_BAUDRATE_9600:
      baud = 9600;
      break;

    case SET_BAUDRATE_19200:
      baud = 19200;
      break;

    case SET_BAUDRATE_38400:
      baud = 38400;
      break;

    case SET_BAUDRATE_57600:
      baud = 57600;
      break;

    case SET_BAUDRATE_115200:
    default:
      baud = 115200;
      break;
  }
#else
  switch (m_nBaud) {

    case SET_BAUDRATE_128000:
      strcpy(szBaud, "128000");
      break;

    case SET_BAUDRATE_230400:
      strcpy(szBaud, "230400");
      break;

    case SET_BAUDRATE_256000:
      strcpy(szBaud, "256000");
      break;

    case SET_BAUDRATE_460800:
      strcpy(szBaud, "460800");
      break;

    case SET_BAUDRATE_500000:
      strcpy(szBaud, "500000");
      break;

    case SET_BAUDRATE_625000:
      strcpy(szBaud, "625000");
      break;

    case SET_BAUDRATE_921600:
      strcpy(szBaud, "921600");
      break;

    case SET_BAUDRATE_1000000:
      strcpy(szBaud, "1000000");
      break;

    case SET_BAUDRATE_9600:
      strcpy(szBaud, "9600");
      break;

    case SET_BAUDRATE_19200:
      strcpy(szBaud, "19200");
      break;

    case SET_BAUDRATE_38400:
      strcpy(szBaud, "38400");
      break;

    case SET_BAUDRATE_57600:
      strcpy(szBaud, "57600");
      break;

    case SET_BAUDRATE_115200:
    default:
      strcpy(szBaud, "115200");
      break;
  }
#endif

    // Open the com port
#ifdef WIN32
  if (!m_com.init(nComPort,
                  CBR_115200,
                  8,
                  NOPARITY,
                  ONESTOPBIT,
                  (m_initFlag & CAN4VSCP_FLAG_ENABLE_HARDWARE_HANDSHAKE) ? HANDSHAKE_HARDWARE : HANDSHAKE_NONE)) {
    spdlog::error("[vscpl1drv-can4vscp] Initializing COM port {} with baud rate {}", nComPort, CBR_115200);
    return CANAL_ERROR_INIT_FAIL;
  }
#else

  // if open we have noting to do
  if (0 != m_com.getFD()) {
    spdlog::warn("[vscpl1drv-can4vscp] Serial port is already open. Aborting! ");
    return CANAL_ERROR_SUCCESS;
  }

  //----------------------------------------------------------------------
  // Open Serial Port
  //----------------------------------------------------------------------
  if (!m_com.open(szDrvParams)) {
    spdlog::error("[vscpl1drv-can4vscp] Open [{}] failed: {}", szDrvParams, strerror(errno));
    return CANAL_ERROR_INIT_FAIL;
  }

  spdlog::debug("[vscpl1drv-can4vscp] Open of port [{}] successful", szDrvParams);

  //----------------------------------------------------------------------
  // Com::setParam( char *baud, char *parity, char *bits, int HWFlow, int SWFlow
  // )
  //----------------------------------------------------------------------
  m_com.setParam((char *) "115200",
                 (char *) "N",
                 (char *) "8",
                 (m_initFlag & CAN4VSCP_FLAG_ENABLE_HARDWARE_HANDSHAKE) ? 1 : 0,
                 0);

#endif // Windows/Linux

  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// doFilter
//

bool
CCan4VSCPObj::doFilter(canalMsg *pcanalMsg)
{
  unsigned long msgid = (pcanalMsg->id & 0x1fffffff);
  if (!m_mask)
    return true; // fast escape

  // Set bit 32 if extended message
  if (pcanalMsg->flags | CANAL_IDFLAG_EXTENDED) {
    msgid &= 0x1fffffff;
    msgid |= 80000000;
  }
  else {
    // Standard message
    msgid &= 0x000007ff;
  }

  // Set bit 31 if RTR
  if (pcanalMsg->flags | CANAL_IDFLAG_RTR) {
    msgid |= 40000000;
  }

  return !((m_filter ^ msgid) & m_mask);
}

//////////////////////////////////////////////////////////////////////
// setFilter
//

int
CCan4VSCPObj::setFilter(unsigned long filter)
{
  m_filter = filter;
  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// setMask
//

int
CCan4VSCPObj::setMask(unsigned long mask)
{
  m_mask = mask;
  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// writeMsg
//

int
CCan4VSCPObj::writeMsg(canalMsg *pMsg)
{
  int rv = CANAL_ERROR_SUCCESS;

  // Must be a message pointer
  if (NULL == pMsg) {
    return CANAL_ERROR_PARAMETER;
  }

  // Must be open
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  // Must be room for the message
  if (m_transmitList.nCount > CAN4VSCP_MAX_SNDMSG) {
    return CANAL_ERROR_FIFO_FULL;
  }

  dllnode *pNode = new dllnode;
  if (NULL == pNode) {
    return CANAL_ERROR_MEMORY;
  }

  canalMsg *pcanalMsg = new canalMsg;
  if (NULL == pcanalMsg) {
    delete pNode;
    return CANAL_ERROR_MEMORY;
  }

  pNode->pObject = pcanalMsg;
  pNode->pKey    = NULL;
  pNode->pstrKey = NULL;

  pNode->pObject = pcanalMsg;
  pNode->pKey    = NULL;
  pNode->pstrKey = NULL;

  memcpy(pcanalMsg, pMsg, sizeof(canalMsg));

  LOCK_MUTEX(m_transmitMutex);
  dll_addNode(&m_transmitList, pNode);
  UNLOCK_MUTEX(m_transmitMutex);

#ifdef WIN32
  SetEvent(m_transmitDataGetEvent);
#else
  semaphorePost(&m_transmitDataGetSem);
#endif

  return rv;
}

//////////////////////////////////////////////////////////////////////
// writeMsg blocking
//

int
CCan4VSCPObj::writeMsgBlocking(canalMsg *pMsg, uint32_t Timeout)
{
#ifdef WIN32
  DWORD res;
#else
  int res;
#endif

  // Must be a message pointer
  if (NULL == pMsg)
    return CANAL_ERROR_PARAMETER;

  // Must be open
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  if (dll_getNodeCount(&m_transmitList) > CAN4VSCP_MAX_SNDMSG) {

#ifdef WIN32
    ResetEvent(m_transmitDataPutEvent);

    res = WaitForSingleObject(m_transmitDataPutEvent, Timeout);

    if (res == WAIT_TIMEOUT) {
      return CANAL_ERROR_TIMEOUT;
    }
    else if (res == WAIT_ABANDONED) {
      return CANAL_ERROR_GENERIC;
    }
#else
    res = semaphoreTimedWait(&m_transmitDataPutSem, Timeout);
    if (0 != res) {
      return (EAGAIN == errno) ? CANAL_ERROR_TIMEOUT : CANAL_ERROR_GENERIC;
    }
#endif
  }

  dllnode *pNode = new dllnode;
  if (NULL == pNode) {
    return CANAL_ERROR_MEMORY;
  }

  canalMsg *pcanalMsg = new canalMsg;
  if (NULL == pcanalMsg) {
    delete pNode;
    return CANAL_ERROR_MEMORY;
  }

  pNode->pObject = pcanalMsg;
  pNode->pKey    = NULL;
  pNode->pstrKey = NULL;

  memcpy(pcanalMsg, pMsg, sizeof(canalMsg));

  LOCK_MUTEX(m_transmitMutex);
  dll_addNode(&m_transmitList, pNode);
  UNLOCK_MUTEX(m_transmitMutex);

#ifdef WIN32
  SetEvent(m_transmitDataGetEvent);
#else
  semaphorePost(&m_transmitDataGetSem);
#endif
  return CANAL_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// readMsg
//

int
CCan4VSCPObj::readMsg(canalMsg *pMsg)
{
  int rv = CANAL_ERROR_SUCCESS;

  // Must be a message pointer
  if (NULL == pMsg) {
    return CANAL_ERROR_PARAMETER;
  }

  // Must be open
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  LOCK_MUTEX(m_receiveMutex);
  if ((0 == m_receiveList.nCount) || (NULL == m_receiveList.pHead) || (NULL == m_receiveList.pHead->pObject)) {
    UNLOCK_MUTEX(m_receiveMutex);
    return CANAL_ERROR_FIFO_EMPTY;
  }

  memcpy(pMsg, m_receiveList.pHead->pObject, sizeof(canalMsg));
  dll_removeNode(&m_receiveList, m_receiveList.pHead);
  if (m_receiveList.nCount == 0) {
#ifdef WIN32
    ResetEvent(m_receiveDataEvent);
#else
    // Drain stale wakeup tokens now that the queue is empty
    while (0 == semaphoreTimedWait(&m_receiveDataSem, 0)) {
      ;
    }
#endif
  }
  UNLOCK_MUTEX(m_receiveMutex);

  return rv;
}

//////////////////////////////////////////////////////////////////////
// readMsgBlocking
//

int
CCan4VSCPObj::readMsgBlocking(canalMsg *pMsg, uint32_t timeout)
{
  int rv = CANAL_ERROR_SUCCESS;
#ifdef WIN32
  DWORD res;
#else
  int res;
#endif

  // Must be a message pointer
  if (NULL == pMsg) {
    return CANAL_ERROR_PARAMETER;
  }

  // Must be open
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  // Yes we block if in queue is empty
  if (0 == m_receiveList.nCount) {
#ifdef WIN32
    res = WaitForSingleObject(m_receiveDataEvent, timeout);

    if (res == WAIT_TIMEOUT) {
      return CANAL_ERROR_TIMEOUT;
    }
    else if (res == WAIT_ABANDONED) {
      return CANAL_ERROR_GENERIC;
    }
#else
    res = semaphoreTimedWait(&m_receiveDataSem, timeout);
    if ((0 != res) && (EAGAIN == errno)) {
      return CANAL_ERROR_TIMEOUT;
    }
#endif
  }

  LOCK_MUTEX(m_receiveMutex);
  if ((m_receiveList.nCount > 0) && (NULL != m_receiveList.pHead) && (NULL != m_receiveList.pHead->pObject)) {
    memcpy(pMsg, m_receiveList.pHead->pObject, sizeof(canalMsg));
    dll_removeNode(&m_receiveList, m_receiveList.pHead);
    if (0 == m_receiveList.nCount) {
#ifdef WIN32
      ResetEvent(m_receiveDataEvent);
#else
      // Drain stale wakeup tokens now that the queue is empty
      while (0 == semaphoreTimedWait(&m_receiveDataSem, 0)) {
        ;
      }
#endif
    }
    UNLOCK_MUTEX(m_receiveMutex);
  }
  else {
    UNLOCK_MUTEX(m_receiveMutex);
    return CANAL_ERROR_FIFO_EMPTY;
  }

  return rv;
}

///////////////////////////////////////////////////////////////////////////////
// dataAvailable
//

int
CCan4VSCPObj::dataAvailable(void)
{
  if (!m_bOpen) {
    return 0;
  }

  return m_receiveList.nCount;
}

///////////////////////////////////////////////////////////////////////////////
//	getStatistics
//

int
CCan4VSCPObj::getStatistics(PCANALSTATISTICS pCanalStatistics)
{
  // Must be a valid pointer
  if (NULL == pCanalStatistics) {
    return CANAL_ERROR_PARAMETER;
  }

  memcpy(pCanalStatistics, &m_stat, sizeof(canalStatistics));

  return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//	getStatus
//

int
CCan4VSCPObj::getStatus(PCANALSTATUS pCanalStatus)
{
  // Must be a message pointer
  if (NULL == pCanalStatus) {
    return CANAL_ERROR_PARAMETER;
  }

  // Must be open
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// getDeviceCapabilities
//

bool
CCan4VSCPObj::getDeviceCapabilities(void)
{
  uint8_t sendData[512];

  // Payload: Our capabilities
  const uint8_t payload[2] = { 1, 10 };
  const uint8_t saveseq    = m_sequencyno; // Sequency used for this frame
  const uint16_t len =
    can4vscp_buildFrame(sendData, VSCP_SERIAL_DRIVER_FRAME_TYPE_CAPS_REQUEST, 0, m_sequencyno++, 2, payload, 2);

  // Empty reply list
  LOCK_MUTEX(m_responseMutex);
  dll_removeAllNodes(&m_responseList);
  UNLOCK_MUTEX(m_responseMutex);

  if (!sendMsg(sendData, len))
    return FALSE;

  // Wait for reply

  cmdResponseMsg msgResponse;
  uint32_t start = getClockMilliSeconds();

  while (getClockMilliSeconds() < (start + 500)) {

    bool bResponse = false;

    LOCK_MUTEX(m_responseMutex);
    if ((NULL != m_responseList.pHead) && (NULL != m_responseList.pHead->pObject)) {
      memcpy(&msgResponse, m_responseList.pHead->pObject, sizeof(cmdResponseMsg));
      dll_removeNode(&m_responseList, m_responseList.pHead);
      bResponse = true;
    }
    UNLOCK_MUTEX(m_responseMutex);

    if (bResponse) {

      if ((VSCP_SERIAL_DRIVER_CAPS_SIZE == msgResponse.sizePayload) && (saveseq == msgResponse.seq) &&
          (VSCP_SERIAL_DRIVER_FRAME_TYPE_CAPS_RESPONSE == msgResponse.op)) {

        m_caps.maxVscpFrames = (((uint16_t) msgResponse.payload[0]) << 8) + msgResponse.payload[1];
        if (!m_caps.maxVscpFrames)
          m_caps.maxVscpFrames = 1; // Zero not allowed

        m_caps.maxCanalFrames = (((uint16_t) msgResponse.payload[2]) << 8) + msgResponse.payload[3];
        if (!m_caps.maxCanalFrames)
          m_caps.maxCanalFrames = 1; // Zero not allowed
      }
    }

    SLEEP(1);
  }

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// sendMsg
//

bool
CCan4VSCPObj::sendMsg(uint8_t *buffer, short size)
{
  // uint8_t flags = 0;
  bool rv;

  if (m_com.isOpen()) {

    LOCK_MUTEX(m_can4vscpMutex);
#ifdef WIN32
    if (m_com.writebuf(buffer, size)) {
      rv = true;
    }
    else {
      // Put message back in ququq

      rv = false;
    }
#else
    if (m_com.comm_puts((char *) buffer, size)) {
      rv = true;
    }
    else {
      rv = false;
    }
#endif
    UNLOCK_MUTEX(m_can4vscpMutex);
  }
  else {
    rv = false;
  }

  return rv;
}

///////////////////////////////////////////////////////////////////////////////
// sendCommand
//
//

bool
CCan4VSCPObj::sendCommand(uint8_t cmdcode, uint8_t *pParam, uint8_t size)
{
  uint8_t sendData[512];
  uint8_t payload[256 + 1];
  uint16_t lenPayload = 0;

  // Command code + parameters
  payload[lenPayload++] = cmdcode;
  if (size) {
    memcpy(payload + lenPayload, pParam, size);
    lenPayload += size;
  }

  const uint16_t len = can4vscp_buildFrame(sendData,
                                           VSCP_SERIAL_DRIVER_FRAME_TYPE_COMMAND,
                                           0,
                                           m_sequencyno++,
                                           (uint16_t) ((size + 1) & 0xff),
                                           payload,
                                           lenPayload);

  // Empty reply list
  LOCK_MUTEX(m_responseMutex);
  dll_removeAllNodes(&m_responseList);
  UNLOCK_MUTEX(m_responseMutex);

  return sendMsg(sendData, len);
}

///////////////////////////////////////////////////////////////////////////////
// waitCommand4Response
//
//

bool
CCan4VSCPObj::wait4CommandResponse(cmdResponseMsg *pMsg, uint8_t cmdcode, uint8_t saveseq, uint32_t timeout)
{
  // uint32_t start = GetTickCount();
  uint32_t start = getClockMilliSeconds();

  while (getClockMilliSeconds() < (start + timeout)) {

    bool bResponse = false;

    LOCK_MUTEX(m_responseMutex);
    if ((NULL != m_responseList.pHead) && (NULL != m_responseList.pHead->pObject)) {
      memcpy(pMsg, m_responseList.pHead->pObject, sizeof(cmdResponseMsg));
      dll_removeNode(&m_responseList, m_responseList.pHead);
      bResponse = true;
    }
    UNLOCK_MUTEX(m_responseMutex);

    if (bResponse && (2 == pMsg->sizePayload) && (saveseq == pMsg->seq) &&
        (VSCP_SERIAL_DRIVER_FRAME_TYPE_COMMAND_REPLY == pMsg->op) && (0 == pMsg->payload[0]) &&
        (cmdcode == pMsg->payload[1])) {
      return true;
    }

    SLEEP(10);
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////
// sendCommandWait
//

bool
CCan4VSCPObj::sendCommandWait(uint8_t cmdcode, uint8_t *pParam, uint8_t size, cmdResponseMsg *pMsg, uint32_t timeout)
{
  // Save sequence number
  uint8_t saveseq = m_sequencyno;

  // Send the command
  if (!sendCommand(cmdcode, pParam, size))
    return false;

  return wait4CommandResponse(pMsg, cmdcode, saveseq, timeout);
}

///////////////////////////////////////////////////////////////////////////////
// sendConfig
//
//

bool
CCan4VSCPObj::sendConfig(uint8_t codeConfig, uint8_t *pParam, uint8_t size)
{
  uint8_t sendData[512];
  uint8_t payload[256 + 1];
  uint16_t lenPayload = 0;

  // Config code + parameters
  payload[lenPayload++] = codeConfig;
  if (size) {
    memcpy(payload + lenPayload, pParam, size);
    lenPayload += size;
  }

  const uint16_t len = can4vscp_buildFrame(sendData,
                                           VSCP_SERIAL_DRIVER_FRAME_TYPE_CONFIGURE,
                                           0,
                                           m_sequencyno++,
                                           (uint16_t) (size & 0xff),
                                           payload,
                                           lenPayload);

  // Empty reply list
  LOCK_MUTEX(m_responseMutex);
  dll_removeAllNodes(&m_responseList);
  UNLOCK_MUTEX(m_responseMutex);

  return sendMsg(sendData, len);
}

///////////////////////////////////////////////////////////////////////////////
// wait4ConfigResponse
//
//

bool
CCan4VSCPObj::wait4ConfigResponse(cmdResponseMsg *pMsg, uint8_t codeConfig, uint8_t saveseq, uint32_t timeout)
{
  // uint32_t start = GetTickCount();
  uint32_t start = getClockMilliSeconds();

  while (getClockMilliSeconds() < (start + timeout)) {

    bool bResponse = false;

    LOCK_MUTEX(m_responseMutex);
    if ((NULL != m_responseList.pHead) && (NULL != m_responseList.pHead->pObject)) {
      memcpy(pMsg, m_responseList.pHead->pObject, sizeof(cmdResponseMsg));
      dll_removeNode(&m_responseList, m_responseList.pHead);
      bResponse = true;
    }
    UNLOCK_MUTEX(m_responseMutex);

    if (bResponse) {

      if ((2 == pMsg->sizePayload) && (saveseq == pMsg->seq) && (VSCP_SERIAL_DRIVER_FRAME_TYPE_ACK == pMsg->op) &&
          (0 == pMsg->payload[0]) && (codeConfig == pMsg->payload[1])) {
        return true;
      }

      if ((2 == pMsg->sizePayload) && (saveseq == pMsg->seq) && (VSCP_SERIAL_DRIVER_FRAME_TYPE_NACK == pMsg->op) &&
          (0 == pMsg->payload[0]) && (codeConfig == pMsg->payload[1])) {
        return false;
      }
    }

    SLEEP(10);
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////
// sendConfigWait
//

bool
CCan4VSCPObj::sendConfigWait(uint8_t codeConfig, uint8_t *pParam, uint8_t size, cmdResponseMsg *pMsg, uint32_t timeout)
{
  // Save sequence number
  uint8_t saveseq = m_sequencyno;

  // Send the command
  if (!sendCommand(codeConfig, pParam, size))
    return false;

  return wait4CommandResponse(pMsg, codeConfig, saveseq, timeout);
}

///////////////////////////////////////////////////////////////////////////////
// checkCRC
//

bool
CCan4VSCPObj::checkCRC(void)
{ return (0 != can4vscp_checkCRC(m_bufferMsgRcv, m_lengthMsgRcv)); }

///////////////////////////////////////////////////////////////////////////////
// sendACK
//
//

void
CCan4VSCPObj::sendACK(uint8_t seq)
{
  uint8_t sendData[16];

  const uint16_t len = can4vscp_buildFrame(sendData, VSCP_SERIAL_DRIVER_FRAME_TYPE_ACK, 0, seq, 0, NULL, 0);

  // Send the event (sendMsg serializes port access)
  sendMsg(sendData, len);
}

///////////////////////////////////////////////////////////////////////////////
// sendNACK
//
//

void
CCan4VSCPObj::sendNACK(uint8_t seq)
{
  uint8_t sendData[16];

  const uint16_t len = can4vscp_buildFrame(sendData, VSCP_SERIAL_DRIVER_FRAME_TYPE_NACK, 0, seq, 0, NULL, 0);

  // Send the event (sendMsg serializes port access)
  sendMsg(sendData, len);
}

///////////////////////////////////////////////////////////////////////////////
// sendNoopFrame
//
//

void
CCan4VSCPObj::sendNoopFrame(void)
{
  uint8_t sendData[16];

  const uint16_t len = can4vscp_buildFrame(sendData, VSCP_SERIAL_DRIVER_FRAME_TYPE_NOOP, 0, m_sequencyno++, 0, NULL, 0);

  // Send the event (sendMsg serializes port access)
  sendMsg(sendData, len);
}

///////////////////////////////////////////////////////////////////////////////
// addToResponseQueue
//
//

bool
CCan4VSCPObj::addToResponseQueue(void)
{
  if (m_initFlag & CAN4VSCP_FLAG_ENABLE_WAIT_FOR_ACK) {

    LOCK_MUTEX(m_responseMutex);
    if (msgResponseInfo.bWaitingForAckNack &&
        (msgResponseInfo.channel == m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_CHANNEL]) &&
        (msgResponseInfo.seq == m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_SEQUENCY])) {

      if (VSCP_SERIAL_DRIVER_FRAME_TYPE_ACK == m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_TYPE]) {

        //  Positive things happens here
        msgResponseInfo.bAck               = true;
        msgResponseInfo.bWaitingForAckNack = false;
#ifdef WIN32
        SetEvent(m_transmitAckNackEvent);
#else
        semaphorePost(&m_transmitAckNackSem);
#endif
      }
      else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_NACK == m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_TYPE]) {

        //  Negative things happen to, sometimes
        msgResponseInfo.bAck               = false;
        msgResponseInfo.bWaitingForAckNack = false;
#ifdef WIN32
        SetEvent(m_transmitAckNackEvent);
#else
        semaphorePost(&m_transmitAckNackSem);
#endif
      }
    }
    UNLOCK_MUTEX(m_responseMutex);
  }

  cmdResponseMsg *pMsg = new cmdResponseMsg;
  if (NULL != pMsg) {

    dllnode *pNode = new dllnode;
    if (NULL != pNode) {

      pMsg->op          = m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_TYPE];
      pMsg->channel     = m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_CHANNEL];
      pMsg->seq         = m_bufferMsgRcv[VSCP_CAN4VSCP_DRIVER_POS_FRAME_SEQUENCY];
      pMsg->sizePayload = ((uint16_t) m_bufferMsgRcv[3] << 8) + m_bufferMsgRcv[4];

      if (pMsg->sizePayload) {
        memcpy(pMsg->payload, m_bufferMsgRcv + 5, (pMsg->sizePayload > 512) ? 512 : pMsg->sizePayload);
      }

      pNode->pObject = pMsg;
      LOCK_MUTEX(m_responseMutex);
      // Bound the queue - drop the oldest response if full
      if (m_responseList.nCount >= CAN4VSCP_MAX_RESPONSEMSG) {
        dll_removeNode(&m_responseList, m_responseList.pHead);
      }
      dll_addNode(&m_responseList, pNode);
      UNLOCK_MUTEX(m_responseMutex);
    }
    else {
      delete pMsg;
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// serialData2StateMachine
//
//

bool
CCan4VSCPObj::serialData2StateMachine(void)
{
  uint8_t c; // Serial character
  int cnt = 0;

  // Read RS-232 data
  c = m_com.readChar(&cnt);
  if (cnt > 0) {
    spdlog::trace("cnt={:02X}", c);
  }
  else if (cnt < 0) {

    if (errno == EAGAIN) {
      // No data available, non-blocking mode
      return false;
    }

    // errno is set to EAGAIN if no data is available, otherwise it indicates an error
    spdlog::error("Read char error [{}] errno[{}]", c, errno);

    switch (errno) {

      case EINTR: // interrupted by signal, just retry
        break;

      case EIO: // device likely disconnected or faulted
        spdlog::error("Serial I/O error — device may have disconnected\n");
        // consider closing fd and attempting reopen
        break;

      case EPIPE:
        spdlog::error("USB endpoint stall (ch341/USB-serial driver) — "
                      "attempting device reset/reopen\n");
        // typically: close fd, maybe trigger a USB reset via sysfs
        // or unbind/rebind, then reopen the port
        break;

      case EBADF:
        spdlog::error("Bad file descriptor — was it closed?\n");
        break;

      default:
        spdlog::error("Unexpected read error: {}", strerror(errno));
        break;
    }

    return false;
  }

  // Linux sets cnt ==-1 if no data, can be error code also
  while (cnt > 0) {

    switch (m_RxMsgState) {

        // We are virgin - no package start received yet
      case INCOMING_STATE_NONE:

        if ((INCOMING_SUBSTATE_NONE == m_RxMsgSubState) && (DLE == c)) {
          spdlog::trace("STATE_NONE/SUBSATE_DLE");
          m_RxMsgSubState = INCOMING_SUBSTATE_DLE;
        }
        else if ((INCOMING_SUBSTATE_DLE == m_RxMsgSubState) && (STX == c)) {
          spdlog::trace("STATE_STX/SUBSATE_NONE");
          m_RxMsgState    = INCOMING_STATE_STX;
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          m_lengthMsgRcv  = 0;
        }
        else {
          spdlog::trace("STATE_NONE/SUBSATE_NONE");
          m_lengthMsgRcv  = 0;
          m_RxMsgState    = INCOMING_STATE_NONE;
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
        }
        break;

        // We have received package start but not end
      case INCOMING_STATE_STX:

        if ((INCOMING_SUBSTATE_NONE == m_RxMsgSubState) && (DLE == c)) {
          spdlog::trace("STATE_STX/SUBSTATE_DLE");
          m_RxMsgSubState = INCOMING_SUBSTATE_DLE;
        }
        else if ((INCOMING_SUBSTATE_DLE == m_RxMsgSubState) && (STX == c)) {
          // This is strange as a DEL STX is not expected here
          // We try to sync up again...
          spdlog::trace("STATE_STX/SUBSTATE_NONE");
          m_RxMsgState    = INCOMING_STATE_STX;
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          m_lengthMsgRcv  = 0;
        }
        else if ((INCOMING_SUBSTATE_DLE == m_RxMsgSubState) && (ETX == c)) {

          // We have a packet
          spdlog::trace("STATE_NONE/SUBSTATE_NONE");
          m_RxMsgState    = INCOMING_STATE_NONE;
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          spdlog::trace(" ***FRAME*** \n");
          return true;
        }
        else if ((INCOMING_SUBSTATE_DLE == m_RxMsgSubState) && (DLE == c)) {
          // Byte stuffed DLE  i.e. DLE DLE == DLE
          spdlog::trace("STATE_STX/SUBSTATE_NONE");
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          if (m_lengthMsgRcv < sizeof(m_bufferMsgRcv)) {
            m_bufferMsgRcv[m_lengthMsgRcv++] = c;
          }
          else {
            // This packet has wrong format as it have
            // to many databytes - start all over!
            spdlog::trace("STATE_NONE/SUBSTATE_NONE");
            m_lengthMsgRcv  = 0;
            m_RxMsgState    = INCOMING_STATE_NONE;
            m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          }
        } // We come here if data is received
        else {
          spdlog::trace("STATE_STX/SUBSTATE_NONE");
          m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          if (m_lengthMsgRcv < sizeof(m_bufferMsgRcv)) {
            m_bufferMsgRcv[m_lengthMsgRcv++] = c;
          }
          else {
            // This packet has wrong format as it have
            // to many databytes - start over!
            m_lengthMsgRcv = 0;
            spdlog::trace("STATE_NONE/SUBSTATE_NONE");
            m_RxMsgState    = INCOMING_STATE_NONE;
            m_RxMsgSubState = INCOMING_SUBSTATE_NONE;
          }
        }
        break;

      case INCOMING_STATE_ETX:
        break;
    }

    // Read RS-232 data
    c = m_com.readChar(&cnt);
    if (cnt > 0) {
      spdlog::trace("Rcv={:02X}", c);
    }
    else if (cnt < 0) {

      if (errno == EAGAIN) {
        // No data available, non-blocking mode
        return false;
      }

      // errno is set to EAGAIN if no data is available, otherwise it indicates an error
      spdlog::error("Read char error [{}]", c);

      switch (errno) {

        case EINTR: // interrupted by signal, just retry
          break;

        case EIO: // device likely disconnected or faulted
          spdlog::error("Serial I/O error — device may have disconnected\n");
          // consider closing fd and attempting reopen
          break;

        case EPIPE:
          spdlog::error("USB endpoint stall (ch341/USB-serial driver) — "
                        "attempting device reset/reopen\n");
          // typically: close fd, maybe trigger a USB reset via sysfs
          // or unbind/rebind, then reopen the port
          break;

        case EBADF:
          spdlog::error("Bad file descriptor — was it closed?\n");
          break;

        default:
          spdlog::error("Unexpected read error: {}", strerror(errno));
          break;
      }

      return false;
    }

  } // while

  return false;
}

///////////////////////////////////////////////////////////////////////////////
// readSerialData
//
//

void
CCan4VSCPObj::readSerialData(void)
{

  if (serialData2StateMachine()) {

    spdlog::trace("OP Operation={} payload={}", m_bufferMsgRcv[0], m_bufferMsgRcv[3] * 256 + m_bufferMsgRcv[4]);
    for (int g = 0; g < (m_bufferMsgRcv[3] * 256 + m_bufferMsgRcv[4]); g++) {
      spdlog::trace("{:02X}", m_bufferMsgRcv[5 + g]);
    }

    spdlog::trace("CRC check");

    // Check CRC
    if (!checkCRC()) {
      spdlog::trace(" CRC Failed! Operation={} payload={}",
                    m_bufferMsgRcv[0],
                    m_bufferMsgRcv[3] * 256 + m_bufferMsgRcv[4]);
      for (int g = 0; g < (m_bufferMsgRcv[3] * 256 + m_bufferMsgRcv[4]); g++) {
        spdlog::trace("{:02X}", m_bufferMsgRcv[5 + g]);
      }
      return;
    }

    spdlog::trace("CRC check OK");

    // Check if NOOP frame
    if (VSCP_SERIAL_DRIVER_FRAME_TYPE_NOOP == (m_bufferMsgRcv[0])) {

      spdlog::trace("NOOP frame");

      m_activity = getClockMilliSeconds(); // activity
      sendACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]);
    }
    // Check for CANAL message frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_CANAL == (m_bufferMsgRcv[0])) {

      spdlog::trace("CANAL message frame");

      m_activity = getClockMilliSeconds(); // activity

      // CANAL message
      // -------------
      // [0]      -   DLE  - Not in buffer!!!!
      // [1]      -   STX  - Not in buffer!!!!
      // [2]      0   Frame type (2 - CANAL message.) - First in buffer
      // [3]      1   Channel (always zero)
      // [4]      2   Sequence number
      // [5/6]    3/4   Size of payload ( 12 + sizeData )
      // [7]      5   CAN id (MSB)
      // [8]      6   CAN id
      // [9]      7   CAN id
      // [10]     8   CAN id (LSB)
      // [11]     9   dlc
      // [12-n]   10  CAN data (0-8 bytes)
      // [len-3]  -   CRC
      // [len-2]  -   DLE
      // [len-1]  -   ETX

      long sizePayload  = ((uint16_t) m_bufferMsgRcv[3] << 8) + m_bufferMsgRcv[4];
      uint8_t *posFrame = m_bufferMsgRcv + 5;

      while (sizePayload > 0) {

        uint8_t dataLen = posFrame[4];

        // Guard: CAN data cannot exceed 8 bytes in standard CANAL frames
        if (dataLen > 8) {
          m_stat.cntOverruns++;
          break; // Corrupt payload structure inside frame, abandon multi-frame parse
        }

        uint16_t frameSize = 5 + dataLen;

        if (m_receiveList.nCount < CAN4VSCP_MAX_RCVMSG) {
          PCANALMSG pMsg = new canalMsg;

          if (NULL != pMsg) {

            pMsg->flags    = 0;
            dllnode *pNode = new dllnode;
            if (NULL != pNode) {

              pMsg->flags     = CANAL_IDFLAG_EXTENDED;
              pMsg->timestamp = getClockMicroSeconds();
              pMsg->obid      = 0;

              pMsg->id = (((uint32_t) m_bufferMsgRcv[5] << 24) & 0x1f000000) |
                         (((uint32_t) m_bufferMsgRcv[6] << 16) & 0x00ff0000) |
                         (((uint32_t) m_bufferMsgRcv[7] << 8) & 0x0000ff00) |
                         (((uint32_t) m_bufferMsgRcv[8]) & 0x000000ff);

              pMsg->sizeData = dataLen;
              if (pMsg->sizeData) {
                memcpy((void *) pMsg->data, (posFrame + 5), pMsg->sizeData);
              }

              if (doFilter(pMsg)) {
                pNode->pObject = pMsg;
                LOCK_MUTEX(m_receiveMutex);
                dll_addNode(&m_receiveList, pNode);
#ifdef WIN32
                SetEvent(m_receiveDataEvent);
#else
                semaphorePost(&m_receiveDataSem);
#endif
                UNLOCK_MUTEX(m_receiveMutex);

                m_stat.cntReceiveData += pMsg->sizeData;
                m_stat.cntReceiveFrames += 1;
              }
              else {
                delete pMsg;
                delete pNode;
              }
            } // No pNode
            else {
              delete pMsg;
            }
          } // No pMsg
        }
        // No room in receive queue
        else {
          // Fix: Log overrun BUT still advance pointers to prevent hanging
          m_stat.cntOverruns++;
        }

        // Always advance pointers regardless of queue capacity
        sizePayload -= frameSize;
        posFrame += frameSize;
      } // while (sizePayload > 0)
    }
    // Check for CANAL message frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_CANAL_TIMESTAMP == (m_bufferMsgRcv[0])) {

      spdlog::trace("Timestamped CANAL message frame");
      m_activity = getClockMilliSeconds(); // activity

      // CANAL message
      // -------------
      // [0]      -    DLE  - Not in buffer!!!!
      // [1]      -    STX  - Not in buffer!!!!
      // [2]      0    Frame type (2 - CANAL message.) - First in buffer
      // [3]      1    Channel (always zero)
      // [4]      2    Sequence number
      // [5/6]    3/4  Size of payload ( 12 + sizeData )
      // [7]      5    CAN id (MSB)
      // [8]      6    CAN id
      // [9]      7    CAN id
      // [10]     8    CAN id (LSB)
      // [11]     9    Timestamp (MSB)
      // [12]     10   Timestamp
      // [13]     11   Timestamp
      // [14]     12   Timestamp (LSB)
      // [15]     13   dlc
      // [16-n]   14   CAN data (0-8 bytes)
      // [len-3]  -    CRC
      // [len-2]  -    DLE
      // [len-1]  -    ETX

      if (m_receiveList.nCount < CAN4VSCP_MAX_RCVMSG) {

        PCANALMSG pMsg = new canalMsg;

        if (NULL != pMsg) {

          pMsg->flags    = 0;
          dllnode *pNode = new dllnode;
          if (NULL != pNode) {

            pMsg->flags = 0;
            pMsg->obid  = 0;

            pMsg->id = (((uint32_t) m_bufferMsgRcv[5] << 24) & 0x1f000000) |
                       (((uint32_t) m_bufferMsgRcv[6] << 16) & 0x00ff0000) |
                       (((uint32_t) m_bufferMsgRcv[7] << 8) & 0x0000ff00) |
                       (((uint32_t) m_bufferMsgRcv[8]) & 0x000000ff);

            pMsg->timestamp = (((uint32_t) m_bufferMsgRcv[9] << 24) & 0x1f000000) |
                              (((uint32_t) m_bufferMsgRcv[10] << 16) & 0x00ff0000) |
                              (((uint32_t) m_bufferMsgRcv[11] << 8) & 0x0000ff00) |
                              (((uint32_t) m_bufferMsgRcv[12]) & 0x000000ff);

            pMsg->sizeData = m_bufferMsgRcv[13];
            if (pMsg->sizeData > 8)
              pMsg->sizeData = 8; // Something is very wrong - Save the world

            if (pMsg->sizeData) {
              memcpy((void *) pMsg->data, (m_bufferMsgRcv + 14), pMsg->sizeData);
            }

            // Always extended so set extended flag
            pMsg->flags |= CANAL_IDFLAG_EXTENDED;

            if (doFilter(pMsg)) {
              pNode->pObject = pMsg;
              LOCK_MUTEX(m_receiveMutex);
              dll_addNode(&m_receiveList, pNode);
#ifdef WIN32
              SetEvent(m_receiveDataEvent); // Signal frame in queue
#else
              semaphorePost(&m_receiveDataSem); // Signal frame in queue
#endif
              UNLOCK_MUTEX(m_receiveMutex);

              // Update statistics
              m_stat.cntReceiveData += pMsg->sizeData;
              m_stat.cntReceiveFrames += 1;
            }
            else {
              // Message was filtered
              delete pMsg;
              delete pNode;
            }

          } // No pNode
          else {
            delete pMsg;
          }

        } // No pMsg
      }
      // No room in receive queue
      else {
        // Full buffer
        m_stat.cntOverruns++;
      }
    }
    // Check for VSCP event frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_VSCP_EVENT == (m_bufferMsgRcv[0])) {

      spdlog::trace("VSCP event frame");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't handle
                                                                       // VSCP events
    }
    // Check for VSCP event frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_VSCP_EVENT_TIMESTAMP == (m_bufferMsgRcv[0])) {

      spdlog::trace("VSCP event frame with timestamp");
#
      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't handle
                                                                       // VSCP events
    }
    // Check for configure frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_CONFIGURE == (m_bufferMsgRcv[0])) {

      spdlog::trace("Configure frame");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't
                                                                       // handle
                                                                       // configure
    }
    // Check for poll frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_POLL == (m_bufferMsgRcv[0])) {

      spdlog::trace("Poll frame");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't
                                                                       // handle
                                                                       // poll
    }
    // Check for no event frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_NO_EVENT == (m_bufferMsgRcv[0])) {

      spdlog::trace("No event frame");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't
                                                                       // handle no
                                                                       // event
    }
    // Check for multiframe VSCP message frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_MULTI_FRAME_VSCP == (m_bufferMsgRcv[0])) {

      spdlog::trace("Multiframe VSCP message frame");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't handle
                                                                       // vscp multi frame
    }
    // Check for multiframe VSCP message frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_MULTI_FRAME_VSCP_TIMESTAMP == (m_bufferMsgRcv[0])) {

      spdlog::trace("Multiframe VSCP message frame with timestamp");

      m_activity = getClockMilliSeconds();                             // activity
      sendNACK(m_bufferMsgRcv[VSCP_SERIAL_DRIVER_POS_FRAME_SEQUENCY]); // We don't handle
                                                                       // vscp multi frame
    }
    // Check for multiframe CANAL message frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_MULTI_FRAME_CANAL == (m_bufferMsgRcv[0])) {

      spdlog::trace("Multiframe CANAL message frame");

      m_activity = getClockMilliSeconds(); // activity

      // CANAL message
      // -------------
      // [0]      DLE  - Not in buffer!!!!
      // [1]      STX  - Not in buffer!!!!
      // [2]      Frame type (2 - CANAL message.) - First in buffer
      // [3]      Channel (always zero)
      // [4]      Sequence number
      // [5/6]    Size of payload ( 5 + sizeData ) * number of frames
      // ---------------------------------------------
      // [7]      CAN id (MSB)
      // [8]      CAN id
      // [9]      CAN id
      // [10]     CAN id (LSB)
      // [11]     dlc
      // [11-n]   CAN data (0-8 bytes)
      // ---------------------------------------------
      // [n+1]    Possible other CANAL frame
      // ---------------------------------------------
      // [len-3]  CRC
      // [len-2]  DLE
      // [len-1]  ETX

      // Payload size
      long sizePayload = ((uint16_t) m_bufferMsgRcv[3] << 8) + m_bufferMsgRcv[4];

      // Save pos for payload
      uint8_t *posFrame = m_bufferMsgRcv + 5;

      while (sizePayload > 0) {

        if (m_receiveList.nCount < CAN4VSCP_MAX_RCVMSG) {

          PCANALMSG pMsg = new canalMsg;
          if (NULL != pMsg) {

            pMsg->flags    = 0;
            dllnode *pNode = new dllnode;
            if (NULL != pNode) {

              pMsg->flags     = 0;
              pMsg->timestamp = getClockMicroSeconds();
              pMsg->obid      = 0;

              pMsg->id = (((uint32_t) posFrame[0] << 24) & 0x1f000000) | (((uint32_t) posFrame[1] << 16) & 0x00ff0000) |
                         (((uint32_t) posFrame[2] << 8) & 0x0000ff00) | (((uint32_t) posFrame[3]) & 0x000000ff);

              pMsg->sizeData = posFrame[4];
              if (pMsg->sizeData > 8)
                pMsg->sizeData = 8; // Something is very wrong - Save the world

              if (pMsg->sizeData) {
                memcpy((void *) pMsg->data, (posFrame + 5), pMsg->sizeData);
              }

              // Always extended so set extended flag
              pMsg->flags |= CANAL_IDFLAG_EXTENDED;

              if (doFilter(pMsg)) {
                pNode->pObject = pMsg;
                LOCK_MUTEX(m_receiveMutex);
                dll_addNode(&m_receiveList, pNode);
#ifdef WIN32
                SetEvent(m_receiveDataEvent); // Signal frame in queue
#else
                semaphorePost(&m_receiveDataSem); // Signal frame in queue
#endif
                UNLOCK_MUTEX(m_receiveMutex);

                // Update statistics
                m_stat.cntReceiveData += pMsg->sizeData;
                m_stat.cntReceiveFrames += 1;
              }
              else {
                // Message was filtered
                delete pMsg;
                delete pNode;
              }

              // Get ready for next payload frame
              sizePayload -= (5 + posFrame[4]);
              posFrame += (5 + posFrame[4]);

            } // No pNode
            else {
              delete pMsg;
            }

          } // No pMsg
        }
        // No room in receive queue
        else {

          spdlog::trace("Overrun");

          // Full buffer
          m_stat.cntOverruns++;

          // Get ready for next payload frame
          sizePayload -= (5 + posFrame[4]);
          posFrame += (5 + posFrame[4]);
        }
      } // while
    }
    // Check for multiframe CANAL message frame with timestamp
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_MULTI_FRAME_CANAL_TIMESTAMP == (m_bufferMsgRcv[0])) {

      spdlog::trace("Multiframe CANAL message frame with timestamp");
      m_activity = getClockMilliSeconds(); // activity

      // CANAL message structure:
      // [0]    - DLE  (Not in buffer)
      // [1]    - STX  (Not in buffer)
      // [2]    - Frame type (0x0C - Multi CANAL timestamped)
      // [3/4]  - Size of payload: (9 + sizeData) * number of frames
      // --- Payload frames ---
      // [0..3] - CAN id (4 bytes)
      // [4..7] - Timestamp (4 bytes)
      // [8]    - dlc (Data length 0-8)
      // [9..n] - CAN data (0-8 bytes)
      // ----------------------
      // [len-3]- CRC
      // [len-2]- DLE
      // [len-1]- ETX

      // Get total payload size (Big-Endian from buffer bytes 3 and 4)
      long sizePayload = ((uint16_t) m_bufferMsgRcv[3] << 8) + m_bufferMsgRcv[4];

      // Save position for start of payload
      uint8_t *posFrame = m_bufferMsgRcv + 5;

      while (sizePayload > 0) {
        uint8_t dataLen = posFrame[8];

        // Guard: CAN data length cannot exceed 8 bytes
        if (dataLen > 8) {
          m_stat.cntOverruns++;
          break; // Corrupt payload structure inside frame, abandon multi-frame parse
        }

        // Total byte length of this individual timestamped sub-frame in payload:
        // 4 bytes ID + 4 bytes Timestamp + 1 byte DLC + dataLen
        uint16_t frameSize = 9 + dataLen;

        if (m_receiveList.nCount < CAN4VSCP_MAX_RCVMSG) {

          PCANALMSG pMsg = new canalMsg;

          if (NULL != pMsg) {

            pMsg->flags    = 0;
            dllnode *pNode = new dllnode;

            if (NULL != pNode) {

              pMsg->flags = 0;
              pMsg->obid  = 0;

              // Extract 32-bit CAN ID
              pMsg->id = (((uint32_t) posFrame[0] << 24) & 0x1f000000) | (((uint32_t) posFrame[1] << 16) & 0x00ff0000) |
                         (((uint32_t) posFrame[2] << 8) & 0x0000ff00) | (((uint32_t) posFrame[3]) & 0x000000ff);

              // Extract 32-bit Timestamp
              pMsg->timestamp = (((uint32_t) posFrame[4] << 24) & 0x1f000000) |
                                (((uint32_t) posFrame[5] << 16) & 0x00ff0000) |
                                (((uint32_t) posFrame[6] << 8) & 0x0000ff00) | (((uint32_t) posFrame[7]) & 0x000000ff);

              pMsg->sizeData = dataLen;

              if (pMsg->sizeData) {
                memcpy((void *) pMsg->data, (posFrame + 9), pMsg->sizeData);
              }

              // Always extended so set extended flag
              pMsg->flags |= CANAL_IDFLAG_EXTENDED;

              if (doFilter(pMsg)) {
                pNode->pObject = pMsg;
                LOCK_MUTEX(m_receiveMutex);
                dll_addNode(&m_receiveList, pNode);
#ifdef WIN32
                SetEvent(m_receiveDataEvent); // Signal frame in queue
#else
                semaphorePost(&m_receiveDataSem); // Signal frame in queue
#endif
                UNLOCK_MUTEX(m_receiveMutex);

                // Update statistics
                m_stat.cntReceiveData += pMsg->sizeData;
                m_stat.cntReceiveFrames += 1;
              }
              else {
                // Message was filtered
                delete pMsg;
                delete pNode;
              }

            } // No pNode
            else {
              delete pMsg;
            }

          } // No pMsg
        }
        // Queue full (overflow condition)
        else {
          m_stat.cntOverruns++;
        }

        // Advance payload pointers REGARDLESS of whether the node was stored or dropped.
        // Prevents hanging in an infinite loop when m_receiveList is full!
        sizePayload -= frameSize;
        posFrame += frameSize;

      } // while (sizePayload > 0)
    }
    // Check for sent ACK frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_SENT_ACK == (m_bufferMsgRcv[0])) {

      spdlog::trace("Sent ACK frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for sent NACK frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_SENT_NACK == (m_bufferMsgRcv[0])) {

      spdlog::trace("Sent NACK frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for ACK frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_ACK == (m_bufferMsgRcv[0])) {

      spdlog::trace("ACK frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for NACK frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_NACK == (m_bufferMsgRcv[0])) {

      spdlog::trace("NACK frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for ERROR frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_ERROR == (m_bufferMsgRcv[0])) {

      spdlog::trace("ERROR frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for command reply frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_COMMAND_REPLY == (m_bufferMsgRcv[0])) {

      spdlog::trace("Command reply frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check for capabilities response frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_CAPS_RESPONSE == (m_bufferMsgRcv[0])) {

      spdlog::trace("Capabilities response frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }
    // Check if command frame
    else if (VSCP_SERIAL_DRIVER_FRAME_TYPE_COMMAND == (m_bufferMsgRcv[0])) {

      spdlog::trace("Command frame");

      m_activity = getClockMilliSeconds(); // activity
      addToResponseQueue();
    }

    m_RxMsgState    = INCOMING_STATE_NONE; // reset state for next msg
    m_RxMsgSubState = INCOMING_SUBSTATE_NONE;

  } // frame & crc
}

///////////////////////////////////////////////////////////////////////////////
// transmitMessage
//
//

static bool
transmitMessage(CCan4VSCPObj *pobj, uint8_t *pseq)
{
  canalMsg msg;

  // Check pointers
  if (NULL == pobj) {
    return false;
  }

  if (NULL == pseq) {
    return false;
  }

  // Must be open
  if (!pobj->m_bOpen) {
    return false;
  }

    spdlog::debug("[vscpl1drv-can4vscp] transmitMessage");
  

  // Must be a message to transmit - fetch a copy under lock
  LOCK_MUTEX(pobj->m_transmitMutex);
  if ((0 == pobj->m_transmitList.nCount) || (NULL == pobj->m_transmitList.pHead) ||
      (NULL == pobj->m_transmitList.pHead->pObject)) {
    UNLOCK_MUTEX(pobj->m_transmitMutex);
    return false;
  }

  memcpy(&msg, pobj->m_transmitList.pHead->pObject, sizeof(canalMsg));
  UNLOCK_MUTEX(pobj->m_transmitMutex);

  // CANAL message
  // -------------
  // [0]      DLE
  // [1]      STX
  // [2]      Frame type (2 - CANAL message.)
  // [3]      Channel (always zero)
  // [4]      Sequence number
  // [5/6]    Size of payload ( 12 + sizeData )
  // [7]      CAN id (MSB)
  // [8]      CAN id
  // [9]      CAN id
  // [10]     CAN id (LSB)
  // [11-n]   CAN data (0-8 bytes)
  // [len-3]  CRC
  // [len-2]  DLE
  // [len-1]  ETX

  uint8_t sendData[128]; // No Level II events in this driver
  uint8_t payload[4 + 8];
  uint16_t lenPayload = 0;

  // id
  payload[lenPayload++] = (msg.id >> 24) & 0xff;
  payload[lenPayload++] = (msg.id >> 16) & 0xff;
  payload[lenPayload++] = (msg.id >> 8) & 0xff;
  payload[lenPayload++] = msg.id & 0xff;

  // Data
  for (int i = 0; i < msg.sizeData; i++) {
    payload[lenPayload++] = msg.data[i];
  }

  const uint16_t len = can4vscp_buildFrame(sendData,
                                           VSCP_SERIAL_DRIVER_FRAME_TYPE_CANAL,
                                           0,
                                           (*pseq)++,
                                           (uint16_t) (4 + msg.sizeData),
                                           payload,
                                           lenPayload);

  // Arm the ACK/NACK mechanism before the frame goes out so the
  // response cannot be missed
#ifdef WIN32
  ResetEvent(pobj->m_transmitAckNackEvent);
#endif
  LOCK_MUTEX(pobj->m_responseMutex);
  pobj->msgResponseInfo.bAck               = false; // We start out being pessimistic
  pobj->msgResponseInfo.channel            = 0;
  pobj->msgResponseInfo.seq                = *pseq - 1; // Sequency for frame
  pobj->msgResponseInfo.bWaitingForAckNack = true;
  UNLOCK_MUTEX(pobj->m_responseMutex);

  // Send the event
  if (!pobj->sendMsg(sendData, len)) {
    LOCK_MUTEX(pobj->m_responseMutex);
    pobj->msgResponseInfo.bWaitingForAckNack = false;
    UNLOCK_MUTEX(pobj->m_responseMutex);
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// workThreadTransmit
//
//

#ifdef WIN32
void
workThreadTransmit(void *pObject)
#else
void *
workThreadTransmit(void *pObject)
#endif
{
#ifdef WIN32
  DWORD errorCode = 0;
#else
  int rv = 0;
  int res;
#endif

  bool bTransmissionInProgress = false; // True while waiting for ACK/NACK
  uint8_t seq                  = 0;     // Increased by one for every frame sent

  CCan4VSCPObj *pobj = (CCan4VSCPObj *) pObject;
  if (NULL == pobj) {
#ifdef WIN32
    ExitThread(errorCode); // Fail
#else
    pthread_exit(&rv);
#endif
  }

  // Init CRC table
  init_crc8();

  // Clear ACK/NACK response structure
  memset(&pobj->msgResponseInfo, 0, sizeof(pobj->msgResponseInfo));

  // --------------------------------------------------------------------------

  while (pobj->m_bRun) {

    // Are we in transmission
    if ((pobj->m_initFlag & CAN4VSCP_FLAG_ENABLE_WAIT_FOR_ACK) && bTransmissionInProgress) {
#ifdef WIN32
      if (WAIT_OBJECT_0 != WaitForSingleObject(pobj->m_transmitAckNackEvent, 500)) {
        // We did not get a ACK/NACK in time - resend frame
        transmitMessage(pobj, &seq);
        continue;
      }
#else
      res = semaphoreTimedWait(&pobj->m_transmitAckNackSem, 20);
      if (0 != res) {
        // We did not get a ACK/NACK in time - resend frame
        transmitMessage(pobj, &seq);
        continue;
      }
#endif

      bool bAck;
      LOCK_MUTEX(pobj->m_responseMutex);
      bAck = pobj->msgResponseInfo.bAck;
      UNLOCK_MUTEX(pobj->m_responseMutex);

      if (bAck) {

        // ACK - Message sent successfully

        canalMsg msg;

        LOCK_MUTEX(pobj->m_transmitMutex);
        if ((0 == pobj->m_transmitList.nCount) || (NULL == pobj->m_transmitList.pHead) ||
            (NULL == pobj->m_transmitList.pHead->pObject)) {
          // Should not happen but if it does anyway... ;-/
          UNLOCK_MUTEX(pobj->m_transmitMutex);
          bTransmissionInProgress = false;
          continue;
        }

        memcpy(&msg, pobj->m_transmitList.pHead->pObject, sizeof(canalMsg));

        // If ACK remove the event from the queue
        dll_removeNode(&pobj->m_transmitList, pobj->m_transmitList.pHead);
        UNLOCK_MUTEX(pobj->m_transmitMutex);

        // Update statistics
        pobj->m_stat.cntTransmitData += msg.sizeData;
        pobj->m_stat.cntTransmitFrames += 1;

        // Wake any writer blocked on a full transmit queue
#ifdef WIN32
        SetEvent(pobj->m_transmitDataPutEvent);
#else
        semaphorePost(&pobj->m_transmitDataPutSem);
#endif

        bTransmissionInProgress = false;
      }
      else {
        // NACK - Message send failed - Retransmit
        transmitMessage(pobj, &seq);
        continue;
      }

    } // Transmission in progress

    // If there is noting in the queue we wait until there is
    // something there
    if (0 == pobj->m_transmitList.nCount) {
#ifdef WIN32
      if (WAIT_OBJECT_0 != WaitForSingleObject(pobj->m_transmitDataGetEvent, 100)) {
        continue;
      }
#else
      res = semaphoreTimedWait(&pobj->m_transmitDataGetSem, 1);
      if (0 != res) {
        continue;
      }
#endif
    }

    // If there is something to transmit, well, then transmit it
    if (pobj->m_transmitList.nCount > 0) {

      if (transmitMessage(pobj, &seq)) {

        if (pobj->m_initFlag & CAN4VSCP_FLAG_ENABLE_WAIT_FOR_ACK) {
          // The message stays in the queue until ACK/NACK arrives
          bTransmissionInProgress = true;
        }
        else {

          canalMsg msg;

          LOCK_MUTEX(pobj->m_transmitMutex);
          if ((pobj->m_transmitList.nCount > 0) && (NULL != pobj->m_transmitList.pHead) &&
              (NULL != pobj->m_transmitList.pHead->pObject)) {

            memcpy(&msg, pobj->m_transmitList.pHead->pObject, sizeof(canalMsg));

            // Remove the event from the queue
            dll_removeNode(&pobj->m_transmitList, pobj->m_transmitList.pHead);
            UNLOCK_MUTEX(pobj->m_transmitMutex);

            // Update statistics
            pobj->m_stat.cntTransmitData += msg.sizeData;
            pobj->m_stat.cntTransmitFrames += 1;

            // Wake any writer blocked on a full transmit queue
#ifdef WIN32
            SetEvent(pobj->m_transmitDataPutEvent);
#else
            semaphorePost(&pobj->m_transmitDataPutSem);
#endif
          }
          else {
            UNLOCK_MUTEX(pobj->m_transmitMutex);
          }
        }
      }
    }
    else {

      // Check for i/f inactivity
      if (pobj->m_initFlag & CAN4VSCP_FLAG_ENABLE_REOPEN) {
        if (getClockMicroSeconds() - pobj->m_activity > SOFT_OPEN_TIMOUT) {
          // We have an inactivity problem
          pobj->softOpen();
          pobj->m_activity = getClockMilliSeconds(); // activity
        }
      }

      // No data to write
      SLEEP(10);
    }

  } // while

  // --------------------------------------------------------------------------

#ifdef WIN32
  ExitThread(errorCode);
#else
  rv = 0xaa;

    spdlog::debug("[vscpl1drv-can4vscp] TX thread terminating");
  
  pthread_exit(&rv);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// workThreadReceive
//
//

#ifdef WIN32
void
workThreadReceive(void *pObject)
#else
void *
workThreadReceive(void *pObject)
#endif
{
#ifdef WIN32
  DWORD errorCode = 0;
#else
  int rv = 0;
#endif

  CCan4VSCPObj *pobj = (CCan4VSCPObj *) pObject;
  if (NULL == pobj) {
#ifdef WIN32
    ExitThread(errorCode); // Fail
#else
    pthread_exit(&rv);
#endif
  }

  while (pobj->m_bRun) {

    // Serial writes are serialized inside sendMsg(). Holding
    // m_can4vscpMutex here would deadlock when readSerialData()
    // answers with sendACK()/sendNACK().
    pobj->readSerialData();

    SLEEP(10);

  } // while

#ifdef WIN32
  ExitThread(errorCode);
#else
  rv = 0x55;

    spdlog::debug("[vscpl1drv-can4vscp] RX thread terminating");
  
  pthread_exit(&rv);
#endif
}
