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

// Semáforos tolerantes
static int sem_wait_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
static int sem_signal_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}

// print “elegante”
static void print_table(int index, char c_dec, time_t t_ins) {
    printf("\033[1;35m---------------------------------------------\033[0m\n");
    printf("\033[1;36m| Índice | Carácter | Hora de Inserción     |\033[0m\n");
    printf("\033[1;33m| %6d | %8c | %s\033[0m",
           index, (c_dec >= 32 && c_dec <= 126) ? c_dec : '?', ctime(&t_ins));
    printf("\033[1;35m---------------------------------------------\033[0m\n");
}

static void tiny_sleep_ns(long ns) {
    struct timespec d = {0, ns};
    nanosleep(&d, NULL);
}

int main(int argc, char *argv[]) {
    // Uso: receptor <id_memoria> <modo(0|1)> <clave_xor> <archivo_salida>
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <id_memoria> <modo(0|1)> <clave_xor> <archivo_salida>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); exit(EXIT_FAILURE); }

    int mode     = atoi(argv[2]);  // 0 manual, 1 automático
    int xor_key  = atoi(argv[3]);
    const char *out_path = argv[4];

    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); exit(EXIT_FAILURE); }

    SharedMemory *mem = (SharedMemory*)shmat(shm_id, NULL, 0);
    if (mem == (void*)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); exit(EXIT_FAILURE); }

    // Registrar receptor activo y total
    if (sem_wait_raw(sem_id, 0) == -1) {
        if (errno==EIDRM || errno==EINVAL) goto graceful_exit;
        perror("semop wait mutex start"); goto graceful_exit;
    }
    mem->receivers_active++;
    mem->receivers_total++;
    if (sem_signal_raw(sem_id, 0) == -1) {
        if (errno==EIDRM || errno==EINVAL) goto graceful_exit;
        perror("semop signal mutex start"); goto graceful_exit;
    }

    // Abrir archivo de salida (todos lo abren en "append", pero solo escriben cuando les toque)
    FILE *fout = fopen(out_path, "a");
    if (!fout) { perror("fopen salida"); goto graceful_exit; }

    printf("\nReceptor iniciado (modo %s). Escribiendo colaborativamente en: %s\n",
           mode==1 ? "automático" : "manual", out_path);

    for (;;) {
        // Esperar dato disponible (full)
        if (sem_wait_raw(sem_id, 2) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (full). Saliendo receptor...\n"); break; }
            perror("semop wait full"); break;
        }
        // Tomar mutex
        if (sem_wait_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex). Saliendo receptor...\n"); break; }
            perror("semop wait mutex"); break;
        }

        int idx = mem->read_index;
        SharedChar sc = mem->buffer[idx];
        mem->buffer[idx].is_full = 0;
        mem->read_index = (idx + 1) % mem->size;
        if (mem->count > 0) mem->count--;

        if (sem_signal_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock). Saliendo receptor...\n"); break; }
            perror("semop signal mutex"); break;
        }
        if (sem_signal_raw(sem_id, 1) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (empty++). Saliendo receptor...\n"); break; }
            perror("semop signal empty"); break;
        }

        // Decodificar
        char c_dec = (char)((unsigned char)sc.ascii ^ (unsigned char)xor_key);

        // Contabilizar consumido (mutex corto)
        if (sem_wait_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex stats). Saliendo receptor...\n"); break; }
            perror("semop wait mutex stats"); break;
        }
        mem->total_consumed++;
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock stats). Saliendo receptor...\n"); break; }
            perror("semop signal mutex stats"); break;
        }

        // Mostrar en consola en tiempo real (requisito)
        print_table(sc.index, c_dec, sc.timestamp);
        putchar(c_dec);
        fflush(stdout);

        // --- Escritura colaborativa en orden ---
        // Reintenta hasta que sea su turno (seq == next_to_flush)
        for (;;) {
            if (sem_wait_raw(sem_id, 0) == -1) {
                if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex flush). Saliendo receptor...\n"); goto end_loop; }
                perror("semop wait mutex flush"); goto end_loop;
            }
            long long expected = mem->next_to_flush;
            if (sc.seq == expected) {
                // Es mi turno: escribir y avanzar
                if (fputc(c_dec, fout) == EOF) perror("fputc");
                fflush(fout);
                mem->next_to_flush = expected + 1;
                if (sem_signal_raw(sem_id, 0) == -1) {
                    if (!(errno == EIDRM || errno == EINVAL)) perror("semop signal mutex flush");
                }
                break; // listo este carácter
            }
            // No es mi turno aún
            if (sem_signal_raw(sem_id, 0) == -1) {
                if (!(errno == EIDRM || errno == EINVAL)) perror("semop signal mutex flush (not yet)");
            }
            // Espera breve y reintenta
            tiny_sleep_ns(50000000L); // 50 ms
        }

        // Control de modo
        if (mode == 0) {
            printf("\nPresione ENTER para leer el siguiente carácter...\n");
            getchar();
        } else {
            struct timespec d = {0, 400000000L}; // 0.4 s
            nanosleep(&d, NULL);
        }
        continue;

end_loop:
        break;
    }

    if (fout) fclose(fout);

graceful_exit:
    // Decrementar receptores activos (si se puede)
    if (sem_wait_raw(sem_id, 0) == -1) {
        if (!(errno == EIDRM || errno == EINVAL)) perror("semop wait mutex exit");
    } else {
        if (mem->receivers_active > 0) mem->receivers_active--;
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (!(errno == EIDRM || errno == EINVAL)) perror("semop signal mutex exit");
        }
    }

    shmdt(mem);
    printf("\nReceptor finalizado correctamente.\n");
    return 0;
}
