/*
 * Smart Industrial Gas Leakage Detection and Safety System
 * Microcontroller: ATmega32 (AVR)
 * Clock: 8 MHz internal/external (F_CPU = 8000000UL)
 *
 * Sensors:
 *   MQ-2 Gas Sensor      -> PA0 (ADC0)
 *   LM35 Temperature     -> PA1 (ADC1)
 *
 * Outputs:
 *   Green LED  -> PB0 (Safe)
 *   Yellow LED -> PB1 (Warning)
 *   Red LED    -> PB2 (Danger/Critical)
 *   Buzzer     -> PB3
 *   Fan        -> PD4
 *   LCD (4-bit mode) -> PC0-PC5 (RS, EN, D4-D7)
 *
 * Emergency Stop -> PD2 (INT0, falling edge)
 */

#define F_CPU 8000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#define LCD_RS PC0
#define LCD_EN PC1
#define LCD_D4 PC2
#define LCD_D5 PC3
#define LCD_D6 PC4
#define LCD_D7 PC5

#define LED_GREEN  PB0
#define LED_YELLOW PB1
#define LED_RED    PB2
#define BUZZER     PB3
#define FAN        PD4

/* GLOBAL FLAGS */
volatile unsigned char emergency_flag = 0;

/*
 LCD FUNCTIONS
*/
void lcd_pulse(void)
{
    PORTC |= (1 << LCD_EN);
    _delay_us(5);
    PORTC &= ~(1 << LCD_EN);
    _delay_us(100);
}

void lcd_nibble(unsigned char n)
{
    if (n & 0x01) PORTC |= (1 << LCD_D4); else PORTC &= ~(1 << LCD_D4);
    if (n & 0x02) PORTC |= (1 << LCD_D5); else PORTC &= ~(1 << LCD_D5);
    if (n & 0x04) PORTC |= (1 << LCD_D6); else PORTC &= ~(1 << LCD_D6);
    if (n & 0x08) PORTC |= (1 << LCD_D7); else PORTC &= ~(1 << LCD_D7);
    lcd_pulse();
}

void lcd_send(unsigned char val, unsigned char mode)
{
    if (mode)
        PORTC |= (1 << LCD_RS);
    else
        PORTC &= ~(1 << LCD_RS);

    lcd_nibble(val >> 4);
    lcd_nibble(val & 0x0F);
    _delay_us(100);
}

void lcd_command(unsigned char cmd)
{
    lcd_send(cmd, 0);
    if (cmd == 0x01 || cmd == 0x02)
        _delay_ms(2);
}

void lcd_char(unsigned char c)
{
    lcd_send(c, 1);
}

void lcd_string(char *str)
{
    unsigned char i = 0;
    while (str[i] != '\0')
    {
        lcd_char(str[i]);
        i++;
    }
}

void lcd_goto(unsigned char row, unsigned char col)
{
    unsigned char pos;
    if (row == 0)
        pos = 0x80 + col;
    else
        pos = 0xC0 + col;
    lcd_command(pos);
}

void lcd_clear(void)
{
    lcd_command(0x01);
    _delay_ms(2);
}

void lcd_init(void)
{
    DDRC = 0xFF;
    PORTC = 0x00;
    _delay_ms(50);

    lcd_nibble(0x03); _delay_ms(5);
    lcd_nibble(0x03); _delay_ms(1);
    lcd_nibble(0x03); _delay_ms(1);
    lcd_nibble(0x02); _delay_ms(1);

    lcd_command(0x28);
    lcd_command(0x0C);
    lcd_command(0x06);
    lcd_command(0x01);
    _delay_ms(2);
}

/*
 NUMBER TO STRING
*/
void num_to_str(unsigned int num, char *buf)
{
    unsigned char i = 0;
    unsigned char j = 0;
    char temp[6];

    if (num == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (num > 0)
    {
        temp[i] = (num % 10) + '0';
        num = num / 10;
        i++;
    }

    while (i > 0)
    {
        buf[j] = temp[i - 1];
        j++;
        i--;
    }
    buf[j] = '\0';
}

void adc_init(void)
{
    ADMUX = 0x40;   // VREF = AVCC (5V)
    ADCSRA = 0x87;  // Enable ADC, Prescaler = 128
}

unsigned int adc_read(unsigned char channel)
{
    ADMUX = 0x40 | (channel & 0x07);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCW;
}

ISR(INT0_vect)
{
    emergency_flag = 1;
}

/*
 OUTPUT HELPERS
*/
void all_off(void)
{
    PORTB &= ~((1 << LED_GREEN) | (1 << LED_YELLOW) | (1 << LED_RED) | (1 << BUZZER));
    PORTD &= ~(1 << FAN);
}

/*
 MAIN
*/
int main(void)
{
    unsigned int gas_raw = 0;
    unsigned int temp_raw = 0;
    unsigned int gas_pct = 0;
    unsigned int temp_c = 0;
    char str_gas[5];
    char str_temp[5];

    /* GPIO Setup */
    DDRB |= (1 << LED_GREEN) | (1 << LED_YELLOW) | (1 << LED_RED) | (1 << BUZZER);
    PORTB = 0x00;

    DDRD |= (1 << FAN);
    DDRD &= ~(1 << PD2);   // Set INT0 pin as input
    PORTD |= (1 << PD2);   // Enable Internal Pull-Up Resistor

    DDRA = 0x00;            // Set Port A as inputs for sensors

    /* INT0 Setup */
    MCUCR = 0x02;            // Falling Edge triggers interrupt
    GICR = (1 << INT0);
    sei();

    adc_init();
    lcd_init();

    lcd_clear();
    lcd_goto(0, 0);
    lcd_string(" Gas Detector ");
    lcd_goto(1, 0);
    lcd_string(" Starting... ");
    _delay_ms(1000);
    lcd_clear();

    while (1)
    {
        /* 1. Emergency System Interrupt Check */
        if (emergency_flag == 1)
        {
            all_off();
            lcd_clear();
            lcd_goto(0, 0);
            lcd_string("!!EMERGENCY STOP");
            lcd_goto(1, 0);
            lcd_string(" Press to Reset ");

            PORTB |= (1 << BUZZER); _delay_ms(200);
            PORTB &= ~(1 << BUZZER); _delay_ms(200);
            PORTB |= (1 << BUZZER); _delay_ms(200);
            PORTB &= ~(1 << BUZZER);
            _delay_ms(1000);

            emergency_flag = 0;
            lcd_clear();
            continue;
        }

        /* 2. Read Sensors */
        gas_raw = adc_read(0);   /* PA0 = MQ-2 Gas Sensor */
        temp_raw = adc_read(1);  /* PA1 = LM35 */

        /* 3. Convert Values */
        gas_pct = gas_raw;                    /* Raw ADC value directly used (0-1023) */
        temp_c = (temp_raw * 500) / 1023;     // Validated LM35 Celsius formula

        /* 4. Format Strings for Telemetry Display */
        num_to_str(gas_pct, str_gas);
        num_to_str(temp_c, str_temp);

        /* 5. Clear Pin Register Outputs Before Evaluating New States */
        all_off();

        /* 6. Independent Zone Logic Structure */

        // --- CRITICAL ZONE: High Gas (>700) OR High Temperature (>50C) ---
        if (gas_pct > 700 || temp_c > 50)
        {
            PORTB |= (1 << LED_RED);   // Red LED ON
            PORTD |= (1 << FAN);       // DC Motor ON
            PORTB |= (1 << BUZZER);    // Buzzer ON continuously

            lcd_goto(0, 0);
            if (gas_pct > 700 && temp_c > 50) {
                lcd_string("CRITICAL EVACUATE");
            } else if (gas_pct > 700) {
                lcd_string("DANGER! Gas Leak");
            } else {
                lcd_string("DANGER! OVERHEAT");
            }
        }
        // --- WARNING ZONE: Elevated Gas levels (401 - 700) ---
        else if (gas_pct > 400)
        {
            PORTB |= (1 << LED_YELLOW);  // Yellow LED ON
            PORTD |= (1 << FAN);         // DC Motor ON (Ventilation starts)

            // Pulsing warning beep code:
            PORTB |= (1 << BUZZER);      // Buzzer ON
            _delay_ms(100);              // Short beep duration
            PORTB &= ~(1 << BUZZER);     // Buzzer OFF

            lcd_goto(0, 0);
            lcd_string("WARNING!CheckGas");
        }
        // --- SYSTEM SAFE ZONE ---
        else
        {
            PORTB |= (1 << LED_GREEN);   // Green LED ON
            // Buzzer and Fan stay OFF automatically via all_off()
            lcd_goto(0, 0);
            lcd_string("Status: SAFE ");
        }

        /* 7. Bottom Row Real-time Status Data Output */
        lcd_goto(1, 0);
        lcd_string("G:");
        lcd_string(str_gas);
        lcd_string(" T:");
        lcd_string(str_temp);
        lcd_string("C ");

        _delay_ms(100); // Small delay to handle the loop cycle smoothly
    }

    return 0;
}
