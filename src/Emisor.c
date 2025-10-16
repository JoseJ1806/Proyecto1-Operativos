#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "shared.h"

// Operación WAIT (P) sobre el semáforo
void sem_wait_op(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    semop(sem_id, &op, 1);
}

// Operación SIGNAL (V) sobre el semáforo
void sem_signal_op(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    semop(sem_id, &op, 1);
}

// Imprimir la información del carácter insertado con formato y color
void print_table(int index, char c, time_t t) {
    printf("\033[1;34m---------------------------------------------\033[0m\n");
    printf("\033[1;32m| Índice | Valor ASCII | Hora de Inserción   |\033[0m\n");
    printf("\033[1;33m| %6d | %12d | %s\033[0m", index, c, ctime(&t));
    printf("\033[1;34m---------------------------------------------\033[0m\n");
}

int main(int argc, char *argv[]) {
    // Validar parámetros
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <id_memoria> <modo> <clave_xor>\n", argv[0]);
        fprintf(stderr, "Modo: 0 = Manual | 1 = Automático\n");
        exit(EXIT_FAILURE);
    }

    key_t shm_key = ftok(".", atoi(argv[1]));
    int mode = atoi(argv[2]); // 0 = manual, 1 = automático
    int xor_key = atoi(argv[3]);

    // Conectarse a la memoria compartida existente
    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) {
        perror("Error al obtener memoria compartida");
        exit(EXIT_FAILURE);
    }

    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (mem == (void *)-1) {
        perror("Error al adjuntar memoria compartida");
        exit(EXIT_FAILURE);
    }

    // Conectarse al conjunto de semáforos
    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) {
        perror("Error al obtener semáforos");
        exit(EXIT_FAILURE);
    }

    // Abrir el archivo fuente (debe existir)
    FILE *fp = fopen("texto_fuente.txt", "r");
    if (!fp) {
        perror("Error al abrir archivo fuente");
        exit(EXIT_FAILURE);
    }

    printf("\nEmisor iniciado (modo %s)\n", mode == 1 ? "automático" : "manual");

    char c;
    while ((c = fgetc(fp)) != EOF) {
        // Esperar a que haya espacio libre y acceso al mutex
        sem_wait_op(sem_id, 1); // empty--
        sem_wait_op(sem_id, 0); // mutex--

        // Insertar carácter en la memoria compartida
        int idx = mem->write_index;
        mem->buffer[idx].ascii = c ^ xor_key;       // Codificación XOR
        mem->buffer[idx].index = idx;
        mem->buffer[idx].timestamp = time(NULL);
        mem->buffer[idx].is_full = 1;

        // Mostrar información formateada en consola
        print_table(idx, mem->buffer[idx].ascii, mem->buffer[idx].timestamp);

        // Actualizar índices y contadores
        mem->write_index = (idx + 1) % mem->size;
        mem->count++;

        // Liberar semáforos
        sem_signal_op(sem_id, 0); // mutex++
        sem_signal_op(sem_id, 2); // full++

        // Control de modo
        if (mode == 0) {
            printf("\nPresione ENTER para enviar el siguiente carácter...\n");
            getchar();
        } else {
            usleep(400000); // pausa de 0.4 segundos
        }
    }

    fclose(fp);
    shmdt(mem);
    printf("\nEmisión finalizada correctamente.\n");
    return 0;
}
