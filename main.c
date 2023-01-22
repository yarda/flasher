/*
 * File:   main.c
 * Author: yarda
 * Copyright (C) 2022 yarda <zbox AT atlas.cz>
 * MCU: PIC12F1572
 * VDD: 4.5 V
 * Description: PIC12F1572 three mode LED flasher with the constant current LED drive
 * LED drive cca. 20 mA
 * Mode 0: off
 * Mode 1: 80 ms on, 80 ms off
 * Mode 2: 160 ms on, 160 ms off
 * Mode 3: on
 * Modes are changed by the push button
 * Pin RA2: feedback loop sensing input
 * Pin RA4: push button input (active low)
 * Pin RA5: LED drive output
 *
 * Created on October 10, 2022, 1:12 AM
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _XTAL_FREQ 31000

#include <xc.h>

// CONFIG1
#pragma config FOSC = INTOSC    //  (INTOSC oscillator; I/O function on CLKIN pin)
#pragma config WDTE = NSLEEP    // Watchdog Timer Enable (WDT enabled while running and disabled in Sleep)
#pragma config PWRTE = ON       // Power-up Timer Enable (PWRT enabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select (MCLR/VPP pin function is VPP)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config BOREN = OFF      // Brown-out Reset Enable (Brown-out Reset disabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)

// CONFIG2
#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config PLLEN = OFF      // PLL Enable (4x PLL enabled)
#pragma config STVREN = ON      // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will cause a Reset)
#pragma config BORV = LO        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), low trip point selected.)
#pragma config LPBOREN = ON     // Low Power Brown-out Reset enable bit (LPBOR is enabled)
#pragma config LVP = ON         // Low-Voltage Programming Enable (Low-voltage programming enabled)

// #pragma config statements should precede project file includes.
// Use project enums instead of #define for ON and OFF.

// 80 ms is cca 620 ticks with 31 kHz clock
#define T80ms 64916
#define T160ms 64296

#define BUTTONM 0x10
#define BUTTONP PORTAbits.RA4
#define LEDP PORTAbits.RA5

#define MOFF 0
#define MFAST 1
#define MSLOW 2
#define MON 3

#define BUFLEN 10

// terget voltage value on the sensing resistor
#define TARGET_VAL 12

#define clear_IOCAF() { \
    IOCAF &= ~BUTTONM; \
}

#define test_IOCAF() \
    ((IOCAF & BUTTONM) == BUTTONM)

#define reload_pwm() { \
    while (PWM1LDCONbits.LDA) \
        ; \
    PWM1LDCONbits.LDA = 1; \
}

#define reload_timer() { \
    TMR1 = timer_preset; \
}

unsigned char mode;
unsigned short timer_preset;
unsigned char adc_val;
unsigned short buf[BUFLEN] = { TARGET_VAL };

void init(void) {
    // RA5 is PWM1
    APFCONbits.P1SEL = 1;
    // 31 kHz internal oscillator
    OSCCON = 0x02;
    // low power sleep mode
    VREGCONbits.VREGPM = 1;
    // explicit init of port A
    PORTA = 0x00;
    LATA = 0x00;
    // RA2 is analog, the rest is digital
    ANSELA = 0x04;
    // BUTTON and RA2 are input, the rest is output
    TRISA = BUTTONM + 0x04;
    // explicitly enable pull-ups control
    OPTION_REG = 0x00;
    // enable pull-up on button
    WPUA = BUTTONM;
    // interrupt on falling edge of BUTTON
    IOCAN = BUTTONM;
    // clear interrupt on change
    clear_IOCAF();
    // enable interrupt on change
    INTCONbits.IOCIE = 1;
    // timer1, fosc/4, no prescaler, timer stopped
    T1CON = 0x00;
    // switch LED off
    LEDP = 0;
    // PWM1 disable, output enable, normal polarity, standard mode
    PWM1CON = 0x40;
    // PWM1 no prescaler, HFINTOSC
    PWM1CLKCON = 0x01;
    // no PWM1 interrupts
    PWM1INTE = 0x00;
    // no trigger for PWM1 reload
    PWM1LDCON = 0x00;
    // PWM1 idependent run mode
    PWM1OFCON = 0x00;
    // PWM1 period
    PWM1PR = 65;
    // PWm1 phase
    PWM1PH = 0;
    // PWM1 duty cycle
    PWM1DC = 10;
    // disable fixed voltage reference, set it to 1,024 V
    FVRCON = 0x42;
    // AN2 (RA2), disable ADC
    ADCON0 = 0x08;
    // right justified, fosc/4, fixed voltage reference
    ADCON1 = 0xc3;
    // no auto conversion trigger
    ADCON2 = 0;
    mode = MOFF;
}

void setup_mode(void) {
    switch (mode) {
        case MOFF:
            PWM1CONbits.EN = 0;
            FVRCONbits.FVREN = 0;
            ADCON0bits.ADON = 0;
            ADCON0bits.ADGO = 0;
            T1CONbits.TMR1ON = 0;
            PIR1bits.TMR1IF = 0;
            break;
        case MFAST:
            PWM1CONbits.EN = 1;
            FVRCONbits.FVREN = 1;
            ADCON0bits.ADON = 1;
            ADCON0bits.ADGO = 1;
            timer_preset = T80ms;
            T1CONbits.TMR1ON = 1;
            reload_timer();
            break;
        case MSLOW:
            PWM1CONbits.EN = 1;
            FVRCONbits.FVREN = 1;
            ADCON0bits.ADON = 1;
            ADCON0bits.ADGO = 1;
            timer_preset = T160ms;
            T1CONbits.TMR1ON = 1;
            reload_timer();
            break;
        case MON:
            PWM1CONbits.EN = 1;
            FVRCONbits.FVREN = 1;
            ADCON0bits.ADON = 1;
            ADCON0bits.ADGO = 1;
            T1CONbits.TMR1ON = 0;
            break;
        default:
            mode = MOFF;
            PWM1CONbits.EN = 0;
            FVRCONbits.FVREN = 0;
            ADCON0bits.ADON = 0;
            ADCON0bits.ADGO = 0;
            T1CONbits.TMR1ON = 0;
            PIR1bits.TMR1IF = 0;
    }
}

unsigned char process_val(unsigned short val) {
    unsigned short avg = 0;
    for (unsigned short x = 0; x < BUFLEN - 1; x++) {
        avg += buf[x];
        buf[x] = buf[x + 1];
    }
    avg += val;
    buf[BUFLEN - 1] = val;
    return (unsigned char) (avg / BUFLEN);
}

void main(void) {
    init();

    while (1) {
        while (!BUTTONP)
            CLRWDT();
        __delay_ms(30);
        clear_IOCAF();
        SLEEP();
        // wait for transient events to stabilize
        __delay_ms(30);
        clear_IOCAF();
        __delay_ms(10);
        if (!test_IOCAF())
            mode++;
        else
            clear_IOCAF();
        setup_mode();
        while (mode > MOFF) {
            if (test_IOCAF()) {
                __delay_ms(30);
                clear_IOCAF();
                if (!BUTTONP) {
                    mode++;
                    if (mode > MON)
                        mode = MOFF;
                    setup_mode();
                }
            }
            if ((mode == MFAST || mode == MSLOW) && PIR1bits.TMR1IF) {
                PIR1bits.TMR1IF = 0;
                reload_timer();
                PWM1CONbits.EN ^= 1;
            }
            if (PWM1CONbits.EN) {
                if (!ADCON0bits.ADGO) {
                    adc_val = process_val(ADRESL);
                    if (adc_val < TARGET_VAL && PWM1DC < 60) {
                        PWM1DC++;
                        reload_pwm();
                    }
                    if (adc_val > TARGET_VAL && PWM1DC > 1) {
                        PWM1DC--;
                        reload_pwm();
                    }
                    ADCON0bits.ADGO = 1;
                }
            }
            CLRWDT();
        }
    }
    return;
}
