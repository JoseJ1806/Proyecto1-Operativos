/*
 ============================================================================
 Archivo: Inicializador.c
 Proyecto: Comunicación de Procesos Sincronizada
 Curso: CE4303 - Principios de Sistemas Operativos
 Profesor: M.Sc. Jason Leitón Jiménez
 Fecha: 17-10-25
 Descripción:
    Este proceso (Inicializador) es responsable de crear y configurar el
    entorno compartido para la comunicación entre procesos pesados (heavy
    process). Define la memoria compartida, el tamaño del buffer circular,
    los semáforos de control (mutex, empty, full) y registra la información
    inicial del archivo fuente y parámetros de codificación XOR.
      - Establecer los espacios compartidos (memoria y semáforos).
      - Indicar el archivo fuente de caracteres.
      - Finalizar una vez creada la memoria, sin mantener procesos activos.
 ============================================================================
*/
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
   Estructura requerida por semctl() para inicializar semáforos
   -------------------------------------------------------------------------- */
union semun {
    int val;                    //Valor para Setval
    struct semid_ds *buf;       //Buffer para IPC_STAT e IPC_SET
    unsigned short *array;      //Arreglo para SETALL
};
/* --------------------------------------------------------------------------
   Función principal del Inicializador
   Parámetros esperados:
     argv[1] -> ID de memoria compartida (entero)
     argv[2] -> Tamaño del buffer circular (entero)
     argv[3] -> Clave XOR para codificación (entero)
     argv[4] -> Ruta del archivo fuente (texto)
   -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    /* ==============================================================
       VALIDACIÓN DE PARÁMETROS
       ============================================================== */
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <id_memoria> <tamano_buffer> <clave_xor> <archivo_fuente>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* ==============================================================
       CONVERSIÓN Y LECTURA DE PARÁMETROS
       ============================================================== */
    key_t shm_key = ftok(".", atoi(argv[1]));  // Genera la clave IPC a partir del ID
    int size = atoi(argv[2]);                  // Define el número de posiciones del buffer
    int xor_key = atoi(argv[3]);               // Clave XOR 
    char *filename = argv[4];                  // Archivo de texto fuente

    /* ==============================================================
       CREACIÓN DE LA MEMORIA COMPARTIDA
       ============================================================== */
    int shm_id = shmget(shm_key, 
                        sizeof(SharedMemory) + size * sizeof(SharedChar), 
                        IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error al crear memoria compartida");
        exit(EXIT_FAILURE);
    }

    /* ==============================================================
       VINCULACIÓN DE LA MEMORIA AL ESPACIO DEL PROCESO
       ============================================================== */
    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (mem == (void *)-1) {
        perror("Error al adjuntar memoria compartida");
        exit(EXIT_FAILURE);
    }

    /* ==============================================================
       INICIALIZACIÓN DE LA ESTRUCTURA DE CONTROL
       --------------------------------------------------------------
       - Se define tamaño, punteros de lectura y escritura.
       - Se limpia el buffer marcando cada espacio como vacío.
       ============================================================== */
    mem->size = size;
    mem->write_index = 0;
    mem->read_index = 0;
    mem->count = 0;
    mem->next_pos = 0;
    mem->next_to_flush = 0;

    // Guardar la ruta del archivo fuente de manera segura
    strncpy(mem->fuente_path, filename, sizeof(mem->fuente_path)-1);
    mem->fuente_path[sizeof(mem->fuente_path)-1] = '\0';

    // Inicializar los espacios del buffer como vacíos
    for (int i = 0; i < size; i++) {
        mem->buffer[i].is_full = 0;
    }

    /* ==============================================================
       CREACIÓN E INICIALIZACIÓN DE LOS SEMÁFOROS
       --------------------------------------------------------------
       - mutex: controla acceso exclusivo a la sección crítica
       - empty: controla espacios vacíos disponibles
       - full: controla espacios llenos listos para lectura
       ============================================================== */
    int sem_id = semget(shm_key, 3, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Error al crear semáforos");
        exit(EXIT_FAILURE);
    }

    // Inicialización de los semáforos
    union semun arg;
    unsigned short values[3] = {1, size, 0}; // mutex=1, empty=size, full=0
    arg.array = values;
    
    if (semctl(sem_id, 0, SETALL, arg) == -1) {
        perror("Error al inicializar semáforos");
        exit(EXIT_FAILURE);
    }

    /* ==============================================================
       SALIDA DE INFORMACION
       --------------------------------------------------------------
       Se muestra al usuario el entorno inicializado correctamente.
       ============================================================== */
    printf("\n Memoria compartida inicializada correctamente.\n");
    printf("ID memoria: %d\n", shm_id);
    printf("Clave XOR: %d\n", xor_key);
    printf("Archivo fuente: %s\n", filename);
    printf("Tamaño del buffer: %d caracteres\n", size);

    /* ==============================================================
       DESVINCULACIÓN FINAL
       --------------------------------------------------------------
       El inicializador no mantiene ejecución activa.
       ============================================================== */
    shmdt(mem);
    return 0;
}
