#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "shared.h"

// Estructura necesaria para inicializar semáforos con semctl()
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main(int argc, char *argv[]) {
    // Validar parámetros de entrada
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <id_memoria> <tamano_buffer> <clave_xor> <archivo_fuente>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Conversión de argumentos
    key_t shm_key = ftok(".", atoi(argv[1]));  // Clave de la memoria compartida
    int size = atoi(argv[2]);                  // Tamaño del buffer circular
    int xor_key = atoi(argv[3]);               // Clave XOR para codificar
    char *filename = argv[4];                  // Archivo de texto fuente

    // Crear el segmento de memoria compartida
    int shm_id = shmget(shm_key, sizeof(SharedMemory) + size * sizeof(SharedChar), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error al crear memoria compartida");
        exit(EXIT_FAILURE);
    }

    // Adjuntar el segmento al espacio de direcciones del proceso
    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (mem == (void *)-1) {
        perror("Error al adjuntar memoria compartida");
        exit(EXIT_FAILURE);
    }

    // Inicializar estructura de control de la memoria
    mem->size = size;
    mem->write_index = 0;
    mem->read_index = 0;
    mem->count = 0;
    for (int i = 0; i < size; i++) {
        mem->buffer[i].is_full = 0;
    }

    // Crear el conjunto de semáforos: mutex, empty, full
    int sem_id = semget(shm_key, 3, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Error al crear semáforos");
        exit(EXIT_FAILURE);
    }

    // Inicialización de los semáforos
    // mutex = 1 (libre)
    // empty = size (todos los espacios vacíos)
    // full = 0 (ningún espacio lleno)
    union semun arg;
    unsigned short values[3] = {1, size, 0};
    arg.array = values;
    if (semctl(sem_id, 0, SETALL, arg) == -1) {
        perror("Error al inicializar semáforos");
        exit(EXIT_FAILURE);
    }

    // Mostrar información del entorno creado
    printf("\n Memoria compartida inicializada correctamente.\n");
    printf("ID memoria: %d\n", shm_id);
    printf("Clave XOR: %d\n", xor_key);
    printf("Archivo fuente: %s\n", filename);
    printf("Tamaño del buffer: %d caracteres\n", size);

    // Desvincular la memoria compartida
    shmdt(mem);
    return 0;
}
