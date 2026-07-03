#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <stdint.h>

// Funciones del LCD
void inicializar_i2c_lcd_hardware(void);// Inicializa el bus I2C y el dispositivo LCD

void lcd_inicializar_secuencia(void);// Envía la secuencia de inicialización al LCD

void lcd_ir_a(uint8_t fila, uint8_t columna);// Mueve el cursor a la posición especificada (fila, columna)

void lcd_imprimir_caracter(char caracter);// Imprime un carácter en la posición actual del cursor

void lcd_imprimir_cadena(const char *cadena);// Imprime una cadena de caracteres en la posición actual del cursor

#endif // LCD_I2C_H