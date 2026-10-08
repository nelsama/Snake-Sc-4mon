/**
 * ============================================================================
 * joy.c - Driver del joystick Atari (DB9) via GPIO del FPGA
 * ============================================================================
 * Vease include/joy.h para el mapa de pines y las advertencias de config.
 * ============================================================================
 */

#include <stdint.h>
#include "joy.h"

#define JOY_PORT  (*(volatile uint8_t *)0xC000)   /* datos puerto 1  */
#define JOY_CFG   (*(volatile uint8_t *)0xC002)   /* config puerto 1 */

void joy_init(void) {
    /* Poner los bits 3-7 como ENTRADA preservando los 0-2 (TM1638).
     * READ-MODIFY-WRITE: nunca escribir un valor fijo, o se pisa el TM1638. */
    JOY_CFG = (uint8_t)((JOY_CFG & 0x07) | JOY_MASK);
}

uint8_t joy_read(void) {
    uint8_t raw = (uint8_t)(JOY_PORT & JOY_MASK);  /* ignora bits del TM1638 */
    uint8_t act = 0;

#if JOY_ACTIVE_LOW
    raw = (uint8_t)(~raw);      /* pulsado = 0 -> invertir a 1 = pulsado */
#endif

    if (raw & JOY_LEFT)  act |= JOY_A_LEFT;
    if (raw & JOY_RIGHT) act |= JOY_A_RIGHT;
    if (raw & JOY_UP)    act |= JOY_A_UP;
    if (raw & JOY_DOWN)  act |= JOY_A_DOWN;
    if (raw & JOY_FIRE)  act |= JOY_A_FIRE;
    return act;
}
