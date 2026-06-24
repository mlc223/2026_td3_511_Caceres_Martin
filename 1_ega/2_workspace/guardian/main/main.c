#include <stdio.h>
#include "freertos/freeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
typedef enum {
    SENOIDAL,
    TRIANGULAR,
    CUADRADA
} tipoOnda;

struct DatoEntrada
{
    uint32_t freq;  // UART: Freq absoluta (Hz) | SINTONIA: Delta Freq (+/- Hz)
    float amplitud;  // UART: Amplitud absoluta (Vpp) | SINTONIA: Delta Amp (+/- V)
    float off;      // UART: Off absoluto (V)   | SINTONIA: Delta Off (+/- V)
    bool origen;    // true = UART (Absoluto)   | false = SINTONIA (Relativo/Incrementos)
    tipoOnda tipo;  // UART: Tipo directo       | SINTONIA: Tipo directo o actual
};

struct DatoBuffer
{
    uint32_t freq;
    tipoOnda tipo;
};

struct DatoPwm
{
    float amplitud;
    float off;
};

struct DatoEstado
{
    uint32_t freq;
    float amplitud;
    float off;
    tipoOnda tipo;
};

QueueHandle_t xColaEntrada=NULL;
QueueHandle_t xColaBuffer=NULL;
QueueHandle_t xColaPwm=NULL;
QueueHandle_t xColaEstado=NULL;

//Tarea Dato prioridad 4//
void tarea_Dato(void *pvParameters){
    
    //1. VALORES INICIALES
    //primer arranque del programa con la FLASH vacía, estado inicial
    struct DatoEstado EstadoActual{
        .freq= 1000,
        .amplitud= 2.5f,
        .off= 0.0f,
        .tipo= SENOIDAL
    };
    //2.RECUPERA EL DATO DE LA MEMORIA FLASH NVS
    nvs_ha


} 



void app_main(void)
{

}