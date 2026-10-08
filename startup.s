; ============================================
; startup.s - Código de inicio para programas C
; ============================================
; Inicializa el runtime CC65 para programas cargados en RAM
; Se ejecuta desde $0800
; ============================================

.export _init
.export __STARTUP__ : absolute = 1

.import _main
.import __BSS_RUN__, __BSS_SIZE__
.importzp c_sp

; Variables temporales en zero page
.segment "ZEROPAGE"
ptr1:       .res 2
ptr2:       .res 2
count:      .res 2

.segment "STARTUP"

_init:
    ; Deshabilitar interrupciones durante init
    sei
    cld

    ; Inicializar stack pointer del 6502
    ldx #$FF
    txs

    ; Inicializar stack pointer de CC65 (software stack)
    ; Usar $3DFF como tope del stack
    lda #<$3DFF
    sta c_sp
    lda #>$3DFF
    sta c_sp+1

    ; Inicializar BSS a ceros
    jsr zerobss

    ; Llamar a main
    jsr _main

    ; Si main retorna, saltar al monitor en ROM.
    ; NOTA: se usa jmp (reset del monitor) y NO un RTS, porque el monitor no
    ; preserva su estado (ZP y RAM) antes de ejecutar el programa: al volver
    ; con RTS reanudaria con su estado corrupto. jmp $8000 es el mecanismo
    ; previsto por el monitor (igual que en sus ejemplos oficiales).
    jmp $8000

; ============================================
; zerobss - Inicializa BSS a ceros
; ============================================
zerobss:
    ; Si BSS_SIZE es 0, no hay nada que hacer
    lda #<__BSS_SIZE__
    ora #>__BSS_SIZE__
    beq @done

    ; Inicializar puntero al inicio de BSS
    lda #<__BSS_RUN__
    sta ptr1
    lda #>__BSS_RUN__
    sta ptr1+1

    ; Contador de bytes
    lda #<__BSS_SIZE__
    sta count
    lda #>__BSS_SIZE__
    sta count+1

    ; Llenar con ceros
    ldy #0
    lda #0
@loop:
    sta (ptr1),y

    ; Incrementar puntero
    inc ptr1
    bne @skip
    inc ptr1+1
@skip:

    ; Decrementar contador
    lda count
    bne @dec_low
    dec count+1
@dec_low:
    dec count

    ; Verificar si terminamos
    lda count
    ora count+1
    bne @loop

@done:
    rts
