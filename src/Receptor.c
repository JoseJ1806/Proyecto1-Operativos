/*
 ============================================================================
 Archivo: Receptor.c
 Proyecto: Comunicación de Procesos Sincronizada
 Curso: CE4303 - Principios de Sistemas Operativos
 Profesor: M.Sc. Jason Leitón Jiménez
 Fecha: 17-10-25
 Descripción:
    Este proceso (Receptor) tiene la función de leer los datos producidos por
    uno o varios emisores desde la memoria compartida, decodificarlos con la
    misma clave XOR utilizada para codificación, y escribirlos en un archivo
    de salida de manera ordenada y sincronizada.

    Según la descripción del proyecto:
      - El receptor debe leer de forma circular los valores de la estructura.
      - No puede usar busy waiting; debe bloquearse si no hay datos (full = 0).
      - Debe mostrar en consola cada carácter leído (en tiempo real).
      - Debe reconstruir colaborativamente el archivo de salida.
      - Puede haber múltiples receptores simultáneos.
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
   Funciones auxiliares para manejo de semáforos
   -------------------------------------------------------------------------- */

// Disminuye el valor del semáforo (bloquea si es necesario)
static int sem_wait_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
// Incrementa el valor del semáforo (libera recurso)
static int sem_signal_raw(int sem_id, int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}


/* --------------------------------------------------------------------------
   Función: print_table
   Muestra de manera elegante la información de cada carácter leído.
   Incluye color, índice, carácter decodificado y hora de inserción.
   -------------------------------------------------------------------------- */
static void print_table(int index, char c_dec, time_t t_ins) {
    printf("\033[1;35m---------------------------------------------\033[0m\n");
    printf("\033[1;36m| Índice | Carácter | Hora de Inserción     |\033[0m\n");
    printf("\033[1;33m| %6d | %8c | %s\033[0m",
           index, (c_dec >= 32 && c_dec <= 126) ? c_dec : '?', ctime(&t_ins));
    printf("\033[1;35m---------------------------------------------\033[0m\n");
}

/* --------------------------------------------------------------------------
   Función: tiny_sleep_ns
   Breve suspensión en nanosegundos para reintentos ordenados.
   Utilizada cuando el receptor espera su turno de escritura.
   -------------------------------------------------------------------------- */
static void tiny_sleep_ns(long ns) {
    struct timespec d = {0, ns};
    nanosleep(&d, NULL);
}

/* --------------------------------------------------------------------------
   PROCESO PRINCIPAL DEL RECEPTOR
   Uso:
       ./receptor <id_memoria> <modo> <clave_xor> <archivo_salida>
       - id_memoria     : identificador usado por ftok() (entero)
       - modo           : 0 = manual | 1 = automático
       - clave_xor      : clave de decodificación XOR
       - archivo_salida : nombre del archivo reconstruido
   -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    /* ==============================================================
       VALIDACIÓN DE PARÁMETROS
       ============================================================== */
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <id_memoria> <modo(0|1)> <clave_xor> <archivo_salida>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); exit(EXIT_FAILURE); }

    int mode     = atoi(argv[2]);  // 0 manual, 1 automático
    int xor_key  = atoi(argv[3]);
    const char *out_path = argv[4];
    
     /* ==============================================================
       CONEXIÓN A LA MEMORIA COMPARTIDA Y SEMÁFOROS EXISTENTES
       ============================================================== */
    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); exit(EXIT_FAILURE); }

    SharedMemory *mem = (SharedMemory*)shmat(shm_id, NULL, 0);
    if (mem == (void*)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); exit(EXIT_FAILURE); }

    /* ==============================================================
       REGISTRO DE RECEPTOR ACTIVO Y TOTAL (protegido con mutex)
       ============================================================== */
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

    /* ==============================================================
       APERTURA DE ARCHIVO DE SALIDA
       Todos los receptores escriben en modo "append",
       pero solo lo hacen cuando es su turno.
       ============================================================== */
    FILE *fout = fopen(out_path, "a");
    if (!fout) { perror("fopen salida"); goto graceful_exit; }

    printf("\nReceptor iniciado (modo %s). Escribiendo colaborativamente en: %s\n",
           mode==1 ? "automático" : "manual", out_path);

    /* ==============================================================
       BUCLE PRINCIPAL DE LECTURA Y DECODIFICACIÓN
       --------------------------------------------------------------
       1) Espera hasta que haya datos (semáforo full)
       2) Entra en sección crítica (mutex)
       3) Extrae el carácter y actualiza índices
       4) Decodifica y muestra en consola
       5) Escribe en el archivo cuando corresponda (turno)
       ============================================================== */
    for (;;) {
        // Esperar a que exista al menos un dato disponible
        if (sem_wait_raw(sem_id, 2) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (full). Saliendo receptor...\n"); break; }
            perror("semop wait full"); break;
        }
        // Entrar a la sección crítica
        if (sem_wait_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex). Saliendo receptor...\n"); break; }
            perror("semop wait mutex"); break;
        }

        // Leer el carácter del buffer circular
        int idx = mem->read_index;
        SharedChar sc = mem->buffer[idx];
        mem->buffer[idx].is_full = 0;                //Marcar espacio vacio
        mem->read_index = (idx + 1) % mem->size;     //Avance Circular
        if (mem->count > 0) mem->count--;            //Decrementar contador


        // Liberar la sección crítica y avisar que hay espacio libre
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock). Saliendo receptor...\n"); break; }
            perror("semop signal mutex"); break;
        }
        if (sem_signal_raw(sem_id, 1) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (empty++). Saliendo receptor...\n"); break; }
            perror("semop signal empty"); break;
        }

        // Decodificar el carácter leído mediante XOR
        char c_dec = (char)((unsigned char)sc.ascii ^ (unsigned char)xor_key);

        // Contabilizar el carácter consumido (bloque corto protegido)
        if (sem_wait_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex stats). Saliendo receptor...\n"); break; }
            perror("semop wait mutex stats"); break;
        }
        mem->total_consumed++;
        if (sem_signal_raw(sem_id, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (unlock stats). Saliendo receptor...\n"); break; }
            perror("semop signal mutex stats"); break;
        }

        // Mostrar en consola en tiempo real
        print_table(sc.index, c_dec, sc.timestamp);
        putchar(c_dec);
        fflush(stdout);

       /* ----------------------------------------------------------
           Escritura colaborativa:
           Cada receptor espera a que su turno (seq == next_to_flush)
           para escribir en el archivo en orden secuencial.
           ---------------------------------------------------------- */
        for (;;) {
            if (sem_wait_raw(sem_id, 0) == -1) {
                if (errno == EIDRM || errno == EINVAL) { fprintf(stderr, "\n[INFO] IPC retirados (mutex flush). Saliendo receptor...\n"); goto end_loop; }
                perror("semop wait mutex flush"); goto end_loop;
            }
            long long expected = mem->next_to_flush;
            if (sc.seq == expected) {
                // Determina si es su turno para escribir y avanza
                if (fputc(c_dec, fout) == EOF) perror("fputc");
                fflush(fout);
                mem->next_to_flush = expected + 1;
                if (sem_signal_raw(sem_id, 0) == -1) {
                    if (!(errno == EIDRM || errno == EINVAL)) perror("semop signal mutex flush");
                }
                break; //Listo el caracter
            }
            // No es el turno aun: libera el mutex y espera un rato
            if (sem_signal_raw(sem_id, 0) == -1) {
                if (!(errno == EIDRM || errno == EINVAL)) perror("semop signal mutex flush (not yet)");
            }
            tiny_sleep_ns(50000000L); // 50 ms
        }

        // Control de modo de ejecucion
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
    /* ==============================================================
       FINALIZACIÓN ELEGANTE DEL RECEPTOR
       --------------------------------------------------------------
       - Decrementa contadores activos
       - Libera recursos compartidos
       ============================================================== */
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
