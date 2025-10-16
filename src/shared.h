#ifndef SHARED_H
#define SHARED_H

#include <time.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Cada entrada en el buffer circular
typedef struct {
    char ascii;          // Valor ASCII (codificado con XOR)
    int index;           // Índice dentro del buffer circular
    time_t timestamp;    // Hora en la que se insertó
    int is_full;         // Indicador: 1 = lleno, 0 = vacío
    long long seq;       // Número de orden global (para reensamblar)
} SharedChar;

// Estructura principal de la memoria compartida
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

    long long next_to_flush;   // <<< NUEVO: próximo seq que debe escribirse en el archivo

    char fuente_path[PATH_MAX]; // Ruta del archivo fuente

    // Buffer flexible (tamaño variable)
    SharedChar buffer[];
} SharedMemory;

#endif
