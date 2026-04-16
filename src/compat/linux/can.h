#ifndef VSCP_MACOS_LINUX_CAN_H
#define VSCP_MACOS_LINUX_CAN_H

#include <stdint.h>

typedef uint32_t canid_t;

#define CAN_EFF_FLAG 0x80000000U
#define CAN_RTR_FLAG 0x40000000U
#define CAN_ERR_FLAG 0x20000000U

#define CAN_SFF_MASK 0x000007FFU
#define CAN_EFF_MASK 0x1FFFFFFFU
#define CAN_ERR_MASK 0x1FFFFFFFU

#define CAN_MAX_DLEN 8

struct can_frame {
    canid_t can_id;
    uint8_t can_dlc;
    uint8_t __pad;
    uint8_t __res0;
    uint8_t __res1;
    uint8_t data[CAN_MAX_DLEN] __attribute__((aligned(8)));
};

#endif