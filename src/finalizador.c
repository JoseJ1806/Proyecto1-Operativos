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

// Leer valor actual de un semáforo
static int sem_getval(int sem_id, int sem_num) {
    return semctl(sem_id, sem_num, GETVAL);
}

// Pequeña espera (para no saturar CPU)
static void tiny_sleep_ns(long ns) {
    struct timespec d = {0, ns};
    nanosleep(&d, NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <id_memoria>\n", argv[0]);
        return 1;
    }

    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); return 1; }

    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); return 1; }

    SharedMemory *mem = (SharedMemory*)shmat(shm_id, NULL, 0);
    if (mem == (void*)-1) { perror("shmat"); return 1; }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); return 1; }

    // --- Pausa manual para iniciar cierre ---
    printf("\n\033[1;34mFinalizador listo.\033[0m Presione ENTER para mostrar el resumen y liberar recursos...\n");
    getchar(); // Simula el “botón físico” de cierre

    // Esperar a que el buffer quede vacío
    for (;;) {
        int full_val = sem_getval(sem_id, 2);
        if (full_val <= 0) break;
        tiny_sleep_ns(100000000L); // 0.1 s
    }

    // Tomar snapshot
    int size            = mem->size;
    int count           = mem->count;
    long long written   = mem->total_written;
    long long consumed  = mem->total_consumed;
    int e_act           = mem->emitters_active;
    int r_act           = mem->receivers_active;
    int e_tot           = mem->emitters_total;
    int r_tot           = mem->receivers_total;

    // Cálculo solicitado
    long long transferidos = (written < consumed) ? written : consumed;
    size_t bytes_mem = sizeof(SharedMemory) + (size_t)size * sizeof(SharedChar);

    // Impresión colorida y concisa
    printf("\n\033[1;32m========== RESUMEN FINAL ==========\033[0m\n");
    printf("\033[1;33m- Cantidad de caracteres transferidos:   \033[0m%lld\n", transferidos);
    printf("\033[1;34m- Cantidad de caracteres en memoria:     \033[0m%d\n", count);
    printf("\033[1;35m- Emisores vivos / totales:              \033[0m%d / %d\n", e_act, e_tot);
    printf("\033[1;36m- Receptores vivos / totales:            \033[0m%d / %d\n", r_act, r_tot);
    printf("\033[1;37m- Memoria compartida utilizada:          \033[0m%zu bytes\n", bytes_mem);
    printf("\033[1;32m===================================\033[0m\n");

    // Liberar IPC
    shmdt(mem);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    printf("\n\033[1;34mCierre completado.\033[0m Recursos liberados correctamente.\n");

    return 0;
}
