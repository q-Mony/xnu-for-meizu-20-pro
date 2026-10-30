// SPDX-License-Identifier: BSD-2-Clause-Patent
// RPMh command path of the application-processor RSC (SM8550): command DB lookup and the register
// sequence that carries one active-TCS write. The caller maps the DRV register block and the reserved
// command DB memory, so this file has no platform or IOKit dependency and is exercised on the host.
#ifndef NATIVE_RPMH_H
#define NATIVE_RPMH_H

#include <stddef.h>
#include <stdint.h>

// DT /reserved-memory/aop_cmd_db_region@81c60000, size 0x20000.
#define RPMH_CMD_DB_SIZE 0x20000u
// struct cmd_db_header: version, magic, eight rsc_hdr, checksum, reserved.
#define RPMH_MAX_SLAVES 8
// A request fits in one TCS command window; the regulator vote uses three.
#define RPMH_MAX_CMDS 3
// A VRM resource address carries its slave type in bits 19:16 (drivers/soc/qcom/cmd-db.c).
#define RPMH_REGULATOR_TYPE 4
// Command message id: length 8 (a write) and the response-requested bit (rpmh-rsc.c CMD_MSGID_*).
#define RPMH_MSGID_WRITE 0x10008u
// TCS control bits: AMC mode enable and trigger.
#define RPMH_TCS_ENABLE (1u << 16)
#define RPMH_TCS_TRIGGER (1u << 24)
// Every apps RSC in the sheng and Meizu DTs has qcom,tcs-offset = <0xd00>.
#define RPMH_TCS_OFFSET 0xd00u

// Layout of one DRV's TCS registers, from the RSC major version in DRV_ID (drv base + 0, bits 23:16).
// v2.7 and v3.0 differ in the command stride and in where the per-TCS control block sits.
struct rpmh_layout {
    uint32_t tcs_stride;          // distance between TCS blocks: 672 in both known versions
    uint32_t cmd_stride;          // per-command stride: 20 (v2.7) or 24 (v3.0)
    uint32_t irq_enable;          // DRV-wide, one bit per TCS
    uint32_t irq_status;
    uint32_t irq_clear;
    uint32_t wait_for_completion; // per-TCS
    uint32_t control;
    uint32_t status;
    uint32_t cmd_enable;
    uint32_t cmd_msgid;
    uint32_t cmd_addr;
    uint32_t cmd_data;
};

#define RPMH_EINVAL (-1)
#define RPMH_ENODB (-2)
#define RPMH_ENOENT (-3)
#define RPMH_EBADVERSION (-4)
#define RPMH_EBADADDR (-5)
#define RPMH_ETIMEOUT (-6)            // the vote was triggered; the TCS never reported completion

typedef void (*rpmh_delay_fn)(void *context, unsigned microseconds);

// Fills the register layout for an RSC version word. Only major 2 and 3 are accepted: anything else
// means the register block was not read correctly, and guessing would write into unknown offsets.
int rpmh_layout_for(uint32_t version, struct rpmh_layout *layout);

// Looks up a resource name in the command DB and returns its RPMh address.
int rpmh_address(const void *cmd_db, size_t size, const char *name, uint32_t *address);

// Votes a VRM regulator on: voltage (mV), on/off and mode 7 (LDO HPM), the three words the Linux
// rpmh-regulator driver writes to address + 0/4/8, then waits up to 200 ms for the TCS report.
// address receives the command DB address, irq_seen the last IRQ_STATUS word.
int rpmh_regulator_on(volatile uint32_t *rsc, uint32_t tcs_offset, const void *cmd_db, size_t cmd_db_size,
                      const char *name, unsigned millivolts, rpmh_delay_fn delay, void *context,
                      uint32_t *address, uint32_t *irq_seen);

#endif
