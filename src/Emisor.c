/*
 ============================================================================
 Archivo: Emisor.c
 Proyecto: Comunicación de Procesos Sincronizada
 Curso: CE4303 - Principios de Sistemas Operativos
 Profesor: M.Sc. Jason Leitón Jiménez
 Fecha: 17-10-25
 Descripción:
    Este proceso (Emisor) es responsable de leer los caracteres del archivo
    fuente establecido por el Inicializador, codificarlos mediante una operación
    XOR, y escribirlos en la memoria compartida de forma circular y sincronizada.

    Cumple con las siguientes funciones descritas en el proyecto:
      - Llenar el buffer circular en memoria compartida sin utilizar busy waiting.
      - Bloquearse cuando no haya espacio (controlado con semáforos).
      - Insertar cada carácter con su valor ASCII, índice, timestamp y secuencia.
      - Permitir múltiples instancias de emisores trabajando simultáneamente.
 ============================================================================
*/
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

/* --------------------------------------------------------------------------
   Funciones auxiliares: control de semáforos
   -------------------------------------------------------------------------- */

// Disminuye el valor del semáforo (wait)
static int sem_wait_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
// Incrementa el valor del semáforo (signal)
static int sem_signal_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}
/* --------------------------------------------------------------------------
   Función: print_table
   Muestra de manera visual los datos insertados en la memoria compartida.
   Incluye color ANSI, encabezado y fecha de inserción.
   -------------------------------------------------------------------------- */
static void print_table(int index, unsigned char c, time_t t) {
    printf("\033[1;34m---------------------------------------------\033[0m\n");
    printf("\033[1;32m| Índice | Valor ASCII | Hora de Inserción   |\033[0m\n");
    printf("\033[1;33m| %6d | %12u | %s\033[0m", index, c, ctime(&t));
    printf("\033[1;34m---------------------------------------------\033[0m\n");
}
/* --------------------------------------------------------------------------
   PROCESO PRINCIPAL DEL EMISOR
   Uso:
       ./emisor <id_memoria> <modo> <clave_xor>
       - id_memoria : identificador usado por ftok() (entero)
       - modo       : 0 = manual | 1 = automático
       - clave_xor  : valor entero de 8 bits para codificación XOR
   -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    // ============================================================
    // VALIDACIÓN DE PARÁMETROS
    // ============================================================
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <id_memoria> <modo> <clave_xor>\n", argv[0]);
        fprintf(stderr, "Modo: 0 = Manual | 1 = Automático\n");
        exit(EXIT_FAILURE);
    }
    
    // Generar la clave de memoria compartida (ftok)
    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); exit(EXIT_FAILURE); }

    int mode = atoi(argv[2]); // 0 = manual, 1 = automático
    int xor_key = atoi(argv[3]);

    // ============================================================
    // CONEXIÓN A LA MEMORIA Y SEMÁFOROS EXISTENTES
    // ============================================================
    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); exit(EXIT_FAILURE); }

    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (mem == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); exit(EXIT_FAILURE); }

    // ============================================================
    // ABRIR ARCHIVO FUENTE DEFINIDO EN LA MEMORIA
    // ============================================================
    FILE *fp = fopen(mem->fuente_path, "rb");
    if (!fp) { perror("fopen fuente"); shmdt(mem); exit(EXIT_FAILURE); }

   // ============================================================
    // REGISTRAR EMISOR ACTIVO Y TOTAL
    // (Protegido por el mutex)
    // ============================================================)
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

    // ============================================================
    // BUCLE PRINCIPAL DE ENVÍO DE DATOS
    // ------------------------------------------------------------
    // Cada iteración:
    //  1) Reserva posición atómica (next_pos)
    //  2) Lee un byte del archivo fuente
    //  3) Escribe en el buffer circular (con XOR)
    //  4) Imprime información y respeta modo de ejecución
    // ============================================================
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

        // Inserción segura en la posición actual del buffer
        int idx = mem->write_index;
        mem->buffer[idx].ascii     = (char)(c ^ xor_key);
        mem->buffer[idx].index     = idx;
        mem->buffer[idx].timestamp = time(NULL);
        mem->buffer[idx].is_full   = 1;
        mem->buffer[idx].seq       = pos;

        mem->total_written++;  // Contador global de caracteres emitidos

        print_table(idx, (unsigned char)mem->buffer[idx].ascii, mem->buffer[idx].timestamp);

        // Actualización circular del índice de escritura
        mem->write_index = (idx + 1) % mem->size;
        mem->count++;

        // Liberar semáforos (salida de sección crítica)
        if (sem_signal_raw(sem_id, 0) == -1) { // mutex++
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock write). Saliendo emisor...\n"); break; }
            perror("semop signal mutex write"); break;
        }
        if (sem_signal_raw(sem_id, 2) == -1) { // full++
            if (errno==EIDRM || errno==EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (full++). Saliendo emisor...\n"); break; }
            perror("semop signal full"); break;
        }
        // 4) Control del modo de ejecucion
        if (mode == 0) {
            printf("\nPresione ENTER para enviar el siguiente carácter...\n");
            getchar();
        } else {
            struct timespec d = {0, 400000000L}; // 0.4 s
            nanosleep(&d, NULL);
        }
    }
/* ============================================================
       FINALIZACIÓN ELEGANTE
       ------------------------------------------------------------
       - Disminuye el contador de emisores activos.
       - Cierra archivos y libera recursos.
       ============================================================ */
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
