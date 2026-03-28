/*
 * @file    cpcu_ipc.c
 * @brief   IPC implementation over SRAM4 with HSEM notification
 * @author  bugrASl
 *
 * Key design: Lock-free SPSC ring buffer.
 *   - M4 only writes sensor_head; M7 only writes sensor_tail.
 *   - SRAM4 is non-cacheable (MPU config) → both cores see writes immediately.
 *   - __DMB() (Data Memory Barrier) ensures write ordering: data is committed
 *     to SRAM4 before the index update becomes visible to the other core.
 *   - HSEM is used for notification only (take+release = doorbell interrupt),
 *     not as a mutex. Neither core ever blocks waiting for a lock.
 */

#include "cpcu_ipc.h"
#include <string.h>

/*============== Initialization (CM7 calls before releasing CM4) ===========================================*/
void IPC_Init(void)
{
    /* Zero the entire 64 KB SRAM4 region */
    memset((void *)IPC_SHM_BASE, 0, IPC_SHM_SIZE);

    /* Write control block identification */
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    ctrl->magic         = IPC_MAGIC;
    ctrl->version       = IPC_VERSION;
    ctrl->m7_ready      = 1;
    ctrl->m4_ready      = 0;
    ctrl->sensor_head   = 0;
    ctrl->sensor_tail   = 0;
    ctrl->motor_cmd_seq = 0;
    ctrl->motor_cmd_ack = 0;
    ctrl->system_state  = IPC_STATE_INIT;

    /* Ensure all writes are globally visible before CM4 can read */
    __DMB();
}

bool IPC_IsReady(void)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    return (ctrl->magic == IPC_MAGIC) && (ctrl->m7_ready == 1);
}

uint32_t IPC_SensorCount(void)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    return (ctrl->sensor_head - ctrl->sensor_tail) & IPC_SENSOR_RING_MASK;
}
/*==========================================================================================================*/

/*============== M4 API: Push sensor data, read motor commands =============================================*/

/*-------------- M4: Push decoded wireless packet into sensor ring buffer ----------------------------------*/
bool IPC_M4_PushSensor(const WL_Packet *pkt)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_SensorEntry  *ring = IPC_SENSOR_RING();
    volatile IPC_Diagnostics  *diag = IPC_DIAG();

    /* Check for space. Full when next_head == tail. */
    uint32_t next_head = (ctrl->sensor_head + 1) & IPC_SENSOR_RING_MASK;
    if (next_head == ctrl->sensor_tail) {
        /* Ring full — overwrite oldest by advancing tail.
         * In a real-time system, recent data is more valuable than old. */
        ctrl->sensor_tail = (ctrl->sensor_tail + 1) & IPC_SENSOR_RING_MASK;
        diag->m4_ring_overflows++;
    }

    /* Write the entry at current head position */
    uint32_t idx = ctrl->sensor_head & IPC_SENSOR_RING_MASK;
    volatile IPC_SensorEntry *entry = &ring[idx];

    /* Copy all sample sets */
    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++) {
        for (int c = 0; c < WL_NUM_CHANNELS; c++) {
            entry->samples[s].ch[c] = pkt->samples[s].ch[c];
        }
    }
    entry->seq       = pkt->seq;
    entry->flags     = pkt->flags;
    entry->timestamp = HAL_GetTick();

    /* Barrier: data must be in SRAM4 before head index is updated.
     * Without this, CM7 could see the new head and read incomplete data. */
    __DMB();

    /* Advance head — this single write makes the entry visible to CM7 */
    ctrl->sensor_head = (ctrl->sensor_head + 1) & IPC_SENSOR_RING_MASK;

    diag->m4_pkts_received++;

    /* Notify CM7: take+release HSEM generates HSEM1_IRQn on CM7.
     * If CM7 is in WFI (low-power wait), this wakes it up. */
    HAL_HSEM_FastTake(IPC_HSEM_SENSOR);
    HAL_HSEM_Release(IPC_HSEM_SENSOR, 0);

    return true;
}

/*-------------- M4: Read latest motor command from CM7 ----------------------------------------------------*/
bool IPC_M4_ReadMotorCommand(IPC_MotorCommand *cmd)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_MotorCommand *mcmd = IPC_MOTOR_CMD();

    /* Check if CM7 posted a new command (seq > ack) */
    uint32_t current_seq = ctrl->motor_cmd_seq;
    if (current_seq == ctrl->motor_cmd_ack) {
        return false;                               /* No new command */
    }

    /* Barrier: read data AFTER confirming seq changed */
    __DMB();

    memcpy(cmd, (const void *)mcmd, sizeof(IPC_MotorCommand));

    /* Acknowledge: tell CM7 we consumed this command */
    ctrl->motor_cmd_ack = current_seq;

    return true;
}

/*-------------- M7: Read one sensor entry from ring buffer ------------------------------------------------*/
bool IPC_M7_PopSensor(IPC_SensorEntry *entry)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_SensorEntry  *ring = IPC_SENSOR_RING();

    /* Empty check: head == tail means nothing to read */
    if (ctrl->sensor_head == ctrl->sensor_tail) {
        return false;
    }

    uint32_t idx = ctrl->sensor_tail & IPC_SENSOR_RING_MASK;
    memcpy(entry, (const void *)&ring[idx], sizeof(IPC_SensorEntry));

    /* Barrier: data must be fully read before advancing tail.
     * Otherwise M4 might overwrite this slot before we finish copying. */
    __DMB();

    ctrl->sensor_tail = (ctrl->sensor_tail + 1) & IPC_SENSOR_RING_MASK;

    return true;
}

/*-------------- M7: Batch-read all available sensor entries -----------------------------------------------*/
bool IPC_M7_PopSensorBatch(IPC_SensorEntry *entries, uint32_t *count)
{
    *count = 0;
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_SensorEntry  *ring = IPC_SENSOR_RING();

    while (ctrl->sensor_head != ctrl->sensor_tail) {
        uint32_t idx = ctrl->sensor_tail & IPC_SENSOR_RING_MASK;
        memcpy(&entries[*count], (const void *)&ring[idx], sizeof(IPC_SensorEntry));

        /* Barrier INSIDE the loop: data must be fully copied before
         * tail advances, otherwise M4 can overwrite the slot we just
         * read from. The old code had the DMB after the entire loop,
         * which allowed M4 to see intermediate tail advances and
         * reclaim slots still being copied. */
        __DMB();

        ctrl->sensor_tail = (ctrl->sensor_tail + 1) & IPC_SENSOR_RING_MASK;
        (*count)++;

        if (*count >= IPC_SENSOR_RING_SIZE) break;
    }

    if (*count > 0) {
        IPC_DIAG()->m7_batches_processed++;
    }

    return (*count > 0);
}

/*-------------- M7: Write new motor command for M4 --------------------------------------------------------*/
void IPC_M7_PushMotorCommand(const IPC_MotorCommand *cmd)
{
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_MotorCommand *mcmd = IPC_MOTOR_CMD();

    memcpy((void *)mcmd, cmd, sizeof(IPC_MotorCommand));

    /* Barrier: command data must be in SRAM4 before seq increments */
    __DMB();

    ctrl->motor_cmd_seq++;

    /* Notify CM4 via HSEM */
    HAL_HSEM_FastTake(IPC_HSEM_MOTOR);
    HAL_HSEM_Release(IPC_HSEM_MOTOR, 0);
}
