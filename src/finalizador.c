/*
 ============================================================================
 Archivo: Finalizador.c
 Proyecto: Comunicación de Procesos Sincronizada
 Curso: CE4303 - Principios de Sistemas Operativos
 Profesor: M.Sc. Jason Leitón Jiménez
 Fecha: 17-10-25
 Descripción:
    Este proceso (Finalizador) se encarga de cerrar el sistema de IPC
    de forma ordenada y sin uso de 'kill', tal como exige el enunciado.
    El cierre se activa por un evento "físico" (aquí: ENTER en consola),
    y luego:
      1) Espera a que el buffer compartido quede vacío (sin busy waiting).
      2) Toma un "snapshot" de estadísticas en memoria compartida.
      3) Imprime un resumen elegante y conciso.
      4) Libera los recursos IPC (memoria compartida y semáforos).

    Relación con el enunciado:
      - Accionamiento por señal externa y finalización normal. 
      - Mostrar estadísticas finales en formato elegante.               
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
   Utilidad: obtener el valor actual de un semáforo con semctl(GETVAL)
   -------------------------------------------------------------------------- */
static int sem_getval(int sem_id, int sem_num) {
    return semctl(sem_id, sem_num, GETVAL);
}

/* --------------------------------------------------------------------------
   Utilidad: pequeña espera
   -------------------------------------------------------------------------- */
static void tiny_sleep_ns(long ns) {
    struct timespec d = {0, ns};
    nanosleep(&d, NULL);
}

/* --------------------------------------------------------------------------
   PROCESO PRINCIPAL DEL FINALIZADOR
   Uso:
       ./finalizador <id_memoria>
   Donde:
       - id_memoria: entero base para ftok() que identifica el conjunto IPC.
   -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    // Validación de parámetros
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <id_memoria>\n", argv[0]);
        return 1;
    }

    // Derivar clave IPC a partir del id solicitado
    key_t shm_key = ftok(".", atoi(argv[1]));
    if (shm_key == (key_t)-1) { perror("ftok"); return 1; }

    // Anexarse a memoria y semáforos ya creados por el Inicializador
    int shm_id = shmget(shm_key, 0, 0666);
    if (shm_id == -1) { perror("shmget"); return 1; }

    SharedMemory *mem = (SharedMemory*)shmat(shm_id, NULL, 0);
    if (mem == (void*)-1) { perror("shmat"); return 1; }

    int sem_id = semget(shm_key, 3, 0666);
    if (sem_id == -1) { perror("semget"); shmdt(mem); return 1; }

    /* ==============================================================
       1) Señal externa de cierre (simulada con ENTER)
       --------------------------------------------------------------
       Equivale al “botón físico” pedido en el enunciado.             
       ============================================================== */
    printf("\n\033[1;34mFinalizador listo.\033[0m Presione ENTER para mostrar el resumen y liberar recursos...\n");
    getchar(); // Simula el “botón físico” de cierre

    /* ==============================================================
       2) Esperar a que el buffer esté vacío antes de cerrar
       --------------------------------------------------------------
       Se consulta el semáforo 'full' (idx=2). Si es >0, aún hay datos
       por consumir. Se duerme brevemente.
       ============================================================== */
    for (;;) {
        int full_val = sem_getval(sem_id, 2); // 'full' (espacios ocupados)
        if (full_val <= 0) break;             // buffer vacío
        tiny_sleep_ns(100000000L);            // 0.1 s
    }

    /* ==============================================================
       3) Tomar snapshot de estadísticas ANTES de desmontar IPC
       --------------------------------------------------------------
       El enunciado solicita reportar estos indicadores al final.     [Secc. 4.4]
       ============================================================== */
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

    /* ==============================================================
       4) Imprimir resumen final de manera elegante (colores/alineado)
       -------------------------------------------------------------- 
       ============================================================== */
    printf("\n\033[1;32m========== RESUMEN FINAL ==========\033[0m\n");
    printf("\033[1;33m- Cantidad de caracteres transferidos:   \033[0m%lld\n", transferidos);
    printf("\033[1;34m- Cantidad de caracteres en memoria:     \033[0m%d\n", count);
    printf("\033[1;35m- Emisores vivos / totales:              \033[0m%d / %d\n", e_act, e_tot);
    printf("\033[1;36m- Receptores vivos / totales:            \033[0m%d / %d\n", r_act, r_tot);
    printf("\033[1;37m- Memoria compartida utilizada:          \033[0m%zu bytes\n", bytes_mem);
    printf("\033[1;32m===================================\033[0m\n");

    /* ==============================================================
       5) Liberación ordenada de recursos IPC
       --------------------------------------------------------------
       - Desacoplar memoria del proceso (shmdt)
       - Marcar la memoria compartida para destrucción (IPC_RMID)
       - Remover el conjunto de semáforos (IPC_RMID)
       Los emisores/receptores están programados para detectar EIDRM/
       EINVAL y cerrarse en forma “normal” (sin kill).                
       ============================================================== */
    shmdt(mem);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    printf("\n\033[1;34mCierre completado.\033[0m Recursos liberados correctamente.\n");

    return 0;
}
