/**
 * ============================================================================
 * sound.h - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * El SID esta mapeado en $D400-$D41F, igual que en el Commodore 64.
 *
 * Mapa de registros por voz (offset 0, 7, 14):
 *   +0 FREQ_LO      +1 FREQ_HI      +2 PW_LO    +3 PW_HI
 *   +4 CTRL (gate/sync/ring/test/waveform)
 *   +5 ATTACK/DECAY (+6 SUSTAIN/RELEASE)
 * $D418 = MODE/VOL (bits 3:0 = volumen master)
 *
 * Voces asignadas:
 *   Voz 1: tic de paso (snd_move).
 *   Voz 2: comer manzana (blip) y choque/muerte (ruido).
 *   Voz 3: bono al subir de nivel. Ademas, su oscilador de ruido ($D41B) se usa
 *          como generador de numeros aleatorios (snd_random).
 * ============================================================================
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Inicializa el SID: silencia las 3 voces, volumen master al maximo. */
void snd_init(void);

/* Efectos. Todos son no bloqueantes: disparan la voz y suenan solos. */
void snd_move(void);      /* tic corto al dar un paso            */
void snd_eat(void);       /* blip al comer una manzana           */
void snd_die(void);       /* ruido largo al chocar / perder       */
void snd_bonus(void);     /* tono corto al subir de nivel         */

/* Apaga todas las voces (al salir o cambiar de estado). */
void snd_silence(void);

/* Devuelve un byte pseudoaleatorio del SID.
 * El core implementa la lectura del oscilador de la voz 3 ($D41B, "OSC3 random"):
 * se pone la voz 3 en modo RUIDO (sin gate, para no sonar) y se lee su registro. */
uint8_t snd_random(void);

/* Tick por frame: gestiona las envolventes de los efectos. */
void snd_update(void);

#endif /* SOUND_H */
