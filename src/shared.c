#ifndef SHARED_H
#define SHARED_H

#include <time.h>

// Estructura que representa un carácter en la memoria compartida
typedef struct {
    char ascii;          // Valor ASCII (codificado con XOR)
    int index;           // Índice dentro del buffer circular
    time_t timestamp;    // Hora en la que se insertó
    int is_full;         // Indicador: 1 = lleno, 0 = vacío
} SharedChar;

// Estructura principal de la memoria compartida
typedef struct {
    int size;            // Tamaño total del buffer
    int write_index;     // Índice actual de escritura
    int read_index;      // Índice actual de lectura
    int count;           // Cantidad de caracteres almacenados actualmente
    SharedChar buffer[]; // Buffer flexible (tamaño variable)
} SharedMemory;

#endif
