#ifndef SHARED_H
#define SHARED_H
/*
 =============================================================================
  Archivo: shared.h
  Propósito:
    Definir los tipos y el contrato de la memoria compartida para el proyecto
    de Comunicación de Procesos Sincronizada (buffer circular con metadatos).

  Resumen funcional:
    - SharedChar: entrada del buffer con (ascii codificado), índice local,
      timestamp y número de orden global (seq) para reconstrucción.
    - SharedMemory: control del buffer circular + contadores globales + ruta
      del archivo fuente + “struct flexible” buffer[].

  Relación con el enunciado:
    • Cada carácter almacenado debe incluir: valor ASCII, índice, hora de
      inserción y cualquier otro dato necesario (usamos seq).
    • Memoria circular, sin sobrescritura de datos no leídos.
    • Soporte para n emisores y n receptores concurrentes.

  Notas de portabilidad:
    - Definir _XOPEN_SOURCE 700 en los .c antes de <unistd.h> para fseeko/ftello/nanosleep.
    - PATH_MAX podría no estar definido; se define una reserva prudente (4096).

  Invariantes esperados (mantenidos por Emisor/Receptor con semáforos):
    1) 0 <= count <= size
    2) write_index y read_index en [0, size-1], avanzan módulo size
    3) empty == size - count, full == count (en términos lógicos)
    4) No se sobrescriben entradas con is_full=1
    5) seq es estricto creciente por carácter leído del archivo,
       y next_to_flush indica el siguiente seq que debe persistirse
       (escritura colaborativa ordenada en Receptor).
 =============================================================================
*/
#include <time.h>
#include <limits.h>

/* -------------------------------
   PATH_MAX de respaldo (portátil)
   ------------------------------- */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


/* =========================================================
   Entrada del buffer circular
   ---------------------------------------------------------
   ascii     : valor almacenado en el buffer (codificado XOR
               por Emisor; Receptor lo decodifica al leer).
   index     : índice local dentro del buffer circular
               (útil para trazabilidad en consola).
   timestamp : momento de inserción (time(NULL) en Emisor).
   is_full   : 1 si la celda está ocupada, 0 si está libre.
   seq       : número de orden global asignado desde archivo;
               Receptor usa (seq == next_to_flush) para escribir
               en orden en el archivo de salida.
   ========================================================= */
typedef struct {
    char ascii;          // Valor ASCII (codificado con XOR)
    int index;           // Índice dentro del buffer circular
    time_t timestamp;    // Hora en la que se insertó
    int is_full;         // Indicador: 1 = lleno, 0 = vacío
    long long seq;       // Número de orden global (para reensamblar)
} SharedChar;

/* =========================================================
   Memoria compartida principal (segmento IPC)
   ---------------------------------------------------------
   size         : capacidad (n° de celdas) del buffer circular.
   write_index  : índice donde el Emisor escribirá el próximo dato.
   read_index   : índice donde el Receptor leerá el próximo dato.
   count        : cantidad de elementos actualmente en el buffer.
   next_pos     : desplazamiento global de lectura en archivo fuente
                  (asignado atómicamente por Emisores).
   total_written: total de caracteres insertados al buffer.
   total_consumed: total de caracteres extraídos del buffer.
   emitters_active / receivers_active:
                  contadores vivos (para estadísticas de cierre).
   emitters_total / receivers_total:
                  contadores acumulados (cuántos han iniciado alguna vez).
   next_to_flush: siguiente seq que debe persistirse (archivo destino).
   fuente_path  : ruta del archivo fuente a transmitir.
   buffer[]     : arreglo flexible de SharedChar (tamaño = size).
   ========================================================= */
typedef struct {
    // Control del buffer
    int size;            // Tamaño total del buffer
    int write_index;     // Índice actual de escritura
    int read_index;      // Índice actual de lectura
    int count;           // Cantidad de caracteres almacenados actualmente

    // Configuración y estado compartido
    long long next_pos;        // Próxima posición global a leer del archivo (emisor)
    long long total_written;   // Caracteres escritos al buffer
    long long total_consumed;  // Caracteres consumidos por receptores

    int emitters_active;       // Emisores activos
    int receivers_active;      // Receptores activos
    int emitters_total;        // Emisores que han iniciado alguna vez
    int receivers_total;       // Receptores que han iniciado alguna vez

    long long next_to_flush;   // próximo seq que debe escribirse en el archivo

    char fuente_path[PATH_MAX]; // Ruta del archivo fuente

    // Buffer flexible (tamaño variable)
    SharedChar buffer[];
} SharedMemory;

#endif
