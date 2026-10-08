/**
 * ROMAPI.H - Header para acceder a la ROM API del Monitor 6502
 * 
 * La ROM expone una Jump Table fija en $BF00 con funciones del sistema.
 * Los programas standalone pueden llamar estas funciones sin incluir
 * las librerias, ahorrando espacio en RAM.
 * 
 * JUMP TABLE: $BF00 - $BFED (38 funciones)
 * MAGIC:      $BF84 - "ROMAPI" v2.4
 * 
 * 
 *   CONVENCIN DE LLAMADA (CC65):                         
 *   - Fastcall: parametros en A/X (funciona desde afuera) 
 *   - Stack: parametros por stack software CC65           
 *   - [ZP]: parametros en Zero Page fijo ($F0-$F7)        
 *     Los wrappers ZP evitan conflictos de stack entre     
 *     el monitor ($000E) y programas externos ($0026).     
 * 
 * 
 * ===========================================================================
 * TABLA RPIDA
 * ===========================================================================
 * 
 * Address   Funcion            Conv.    Parametros ZP (si aplica)
 * -------   ------------------ -------- -------------------------------
 * $BF00     sd_init()          fastcall
 * $BF03     mfs_mount()        fastcall
 * $BF06     mfs_open(name)     fastcall  name en AX
 * $BF09     mfs_read           [ZP]      $F0=buf, $F2=len
 * $BF0C     mfs_close()        fastcall
 * $BF0F     mfs_get_size()     fastcall
 * $BF12     mfs_list           [ZP]      $F4=index, $F5=info ptr
 * $BF15     uart_init()        fastcall
 * $BF18     uart_putc(c)       fastcall  c en A
 * $BF1B     uart_getc()        fastcall  retorna en A
 * $BF1E     uart_puts(str)     fastcall  str en AX
 * $BF21     uart_rx_ready()    fastcall  retorna status en A
 * $BF24     uart_tx_ready()    fastcall  retorna status en A
 * $BF27     mfs_read_ext       [ZP]      $F0=buf, $F2=len
 * $BF2A     xmodem_receive     fastcall  dest addr en AX
 * $BF2D     get_micros()       fastcall  retorna uint32
 * $BF30     delay_us(us)       fastcall  us en AX
 * $BF33     delay_ms(ms)       fastcall  ms en AX
 * $BF36     uart_clear_errors  fastcall
 * $BF39     uart_set_baudrate  fastcall  divisor en AX
 * $BF3C     mfs_create         [ZP]      $F4=name, $F6=size
 * $BF3F     mfs_write          [ZP]      $F4=buf, $F6=len
 * $BF42     mfs_delete(name)   fastcall  name en AX
 * $BF45     mfs_format()       fastcall
 * $BF48     spi_init()         fastcall
 * $BF4B     spi_select(cs)     fastcall  cs en A
 * $BF4E     spi_deselect()     fastcall
 * $BF51     spi_transfer(d)    fastcall  d en A, retorna en A
 * $BF54     spi_send(d)        fastcall  d en A
 * $BF57     spi_receive()      fastcall  retorna en A
 * $BF5A     spi_busy()         fastcall  retorna status en A
 * $BF5D     i2c_init()         fastcall
 * $BF60     i2c_start(dev,rw)  fastcall  dev en A, rw en X
 * $BF63     i2c_stop()         fastcall
 * $BF66     i2c_write_byte(d)  fastcall  d en A, retorna ACK
 * $BF69     i2c_read_byte(a)   fastcall  ack en A, retorna byte
 * $BF6C     i2c_write          stack     (ver nota abajo)
 * $BF6F     i2c_read           stack     (ver nota abajo)
 * $BF72     sd_read_sector     [ZP]      $F0=sector(32b), $F4=buf
 * $BF75     sd_write_sector    [ZP]      $F0=sector(32b), $F4=buf
 * $BF78     sd_is_ready()      fastcall
 * $BF7B     sd_get_type()      fastcall
 * $BF7E     mfs_load_file      [ZP]      $F4=name, $F6=addr
 * $BF81     mfs_load_run       [ZP]      $F4=name, $F6=addr
 * $BF84     mfs_seek(offset)   fastcall  offset en AX
 * $BF87     Magic "ROMAPI"     -         Identificador + version
 * 
 * NOTA: i2c_write ($BF6C) e i2c_read ($BF6F) usan stack CC65.
 *       No tienen wrapper ZP. Desde programas externos, usar
 *       las funciones byte-level: start/stop/write_byte/read_byte.
 */

#ifndef ROMAPI_H
#define ROMAPI_H

#include <stdint.h>


/* ===========================================================================
 * 1. DIRECCIONES DE LA JUMP TABLE
 * =========================================================================== */

#define ROMAPI_BASE             0xBF00

/* --- SD Card --- */
#define ROMAPI_SD_INIT          0xBF00
#define ROMAPI_SD_READ_SECTOR   0xBF72
#define ROMAPI_SD_WRITE_SECTOR  0xBF75
#define ROMAPI_SD_IS_READY      0xBF78
#define ROMAPI_SD_GET_TYPE      0xBF7B

/* --- MicroFS (Sistema de archivos) --- */
#define ROMAPI_MFS_MOUNT        0xBF03
#define ROMAPI_MFS_OPEN         0xBF06
#define ROMAPI_MFS_READ         0xBF09    /* [ZP] usa $F0-$F3 */
#define ROMAPI_MFS_CLOSE        0xBF0C
#define ROMAPI_MFS_GET_SIZE     0xBF0F
#define ROMAPI_MFS_LIST         0xBF12    /* [ZP] usa $F4-$F6 */
#define ROMAPI_MFS_READ_EXT     0xBF27    /* [ZP] usa $F0-$F3 */
#define ROMAPI_MFS_CREATE       0xBF3C    /* [ZP] usa $F4-$F7 */
#define ROMAPI_MFS_WRITE        0xBF3F    /* [ZP] usa $F4-$F7 */
#define ROMAPI_MFS_DELETE       0xBF42
#define ROMAPI_MFS_FORMAT       0xBF45
#define ROMAPI_MFS_SEEK         0xBF84

/* --- UART --- */
#define ROMAPI_UART_INIT        0xBF15
#define ROMAPI_UART_PUTC        0xBF18
#define ROMAPI_UART_GETC        0xBF1B
#define ROMAPI_UART_PUTS        0xBF1E
#define ROMAPI_UART_RX_READY    0xBF21
#define ROMAPI_UART_TX_READY    0xBF24
#define ROMAPI_UART_CLEAR_ERRORS 0xBF36
#define ROMAPI_UART_SET_BAUDRATE 0xBF39

/* --- XMODEM --- */
#define ROMAPI_XMODEM_RECV      0xBF2A

/* --- Timer --- */
#define ROMAPI_GET_MICROS       0xBF2D
#define ROMAPI_DELAY_US         0xBF30
#define ROMAPI_DELAY_MS         0xBF33

/* --- SPI --- */
#define ROMAPI_SPI_INIT         0xBF48
#define ROMAPI_SPI_SELECT       0xBF4B
#define ROMAPI_SPI_DESELECT     0xBF4E
#define ROMAPI_SPI_TRANSFER     0xBF51
#define ROMAPI_SPI_SEND         0xBF54
#define ROMAPI_SPI_RECEIVE      0xBF57
#define ROMAPI_SPI_BUSY         0xBF5A

/* --- I2C --- */
#define ROMAPI_I2C_INIT         0xBF5D
#define ROMAPI_I2C_START        0xBF60
#define ROMAPI_I2C_STOP         0xBF63
#define ROMAPI_I2C_WRITE_BYTE   0xBF66
#define ROMAPI_I2C_READ_BYTE    0xBF69
#define ROMAPI_I2C_WRITE        0xBF6C    /* usa stack - no wrapper */
#define ROMAPI_I2C_READ         0xBF6F    /* usa stack - no wrapper */

/* --- Identificador ROM API --- */
#define ROMAPI_MAGIC_ADDR       0xBF87
#define ROMAPI_MAGIC            "ROMAPI"

/* --- Carga y ejecución desde SD --- */
#define ROMAPI_MFS_LOAD_FILE    0xBF7E
#define ROMAPI_MFS_LOAD_RUN     0xBF81


/* ===========================================================================
 * 2. MACROS DE LLAMADA (fastcall - funcionan desde programas externos)
 * ===========================================================================
 * Estas funciones usan solo registros A/X, sin stack de CC65.
 * Se pueden llamar directamente desde cualquier programa.
 * =========================================================================== */

/* --- SD Card --- */
#define rom_sd_init()           (((uint8_t (*)(void))ROMAPI_SD_INIT)())
#define rom_sd_is_ready()       (((uint8_t (*)(void))ROMAPI_SD_IS_READY)())
#define rom_sd_get_type()       (((uint8_t (*)(void))ROMAPI_SD_GET_TYPE)())

/* --- MicroFS (fastcall) --- */
#define rom_mfs_mount()         (((uint8_t (*)(void))ROMAPI_MFS_MOUNT)())
#define rom_mfs_seek(offset)    (((uint8_t (*)(uint16_t))ROMAPI_MFS_SEEK)(offset))
#define rom_mfs_open(name)      \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(name), \
     ((uint8_t (*)(void))ROMAPI_MFS_OPEN)())
#define rom_mfs_close()         (((void (*)(void))ROMAPI_MFS_CLOSE)())
#define rom_mfs_get_size()      (((uint16_t (*)(void))ROMAPI_MFS_GET_SIZE)())
#define rom_mfs_delete(name)    \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(name), \
     ((uint8_t (*)(void))ROMAPI_MFS_DELETE)())
#define rom_mfs_format()        (((uint8_t (*)(void))ROMAPI_MFS_FORMAT)())

/* --- UART --- */
#define rom_uart_init()         (((void (*)(void))ROMAPI_UART_INIT)())
#define rom_uart_putc(c)        (((void (*)(char))ROMAPI_UART_PUTC)(c))
#define rom_uart_getc()         (((char (*)(void))ROMAPI_UART_GETC)())
#define rom_uart_puts(s)        (((void (*)(const char*))ROMAPI_UART_PUTS)(s))
#define rom_uart_rx_ready()     (((uint8_t (*)(void))ROMAPI_UART_RX_READY)())
#define rom_uart_tx_ready()     (((uint8_t (*)(void))ROMAPI_UART_TX_READY)())
#define rom_uart_clear_errors() (((void (*)(void))ROMAPI_UART_CLEAR_ERRORS)())
#define rom_uart_set_baudrate(d) (((void (*)(uint16_t))ROMAPI_UART_SET_BAUDRATE)(d))

/* --- XMODEM --- */
#define rom_xmodem_receive(addr) (((int (*)(unsigned int))ROMAPI_XMODEM_RECV)(addr))

/* --- Timer --- */
#define rom_get_micros()        (((uint32_t (*)(void))ROMAPI_GET_MICROS)())
#define rom_delay_us(us)        (((void (*)(uint16_t))ROMAPI_DELAY_US)(us))
#define rom_delay_ms(ms)        (((void (*)(uint16_t))ROMAPI_DELAY_MS)(ms))

/* --- SPI --- */
#define rom_spi_init()          (((void (*)(void))ROMAPI_SPI_INIT)())
#define rom_spi_select(m)       (((void (*)(uint8_t))ROMAPI_SPI_SELECT)(m))
#define rom_spi_deselect()      (((void (*)(void))ROMAPI_SPI_DESELECT)())
#define rom_spi_transfer(d)     (((uint8_t (*)(uint8_t))ROMAPI_SPI_TRANSFER)(d))
#define rom_spi_send(d)         (((void (*)(uint8_t))ROMAPI_SPI_SEND)(d))
#define rom_spi_receive()       (((uint8_t (*)(void))ROMAPI_SPI_RECEIVE)())
#define rom_spi_busy()          (((uint8_t (*)(void))ROMAPI_SPI_BUSY)())

/* --- I2C (fastcall - funciones basicas) --- */
#define rom_i2c_init()          (((void (*)(void))ROMAPI_I2C_INIT)())
#define rom_i2c_start(dev,rw)   (((uint8_t (*)(uint8_t, uint8_t))ROMAPI_I2C_START)(dev, rw))
#define rom_i2c_stop()          (((void (*)(void))ROMAPI_I2C_STOP)())
#define rom_i2c_write_byte(d)   (((uint8_t (*)(uint8_t))ROMAPI_I2C_WRITE_BYTE)(d))
#define rom_i2c_read_byte(a)    (((uint8_t (*)(uint8_t))ROMAPI_I2C_READ_BYTE)(a))


/* ===========================================================================
 * 3. ZP WRAPPERS - Para funciones que usan stack de CC65
 * ===========================================================================
 * 
 * 
 *   ?POR QU EXISTEN ESTOS WRAPPERS?                            
 *   El monitor guarda su stack pointer de CC65 en ZP $000E.     
 *   Los programas externos tienen el suyo en otra direccion     
 *   (ej: $0026). Si un programa externo llama a una funcion     
 *   que lee parametros del stack, lee de $000E en vez de su     
 *   propio stack, obteniendo basura.                            
 *                                                               
 *   Los wrappers ZP resuelven esto: el programa externo         
 *   escribe los parametros en direcciones fijas de Zero Page,   
 *   y el wrapper en la ROM los pasa al stack correcto.          
 *                                                               
 *   ZP usado: $F0-$F7 (8 bytes, libres para el usuario)        
 * 
 * 
 * Las macros _via_zp escriben los parametros en ZP y llaman a la
 * funcion. No necesitas tocar ZP manualmente.
 * =========================================================================== */

/* mfs_read:   $F0-$F1 = buf ptr,  $F2-$F3 = len */
#define rom_mfs_read_via_zp(buf, len) \
    (*(volatile uint16_t*)0xF0 = (uint16_t)(buf), \
     *(volatile uint16_t*)0xF2 = (len), \
     ((uint16_t (*)(void))ROMAPI_MFS_READ)())

/* mfs_read_ext:  $F0-$F1 = buf ptr,  $F2-$F3 = len (version original) */
#define rom_mfs_read_ext() \
    ((uint16_t (*)(void))ROMAPI_MFS_READ_EXT)()

/* mfs_list:  $F4 = index,  $F5-$F6 = info ptr */
typedef struct {
    char     name[12];
    uint16_t size;
    uint8_t  index;
} rom_mfs_fileinfo_t;

#define rom_mfs_list_via_zp(index, info) \
    (*(volatile uint8_t*)0xF4 = (index), \
     *(volatile uint16_t*)0xF5 = (uint16_t)(info), \
     ((uint8_t (*)(void))ROMAPI_MFS_LIST)())

/* mfs_create:  $F4-$F5 = name ptr,  $F6-$F7 = size */
#define rom_mfs_create_via_zp(name, size) \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(name), \
     *(volatile uint16_t*)0xF6 = (size), \
     ((uint8_t (*)(void))ROMAPI_MFS_CREATE)())

/* mfs_write:  $F4-$F5 = buf ptr,  $F6-$F7 = len */
#define rom_mfs_write_via_zp(buf, len) \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(buf), \
     *(volatile uint16_t*)0xF6 = (len), \
     ((uint16_t (*)(void))ROMAPI_MFS_WRITE)())

/* sd_read_sector:  $F0-$F3 = sector (uint32),  $F4-$F5 = buf ptr */
#define rom_sd_read_sector_via_zp(sector, buf) \
    (*(volatile uint32_t*)0xF0 = (sector), \
     *(volatile uint16_t*)0xF4 = (uint16_t)(buf), \
     ((uint8_t (*)(void))ROMAPI_SD_READ_SECTOR)())

/* sd_write_sector: $F0-$F3 = sector (uint32),  $F4-$F5 = buf ptr */
#define rom_sd_write_sector_via_zp(sector, buf) \
    (*(volatile uint32_t*)0xF0 = (sector), \
     *(volatile uint16_t*)0xF4 = (uint16_t)(buf), \
     ((uint8_t (*)(void))ROMAPI_SD_WRITE_SECTOR)())

/* mfs_load_file: Carga archivo SD a memoria */
/*   $F4-$F5 = nombre archivo, $F6-$F7 = direccion destino */
#define rom_mfs_load_file(name, addr) \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(name), \
     *(volatile uint16_t*)0xF6 = (addr), \
     ((void (*)(void))ROMAPI_MFS_LOAD_FILE)())

/* mfs_load_run: Carga archivo SD y lo ejecuta */
/*   $F4-$F5 = nombre archivo, $F6-$F7 = direccion destino */
#define rom_mfs_load_run(name, addr) \
    (*(volatile uint16_t*)0xF4 = (uint16_t)(name), \
     *(volatile uint16_t*)0xF6 = (addr), \
     ((void (*)(void))ROMAPI_MFS_LOAD_RUN)())


/* ===========================================================================
 * 4. CONSTANTES DE BAUDRATE UART
 * ===========================================================================
 * Divisores para CLK 6.75 MHz. Usar con rom_uart_set_baudrate().
 * =========================================================================== */
#define ROM_UART_BAUD_9600      703     /* $02BF */
#define ROM_UART_BAUD_19200     351     /* $015F */
#define ROM_UART_BAUD_38400     175     /* $00AF */
#define ROM_UART_BAUD_57600     117     /* $0075 */
#define ROM_UART_BAUD_115200    58      /* $003A - Por defecto */


/* ===========================================================================
 * 5. CDIGOS DE ERROR
 * =========================================================================== */

/* SD Card */
#define SD_OK               0x00
#define SD_ERROR_TIMEOUT    0x01
#define SD_ERROR_CMD        0x02
#define SD_ERROR_INIT       0x03
#define SD_ERROR_READ       0x04
#define SD_ERROR_WRITE      0x05

/* MicroFS */
#define MFS_OK              0
#define MFS_ERR_DISK        1
#define MFS_ERR_NOFS        2
#define MFS_ERR_NOTFOUND    3
#define MFS_ERR_FULL        4
#define MFS_ERR_EXISTS      5

/* XMODEM */
#define XMODEM_ERROR_TIMEOUT   -1
#define XMODEM_ERROR_CANCELLED -2
#define XMODEM_ERROR_SYNC      -3
#define XMODEM_ERROR_CHECKSUM  -4


/* ===========================================================================
 * 6. EJEMPLOS DE USO
 * ===========================================================================
 * 
 *  Guardar archivo en SD (via ZP wrapper)
 *   rom_mfs_delete("TEST.TXT");           // eliminar si existe
 *   rom_mfs_create_via_zp("TEST.TXT", len); // crear (abre internamente)
 *   rom_mfs_write_via_zp(buffer, len);     // escribir datos
 *   rom_mfs_close();                       // cerrar
 *
 *  Leer archivo de SD (via ZP wrapper)
 *   rom_mfs_open("TEST.TXT");
 *   uint16_t size = rom_mfs_get_size();
 *   rom_mfs_read_via_zp(buffer, size);
 *   rom_mfs_close();
 * 
 *  Listar archivos (via ZP wrapper) 
 *   rom_mfs_fileinfo_t info;
 *   for (uint8_t i = 0; i < 16; i++) {
 *       if (rom_mfs_list_via_zp(i, &info) == MFS_OK) {
 *           // info.name, info.size
 *       }
 *   }
 * 
 *  Sector raw (via ZP wrapper) 
 *   rom_sd_read_sector_via_zp(0, buffer);  // leer sector 0
 * 
 *  UART (fastcall, directo) 
 *   rom_uart_puts("Hola!\r\n");
 *   char c = rom_uart_getc();
 * 
 *  SPI (fastcall, directo) 
 *   rom_spi_init();
 *   rom_spi_select(0x01);        // CS=bit0
 *   uint8_t r = rom_spi_transfer(0xFF);
 *   rom_spi_deselect();
 * 
 *  I2C (fastcall, directo) 
 *   rom_i2c_init();
 *   if (rom_i2c_start(0x50, 0)) {  // WRITE
 *       rom_i2c_write_byte(0x00);  // addr
 *       rom_i2c_stop();
 *   }
 *   uint8_t d = rom_i2c_read_byte(0);  // NACK = last byte
 * 
 *  Timer (fastcall, directo) 
 *   rom_delay_ms(500);
 *   uint32_t t = rom_get_micros();
 * 
 *  XMODEM (fastcall, directo) 
 *   int n = rom_xmodem_receive(0x1000);
 *   if (n > 0) {  // OK, n bytes  }
 * 
 *  Deteccion de ROM API 
 *   if (*(uint16_t*)ROMAPI_MAGIC_ADDR == *(uint16_t*)"RO") {
 *       // ROM API presente
 *   }
 */

#endif /* ROMAPI_H */
