#include "lcd_i2c.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



i2c_master_bus_handle_t bus_handle = NULL;// Handle del bus I2C
i2c_master_dev_handle_t dev_handle = NULL;// Handle del dispositivo I2C

//1 INICIALIZACIÓN DEL BUS MAESTRO I2C
void inicializar_i2c_lcd_hardware (void){
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 16,
        .sda_io_num = 15,
        .glitch_ignore_cnt=7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));// Crea el bus I2C maestro

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x27,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));// Crea el dispositivo I2C
}
// 2 PROTOCOLO DE CONTROL EN 4 BITS PARA EL LCD
void lcd_enviar_nibble(uint8_t nibble, uint8_t modo) {
    uint8_t buffer_datos[3];
    //configuración de los bits de control: RS, RW, E y backlight
    buffer_datos[0] = (nibble & 0xF0) | (modo & 0x01) | 0x08; // RS y backlight
    buffer_datos[1] = buffer_datos[0] | 0x04; // Enable high
    buffer_datos[2] = buffer_datos[0] & ~0x04; // Enable low

    i2c_master_transmit(dev_handle, buffer_datos, 3, pdMS_TO_TICKS(10));
}

void lcd_enviar_byte(uint8_t byte, uint8_t modo) {
    lcd_enviar_nibble(byte & 0xF0, modo); // Enviar nibble alto
    lcd_enviar_nibble((byte << 4) & 0xF0, modo); // Enviar nibble bajo
}   
//3 Comandos Lógicos de alto nivel
void lcd_inicializar_secuencia(void) {
    vTaskDelay(pdMS_TO_TICKS(50)); // Espera 50 ms después de encender el LCD

    lcd_enviar_nibble(0x30, 0); // Función set: 8 bits
    vTaskDelay(pdMS_TO_TICKS(5)); // Espera 5 ms

    lcd_enviar_nibble(0x30, 0); // Función set: 8 bits
    vTaskDelay(pdMS_TO_TICKS(1)); // Espera 1 ms

    lcd_enviar_nibble(0x30, 0); // Función set: 8 bits
    vTaskDelay(pdMS_TO_TICKS(1)); // Espera 1 ms

    lcd_enviar_nibble(0x20, 0); // Función set: 4 bits
    vTaskDelay(pdMS_TO_TICKS(1)); // Espera 1 ms

    lcd_enviar_byte(0x28, 0); // Función set: 4 bits, 2 líneas, 5x8 puntos
    lcd_enviar_byte(0x08, 0); // Display off
    lcd_enviar_byte(0x01, 0); // Clear display
    vTaskDelay(pdMS_TO_TICKS(2)); // Espera 2 ms
    lcd_enviar_byte(0x06, 0); // Entry mode set: Increment cursor
    lcd_enviar_byte(0x0C, 0); // Display on, cursor off, blink off
}
void lcd_ir_a(uint8_t fila, uint8_t columna) {//
    uint8_t direccion = 0x00;//

    switch (fila) {
        case 0:
            direccion = 0x00 + columna;
            break;
        case 1:
            direccion = 0x40 + columna;
            break;
        case 2:
            direccion = 0x14 + columna;// Dirección de la tercera fila
            break;
        case 3:
            direccion = 0x54 + columna;// Dirección de la cuarta fila
            break;
        default:
            return; // Fila inválida
    }

    lcd_enviar_byte(0x80 | direccion, 0); // Comando para establecer la dirección DDRAM
}
void lcd_imprimir_caracter(char caracter) {
    lcd_enviar_byte((uint8_t)caracter, 1); // Modo de datos (RS=1)
}   

void lcd_imprimir_cadena(const char *cadena) {
    while (*cadena) {
        lcd_imprimir_caracter(*cadena++);
    }
}