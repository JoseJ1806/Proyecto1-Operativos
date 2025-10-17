#define _XOPEN_SOURCE 700
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "shared.h"

// Semáforos tolerantes: NO salir aquí; el bucle maneja EIDRM/EINVAL
static int sem_wait_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
static int sem_signal_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}

static void print_table(int index, unsigned char c, time_t t) {
    printf("\033[1;34m---------------------------------------------\033[0m\n");
    printf("\033[1;32m| Índice | Valor ASCII | Hora de Inserción   |\033[0m\n");
    printf("\033[1;33m| %6d | %12u | %s\033[0m", index, c, ctime(&t));
    printf("\033[1;34m---------------------------------------------\033[0m\n");
}

int main(int argc, char *argv[]) {
    // Uso: emisor <id_memoria> <modo(0|1)> <clave_xor>
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <id_memoria> <modo> <clave_xor>\n", argv[0]);
        fprintf(stderr, "Modo: 0 = Manual | 1 = Automático\n");
        exit(EXIT_FAILURE);
    }

    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); exit(EXIT_FAILURE); }

    int mode = atoi(argv[2]); // 0 = manual, 1 = automático
    int xor_key = atoi(argv[3]);

    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); exit(EXIT_FAILURE); }

    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (mem == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); exit(EXIT_FAILURE); }

    FILE *fp = fopen(mem->fuente_path, "rb");
    if (!fp) { perror("fopen fuente"); shmdt(mem); exit(EXIT_FAILURE); }

    // Marcar emisor activo y total (con mutex)
    if (sem_wait_raw(sem_id, 0) == -1) {
        if (errno==EIDRM || errno==EINVAL) goto graceful_exit;
        perror("semop wait mutex");
        goto graceful_exit;
    }
    mem->emitters_active++;
    mem->emitters_total++;
    if (sem_signal_raw(sem_id, 0) == -1) {
        if (errno==EIDRM || errno==EINVAL) goto graceful_exit;
        perror("semop signal mutex");
        goto graceful_exit;
    }

    printf("\nEmisor iniciado (modo %s)\n", mode == 1 ? "automático" : "manual");

    for (;;) {
        // 1) Reservar posición global atómica
        long long pos;
        if (sem_wait_raw(sem_id, 0) == -1) {
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex next_pos). Saliendo emisor...\n"); break; }
            perror("semop wait mutex next_pos"); break;
        }
        pos = mem->next_pos++;
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock next_pos). Saliendo emisor...\n"); break; }
            perror("semop signal mutex next_pos"); break;
        }

        // 2) Leer byte del archivo
        if (fseeko(fp, (off_t)pos, SEEK_SET) != 0) break;
        int ch = fgetc(fp);
        if (ch == EOF) break;
        unsigned char c = (unsigned char)ch;

        // 3) Escribir en buffer circular
        if (sem_wait_raw(sem_id, 1) == -1) { // empty--
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (empty). Saliendo emisor...\n"); break; }
            perror("semop wait empty"); break;
        }
        if (sem_wait_raw(sem_id, 0) == -1) { // mutex--
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex write). Saliendo emisor...\n"); break; }
            perror("semop wait mutex write"); break;
        }

        int idx = mem->write_index;
        mem->buffer[idx].ascii     = (char)(c ^ xor_key);
        mem->buffer[idx].index     = idx;
        mem->buffer[idx].timestamp = time(NULL);
        mem->buffer[idx].is_full   = 1;
        mem->buffer[idx].seq       = pos;

        mem->total_written++;  // contabilizar

        print_table(idx, (unsigned char)mem->buffer[idx].ascii, mem->buffer[idx].timestamp);

        mem->write_index = (idx + 1) % mem->size;
        mem->count++;

        if (sem_signal_raw(sem_id, 0) == -1) { // mutex++
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock write). Saliendo emisor...\n"); break; }
            perror("semop signal mutex write"); break;
        }
        if (sem_signal_raw(sem_id, 2) == -1) { // full++
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (full++). Saliendo emisor...\n"); break; }
            perror("semop signal full"); break;
        }

        if (mode == 0) {
            printf("\nPresione ENTER para enviar el siguiente carácter...\n");
            getchar();
        } else {
            struct timespec d = {0, 400000000L}; // 0.4 s
            nanosleep(&d, NULL);
        }
    }

graceful_exit:
    if (sem_wait_raw(sem_id, 0) == -1) {
        if (!(errno==EIDRM || errno==EINVAL)) perror("semop wait mutex exit");
    } else {
        if (mem->emitters_active > 0) mem->emitters_active--;
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (!(errno==EIDRM || errno==EINVAL)) perror("semop signal mutex exit");
        }
    }

    fclose(fp);
    shmdt(mem);
    printf("\nEmisión finalizada correctamente.\n");
    return 0;
}
