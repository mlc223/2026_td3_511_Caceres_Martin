#include <stdio.h>
#include "freertos/freeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "lcd_i2c.h"

QueueHandle_t xColaEntrada=NULL;
QueueHandle_t xColaBuffer=NULL;
QueueHandle_t xColaPwm=NULL;
QueueHandle_t xColaSpi=NULL;
QueueHandle_t xColaEstado=NULL;
SemaphoreHandle_t xSemaforoSincro=NULL;//Sincroniza la tarea display con la tarea flash


typedef enum {
    SENOIDAL,
    TRIANGULAR,
    CUADRADA
} tipoOnda;

typedef enum {ESCALA_x1,ESCALA_x10,ESCALA_x100,ESCALA_x1000} tipoEscala;

struct DatoEntrada
{
    int32_t frec;  // UART: Freq absoluta (Hz) | SINTONIA: Delta Freq (+/- Hz)
    float amplitud;  // UART: Amplitud absoluta (Vpp) | SINTONIA: Delta Amp (+/- V)
    float offset;      // UART: Off absoluto (V)   | SINTONIA: Delta Off (+/- V)
    bool origen;    // true = UART (Absoluto)   | false = SINTONIA (Relativo/Incrementos)
    tipoOnda tipo;  // UART: Tipo directo       | SINTONIA: Tipo directo o actual
    tipoEscala mfrec;  //UART: NO CARGA         | SINTONIA: Escala para la frecuencia
    tipoEscala mamp;   //UART: NO CARGA         | SINTONIA: Escala para la amplitud 
};

struct DatoBuffer
{
    uint32_t frec;
    tipoOnda tipo;
};

struct DatoPwm
{    
    float offset;
};

struct DatoSpi
{
    float amplitud;
};


struct DatoEstado
{
    uint32_t frec;
    float amplitud;
    float offset;
    tipoOnda tipo;
    tipoEscala mfrec;
    tipoEscala mamp;
};

//Tarea Display prioridad 1//
void tarea_Display(void *pvParameters){
    struct DatoEstado EstadoVisual;
    //1. INICIALIZACION DEL HARDWARE
    lcd_inicializar_secuencia();
    
    char bufferfila1[32];// Buffer para la primera fila del display
    char bufferfila2[32];
    while(1){
        //2. ESPERA DE DATOS DE LA COLA DE ESTADO, SIN DESTRUIR EL DATO
        if(xQueuePeek(xColaEstado, &EstadoVisual, portMAX_DELAY)==pdTRUE)// Se utiliza QueuePeek para leer el dato sin eliminarlo de la cola
        {
            //3. CONVERTIR DE ENUMS A STRINGS PARA MOSTRAR EN EL DISPLAY
            // Se utiliza el operador ternario para convertir el tipo de onda a string, ancho de 3 caracteres fijo
            const char* strOnda=(EstadoVisual.tipo == SENOIDAL) ? "SEN" :
                            (EstadoVisual.tipo == TRIANGULAR) ? "TRI" :
                            (EstadoVisual.tipo == CUADRADA) ? "CUA" : "UNK";

            //Ancho fijo de 4 caracteres para la frecuencia
            const char* strEscalaFrec=(EstadoVisual.mfrec == ESCALA_x1) ? "x1  " :
                            (EstadoVisual.mfrec == ESCALA_x10) ? "x10 " :
                            (EstadoVisual.mfrec == ESCALA_x100) ? "x100" :
                            (EstadoVisual.mfrec == ESCALA_x1000) ? "x1K " : "UNK ";

            //Ancho fijo de 3 caracteres para la amplitud
            const char* strEscalaAmp=(EstadoVisual.mamp == ESCALA_x1) ? "1m " :
                            (EstadoVisual.mamp == ESCALA_x10) ? "10m" :
                            (EstadoVisual.mamp == ESCALA_x100) ? ".1V" :
                            (EstadoVisual.mamp == ESCALA_x1000) ? "1V " : "UNK";

            //4. FORMATEO DE LOS DATOS PARA MOSTRAR EN EL DISPLAY
            snprintf(bufferfila1, sizeof(bufferfila1), "%sF:%-5luHz%s", strOnda, EstadoVisual.frec, strEscalaFrec);// Formatea la primera fila del display con el tipo de onda, frecuencia y escala de frecuencia

            snprintf(bufferfila2, sizeof(bufferfila2), "A:%.1fV %-3s O:%+.1fV", EstadoVisual.amplitud, strEscalaAmp, EstadoVisual.offset);// Formatea la segunda fila del display con la amplitud y escala de amplitud y el offset

            //5. ENVIO DE LOS DATOS AL DISPLAY

            lcd_ir_a(0, 0); // Mueve el cursor a la primera fila, primera columna
            lcd_imprimir_cadena(bufferfila1); // Imprime la primera fila en el display
            lcd_ir_a(1, 0); // Mueve el cursor a la segunda fila, primera columna
            lcd_imprimir_cadena(bufferfila2); // Imprime la segunda fila en el display

            vTaskDelay(pdMS_TO_TICKS(50)); // Se agrega un pequeño retardo para evitar parpadeos en el display

            //2. LIBERACION DEL SEMAFORO PARA LA TAREA FLASH
            xSemaphoreGive(xSemaforoSincro); // Se libera el semaforo para que la tarea flash pueda guardar el estado en la memoria flash NVS
            vTaskDelay(pdMS_TO_TICKS(10)); // Se agrega retardo para evitar que la tarea display vuelva a tomar el dato de la cola
                                            //y Flash no pueda accionar
        }
    }

}

//Tarea Flash prioridad 1//
void tarea_Flash(void *pvParameters){
    struct DatoEstado DatosUltimos;
    struct DatoEstado DatosNuevos;
    nvs_handle_t mem_flash;

    TickType_t TiempoUltimoCambio = xTaskGetTickCount();
    bool cambio_detectado = false;

   while(1){
   TickType_t TiempoEspera = (cambio_detectado) ? pdMS_TO_TICKS(100) : portMAX_DELAY; // 
    //1. ESPERA DE DATOS DE LA COLA DE ESTADO, SIN DESTRUIR EL DATO
    if(xQueuePeek(xColaEstado, &DatosNuevos, TiempoEspera))// Se utiliza QueuePeek para leer el dato sin eliminarlo de la cola
    {
        //2. COORDINACION POR SEMAFORO
        if(!cambio_detectado){
            //Se bloquea esperando a que la tarea display termine y libere el semaforo
            if(xSemaphoreTake(xSemaforoSincro, portMAX_DELAY) == pdTRUE)// Se utiliza QueuePeek para leer el dato sin eliminarlo de la cola
            {
                DatosUltimos = DatosNuevos; // Se actualiza el estado anterior con el estado actual para comparar cambios
                TiempoUltimoCambio = xTaskGetTickCount();
                cambio_detectado = true;
            }
        } 
    else{
        if(DatosUltimos.frec != DatosNuevos.frec || DatosUltimos.amplitud != DatosNuevos.amplitud || DatosUltimos.offset != DatosNuevos.offset || DatosUltimos.tipo != DatosNuevos.tipo || DatosUltimos.mfrec != DatosNuevos.mfrec || DatosUltimos.mamp != DatosNuevos.mamp)
            {
            //Si hay cambios en los datos, se actualiza el estado anterior con el estado actual para comparar cambios
            DatosUltimos = DatosNuevos;
            TiempoUltimoCambio = xTaskGetTickCount();
            printf("FLASH: Cambio detectado, reinicio del cronometro de guardado en flash.\n");
            }
        
        }
    }
//Evaluo el tiempo fuera del if del QueuePeek
//si cambio_detectado es true y pasaron 100ms de timeout del peek sin cambios nuevos 
//vengo a verificar si se cumplieron los 2 segundos para guardar en flash
    if(cambio_detectado && (xTaskGetTickCount() - TiempoUltimoCambio >= pdMS_TO_TICKS(2000)))
    {
        //3. ADAPTACION DE LOS DATOS PARA GUARDAR EN FLASH
        
        uint32_t frec_guardada = DatosNuevos.frec;
        uint32_t amplitud_guardada = (uint32_t)(DatosNuevos.amplitud * 1000); // Se convierte de float a uint32_t multiplicando por 1000 para guardar los decimales
        uint32_t offset_guardado = (uint32_t)((DatosNuevos.offset * 1000 )+ 2500.0f); // Se convierte de float a uint32_t multiplicando por 1000 y sumando 2500 para guardar los decimales y evitar negativos
        uint8_t tipo_guardado = (uint8_t)DatosNuevos.tipo;
        uint8_t mfrec_guardado = (uint8_t)DatosNuevos.mfrec;
        uint8_t mamp_guardado = (uint8_t)DatosNuevos.mamp;
        //4. GUARDADO EN FLASH
            if (nvs_open("ultimo_estado", NVS_READWRITE, &mem_flash) == ESP_OK) // Se abre la memoria flash NVS en modo lectura/escritura
                {
                    nvs_set_u32(mem_flash, "frec", frec_guardada);
                    nvs_set_u32(mem_flash, "amplitud", amplitud_guardada);
                    nvs_set_u32(mem_flash, "offset", offset_guardado);
                    nvs_set_u8(mem_flash, "tipo", tipo_guardado);
                    nvs_set_u8(mem_flash, "mfrec", mfrec_guardado);
                    nvs_set_u8(mem_flash, "mamp", mamp_guardado);

                    nvs_commit(mem_flash); // Se confirma el guardado en la memoria flash NVS
                    nvs_close(mem_flash); // Se cierra la memoria flash NVS
                    printf("FLASH: Estado guardado en NVS.\n");
                }
                else{
                        printf("FLASH: Error al abrir la memoria flash NVS.\n");
                    }
                //4 LIMPIO EL DATO
                struct DatoEstado DatoVacio;
                xQueueReceive(xColaEstado, &DatoVacio, 0); // Se utiliza QueueReceive para eliminar el dato de la cola sin bloquear
                //5. REINICIO DE LA BANDERA DE CAMBIO DETECTADO
                cambio_detectado = false;// Se reinicia la bandera de cambio detectado para volver a esperar cambios en el estado   
                }       

                   
        }
    }



//Tarea Dato prioridad 3//

void tarea_Dato(void *pvParameters){
    
    //1. VALORES INICIALES
    //primer arranque del programa con la FLASH vacía o no se pudo leer datos, estado inicial
    struct DatoEstado EstadoActual = {
        .frec= 1000,
        .amplitud= 2.5f,
        .offset= 0.0f,
        .tipo= SENOIDAL,
        .mfrec= ESCALA_x1,
        .mamp= ESCALA_x1        
    };
    //2.RECUPERA EL DATO DE LA MEMORIA FLASH NVS
    nvs_handle_t mem_flash;
    if (nvs_open("ultimo_estado", NVS_READONLY, &mem_flash) != ESP_OK) // Si no se pudo abrir la memoria flash, se mantiene el estado inicial  
    {
        //las variables deben ser uint32_t, La librería nvs_flash de ESP-IDF no tiene funciones para leer ni escribir float
        uint32_t frec_guardada=0, amplitud_guardada=0, offset_guardado=0;// Se guardan como enteros, luego se convierten a flotantes
        uint8_t tipo_leida=0, mfrec_leida=0, mamp_leida=0;

        esp_err_t e1 = nvs_get_u32(mem_flash, "frec", &frec_guardada);
        esp_err_t e2 = nvs_get_u32(mem_flash, "amplitud", &amplitud_guardada);
        esp_err_t e3 = nvs_get_u32(mem_flash, "offset", &offset_guardado);
        esp_err_t e4 = nvs_get_u8(mem_flash, "tipo", &tipo_leida);
        esp_err_t e5 = nvs_get_u8(mem_flash, "mfrec", &mfrec_leida);
        esp_err_t e6 = nvs_get_u8(mem_flash, "mamp", &mamp_leida);  
        if (e1 == ESP_OK && e2 == ESP_OK && e3 == ESP_OK && e4 == ESP_OK && e5 == ESP_OK && e6 == ESP_OK)
        {
            EstadoActual.frec = frec_guardada;
            EstadoActual.amplitud = (float)amplitud_guardada/1000.0f; // Se convierte de uint32_t a float dividir por 1000 para recuperar los decimales
            EstadoActual.offset = ((float)offset_guardado - 2500.0f)/1000.0f; // Se convierte de uint32_t a float y se ajusta el offset restando 2500 y dividiendo por 1000 para recuperar los decimales
            EstadoActual.tipo = (tipoOnda)tipo_leida;
            EstadoActual.mfrec = (tipoEscala)mfrec_leida;   
            EstadoActual.mamp = (tipoEscala)mamp_leida; 
            printf("DATO: Estado y escalas recuperadas desde NVS.\n");
        }
        nvs_close(mem_flash);//cierra la memoria flash
    }
    //3. SINCRONIZACIÓN INICIAL DE LAS COLAS DE DATOS
    struct DatoBuffer buffer_inicial = {
        .frec = EstadoActual.frec,
        .tipo = EstadoActual.tipo
    };
    struct DatoPwm pwm_inicial = {
        .offset = EstadoActual.offset
    };
    struct DatoSpi spi_inicial = {
        .amplitud = EstadoActual.amplitud
    };      

    xQueueSend(xColaBuffer, &buffer_inicial, portMAX_DELAY);// Se utiliza QueueSend para mantener el primer dato en la cola
    xQueueSend(xColaPwm, &pwm_inicial, portMAX_DELAY);// portMAX_DELAY indica que la tarea se bloqueará hasta que haya espacio en la cola para enviar el dato,por seguridad 
    xQueueSend(xColaSpi, &spi_inicial, portMAX_DELAY);//Al enviar el primer dato, las colas estan vacías

    xQueueOverwrite(xColaEstado, &EstadoActual); // Se utiliza QueueOverwrite para mantener siempre el último estado

    struct DatoEntrada dato_entrada;// Variable para recibir datos de la cola de entrada
    printf("DATO: Tarea Dato iniciada.\n");

    //4. BUCLE PRINCIPAL DE LA TAREA
    while (1){
        if(xQueueReceive(xColaEntrada, &dato_entrada, pdMS_TO_TICKS(100)))// Se utiliza QueueReceive para recibir datos de la cola de entrada sin bloquear,espera 100 ms para recibir un dato, si no hay datos, se sigue con el bucle
        {
            bool cambio_señal = false; // Bandera para indicar que hubo un cambio en el estado como minimo habra que actualizar el display y reiniciar el cronometro de guardado en flash
            bool cambio_buffer = false; // Bandera para indicar que hubo un cambio en la frecuencia o el tipo de onda, habra que actualizar el buffer de la señal
            bool cambio_offset = false; // Bandera para indicar que hubo un cambio en el offset actualizara el PWM
            bool cambio_amplitud = false; // Bandera para indicar que hubo un cambio en la amplitud actualizara el SPI


            if (dato_entrada.origen==false)// si el dato proviene de SINTONIA, verifico si hubo cambios en las escalas de frecuencia y amplitud   
            {
                if( EstadoActual.mamp != dato_entrada.mamp)
                {
                    EstadoActual.mamp = dato_entrada.mamp;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    printf("DATO: Cambio de escala de amplitud a %d\n", EstadoActual.mamp);
                }
                if( EstadoActual.mfrec != dato_entrada.mfrec)
                {
                    EstadoActual.mfrec = dato_entrada.mfrec;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    printf("DATO: Cambio de escala de frecuencia a %d\n", EstadoActual.mfrec);
                }
                
                
            }
            if (dato_entrada.origen==false)// Si el dato proviene de SINTONIA, INCREMENTOS RELATIVOS 
            {
                if (dato_entrada.frec != 0)
                {
                    int32_t NuevaFrec= (int32_t)EstadoActual.frec + dato_entrada.frec; // Se suma el incremento o decremento de frecuencia al valor actual
                    if (NuevaFrec < 1) // Se asegura que la frecuencia no sea menor a 1 Hz
                    {
                        NuevaFrec = 1;
                    }
                    if( NuevaFrec > 20000) // Se asegura que la frecuencia no sea mayor a 20 kHz
                    {
                        NuevaFrec = 20000;
                    }
                    if(EstadoActual.frec != (uint32_t)NuevaFrec) // Se verifica si hubo un cambio en la frecuencia
                    {
                        EstadoActual.frec = (uint32_t)NuevaFrec;
                        cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                        cambio_buffer = true; // Habra que actualizar el buffer de la señal
                        printf("DATO: Cambio de frecuencia a %ld Hz\n", EstadoActual.frec);
                    }   
                }
                if(dato_entrada.amplitud != 0.0f)
                {
                    float NuevaAmp= EstadoActual.amplitud + dato_entrada.amplitud; // Se suma el incremento o decremento de amplitud al valor actual
                    if (NuevaAmp < 0.1f) // Se asegura que la amplitud no sea menor a 0.1 Vpp
                    {
                        NuevaAmp = 0.1f;
                    }
                    if( NuevaAmp > 5.0f) // Se asegura que la amplitud no sea mayor a 5 Vpp
                    {
                        NuevaAmp = 5.0f;
                    }
                    if(EstadoActual.amplitud != NuevaAmp) // Se verifica si hubo un cambio en la amplitud
                    {
                        EstadoActual.amplitud = NuevaAmp;
                        cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                        cambio_amplitud = true; // Habra que actualizar el SPI
                        printf("DATO: Cambio de amplitud a %.2f Vpp\n", EstadoActual.amplitud);
                    }   
                }
                if(dato_entrada.offset != 0.0f)
                {
                    float NuevoOffset= EstadoActual.offset + dato_entrada.offset; // Se suma el incremento o decremento de offset al valor actual
                    if (NuevoOffset < -2.5f) // Se asegura que el offset no sea menor a -2.5 V
                    {
                        NuevoOffset = -2.5f;
                    }
                    if( NuevoOffset > 2.5f) // Se asegura que el offset no sea mayor a 2.5 V
                    {
                        NuevoOffset = 2.5f;
                    }
                    if(EstadoActual.offset != NuevoOffset) // Se verifica si hubo un cambio en el offset
                    {
                        EstadoActual.offset = NuevoOffset;
                        cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                        cambio_offset = true; // Habra que actualizar el SPI
                        printf("DATO: Cambio de offset a %.2f V\n", EstadoActual.offset);
                    }   
                }
                if(dato_entrada.tipo != EstadoActual.tipo)
                {
                    EstadoActual.tipo = dato_entrada.tipo;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    cambio_buffer = true; // Habra que actualizar el buffer de la señal
                    printf("DATO: Cambio de tipo de onda a %d\n", EstadoActual.tipo);
                }
            }
            else // Si el dato proviene de UART, DATOS ABSOLUTOS
            {
                if(dato_entrada.frec >= 1 && dato_entrada.frec<=20000 && dato_entrada.amplitud >= 0.1f && dato_entrada.amplitud <= 5.0f && dato_entrada.offset >= -2.5f && dato_entrada.offset <= 2.5f) // Se asegura que los datos estén dentro de los rangos válidos, sino descarto el dato      
                {
                     if(EstadoActual.frec != (uint32_t)dato_entrada.frec)
                 {
                    EstadoActual.frec = (uint32_t)dato_entrada.frec;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    cambio_buffer = true; // Habra que actualizar el buffer de la señal
                    printf("DATO: Cambio de frecuencia a %lu Hz\n", EstadoActual.frec);
                 }
                if(EstadoActual.amplitud != dato_entrada.amplitud)
                 {
                    EstadoActual.amplitud = dato_entrada.amplitud;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    cambio_amplitud = true; // Habra que actualizar el SPI
                    printf("DATO: Cambio de amplitud a %.2f Vpp\n", EstadoActual.amplitud);
                 }
                if(EstadoActual.offset != dato_entrada.offset)
                  {
                    EstadoActual.offset = dato_entrada.offset;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    cambio_offset = true; // Habra que actualizar el PWM
                    printf("DATO: Cambio de offset a %.2f V\n", EstadoActual.offset);
                  }
                if(EstadoActual.tipo != dato_entrada.tipo)
                    {
                    EstadoActual.tipo = dato_entrada.tipo;
                    cambio_señal = true; // Habra que actualizar el display y reiniciar el cronometro de guardado en flash
                    cambio_buffer = true; // Habra que actualizar el buffer de la señal
                    printf("DATO: Cambio de tipo de onda a %d\n", EstadoActual.tipo);
                    }
                }
               
             
            }
            //ENVIOS DE DATOS A LAS COLAS DE SALIDA SEGÚN LOS CAMBIOS DETECTADOS
            if(cambio_señal)
            {
                if(cambio_buffer)
                {
                    struct DatoBuffer buffer_actual = {
                        .frec = EstadoActual.frec,
                        .tipo = EstadoActual.tipo
                    };
                    xQueueOverwrite(xColaBuffer, &buffer_actual); // Se utiliza QueueOverwrite para mantener siempre el último dato en la cola
                }
                if(cambio_amplitud)
                {
                    struct DatoSpi spi_actual = {
                        .amplitud = EstadoActual.amplitud
                    };
                    xQueueOverwrite(xColaSpi, &spi_actual); // Se utiliza QueueOverwrite para mantener siempre el último dato en la cola
                }
                if(cambio_offset)
                {
                    struct DatoPwm pwm_actual = {
                        .offset = EstadoActual.offset
                    };
                    xQueueOverwrite(xColaPwm, &pwm_actual); // Se utiliza QueueOverwrite para mantener siempre el último dato en la cola
                }
                xQueueOverwrite(xColaEstado, &EstadoActual); // Se utiliza QueueOverwrite para mantener siempre el último estado
            }

        }
        
        
    }


} 



void app_main(void)
{
    // INICIALIZACIÓN DE LA MEMORIA FLASH NVS
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    printf("MAIN: NVS Flash no tiene paginas libres o hay una nueva version, se procede a borrar y reinicializar.\n");
    ESP_ERROR_CHECK(nvs_flash_erase());// Borra la memoria flash NVS 
    ret = nvs_flash_init(); // Reinicializa la memoria flash NVS
    }
    ESP_ERROR_CHECK(ret);

    xColaEntrada = xQueueCreate(10, sizeof(struct DatoEntrada));
    xColaBuffer = xQueueCreate(1, sizeof(struct DatoBuffer));
    xColaPwm = xQueueCreate(1, sizeof(struct DatoPwm));
    xColaSpi = xQueueCreate(1, sizeof(struct DatoSpi));
    xColaEstado = xQueueCreate(1, sizeof(struct DatoEstado));
    
    if( xColaEntrada == NULL )
    {
        printf("MAIN: Error al crear las colas de datos.\n");
        return;
    }
    if( xColaBuffer == NULL )
    {
        printf("MAIN: Error al crear la cola de buffer.\n");
        return;
    }
    if( xColaPwm == NULL )
    {
        printf("MAIN: Error al crear la cola de PWM.\n");
        return;
    }
    if( xColaSpi == NULL )
    {
        printf("MAIN: Error al crear la cola de SPI.\n");
        return;
    }
    if( xColaEstado == NULL )
    {
        printf("MAIN: Error al crear la cola de estado.\n");
        return;
    }
    xSemaforoSincro = xSemaphoreCreateBinary();
    if( xSemaforoSincro == NULL )
    {
        printf("MAIN: Error al crear el semaforo de sincronizacion Display-Flash.\n");
        return;
    }

}
