/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : Ivan Nikitin
* Version            : V1.0.0
* Date               : 2025
* Description        : L9208A driver for sony displays
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

/*
 *@Note
 must run in one thread, one core, one instruction at a time

*/

#include "debug.h"
#include "font.h"

//driver chip pinout
#define RST  GPIO_Pin_1 // reset
#define CS  GPIO_Pin_0 // chip select
#define SCK  GPIO_Pin_15 // clock
#define SDI  GPIO_Pin_14 // data
#define DC  GPIO_Pin_12 // data/command

//reg output pin
#define REG_PIN GPIO_Pin_13

//output GPIO terminal 
#define GPIO GPIOB
#define clk_periph RCC_APB2Periph_GPIOB

#define MATRIX_ROWS 64
#define MATRIX_COLS 128

//device control parameters
int is_reset = 1;
int state = 0;
int re_enable=0;

volatile int set_reset = 0;
volatile int byte_ready = 0;
unsigned char byte = 0;

unsigned int clk_state = 0;
int byte_state = 0;
int byte_clocker(){

	
	if(!clk_state){
		int shiftreg = 7 - (byte_state >> 1);
		if(byte & (1 << (shiftreg)))
			GPIO_SetBits(GPIO,SDI);
		else
			GPIO_ResetBits(GPIO,SDI);
		GPIO_ResetBits(GPIO, SCK);
	}else{
		GPIO_SetBits(GPIO, SCK);
	}

	clk_state = ~clk_state;
	byte_state++;
	if(byte_state >= 16){
		byte_state = 0;
		return 1;
	}
	return 0;
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) {
 if(TIM2->INTFR & TIM_UIF) {  
        TIM2->INTFR &= ~TIM_UIF; 
		 
			switch(state){
				case 0:
					if(is_reset){
						if(!re_enable){
							GPIO_SetBits(GPIO,SCK);
							clk_state = 0;
							re_enable = 1;
						}else{
							GPIO_SetBits(GPIO,RST);
							is_reset = 0;
							state = 1;
							re_enable = 0;
						}
			
					}else{
						state = 1;
					}
					break;
				case 1:
					if(byte_ready){
						if(byte_clocker()){
							byte_ready = 0;
						}

					}else{
						state = (set_reset)? 2 : 1;
					}
					break;
				case 2:
					GPIO_SetBits(GPIO,SCK);
					GPIO_ResetBits(GPIO,RST);
					is_reset = 1;
					state = (set_reset)? 2 : 0;
					break;
					

			}
	
 }
        
    
}

void send_byte(unsigned char ibyte,int command){
	if(command)
		GPIO_ResetBits(GPIO,DC);
	else 
		GPIO_SetBits(GPIO,DC);
	set_reset = 0;
	byte = ibyte;
	byte_ready=1;
}
void send_byte_blocking(unsigned char ibyte,int command){
	send_byte(ibyte,command);
	
		
	while(byte_ready);
}
void reset_device(){
	set_reset=1;
}
void deselect_device(){
	GPIO_ResetBits(GPIO,CS);
}
void select_device(){
	GPIO_SetBits(GPIO,CS);
}

void st7565_init(void) {
    
    
   
    
    send_byte_blocking(0xE2, 1);  //flush registers
	
    Delay_Ms(10);
    
    send_byte_blocking(0xA2, 1);  
    send_byte_blocking(0xA0, 1);  
    send_byte_blocking(0xC8, 1); 
    send_byte_blocking(0x24, 1);  
    send_byte_blocking(0x81, 1);  //contrast start
    send_byte_blocking(0x30, 1);  //contrast
    send_byte_blocking(0x2F, 1);  
    send_byte_blocking(0x40, 1);  
    send_byte_blocking(0xAF, 1);  
 
}
//00 to 3F
void st7565_set_contrast(unsigned char contrast){
        send_byte_blocking(0x81, 1);
		send_byte_blocking(contrast, 1);
}

unsigned char st7565_matrix[MATRIX_ROWS/8][MATRIX_COLS];

void write_pixels_raw(){
	send_byte_blocking(0x40, 1);  //set cursor to zero
	int mpage = MATRIX_ROWS >> 3;
    for(int page = 0; page < mpage; page++) {
        send_byte_blocking(0xB0 | page, 1);  //set page address
        send_byte_blocking(0x10, 1);         //set column address to start
        send_byte_blocking(0x00, 1);         
        
        for(int col = 0; col < MATRIX_COLS; col++) {
            send_byte_blocking(st7565_matrix[page][col], 0);     
        }
    }
	send_byte_blocking(0b11101110, 1);  //release
}
void clear(){
int mpage = MATRIX_ROWS >> 3;
for(int page = 0; page < mpage; page++) {
        for(int col = 0; col < MATRIX_COLS; col++) {
            st7565_matrix[page][col]    =0; 
        }
    }
}
void set_pixel(int x,int y,int on_off){
	int row = y >> 3;
	int bit = y % 8;
	if(on_off)
		st7565_matrix[row][x] |= 1 << bit;
	else 
		st7565_matrix[row][x] &= ~(1 << bit);
}
void set_letter(int x, int y_page,unsigned char c){
	for(int ix=x;ix<x+5;ix++)
		st7565_matrix[y_page][ix] = font_5x7[c-32][ix - x];
}
void write_string(int y_page, int start_x,char* string){
	int index= 0;
	for(int x =start_x;x<MATRIX_COLS - 6;x = x + 6){
		char lc = string[index];
		index++;
		if(!lc)
			break;
		set_letter(x,y_page,lc);
	}
}

void TIM2_Init(void) {
    RCC->APB1PCENR |= RCC_APB1Periph_TIM2; 
   
    uint32_t system_clock = 96000000; // 96MHz
    

    uint32_t target_freq = 200000;
    uint32_t prescaler = 0;  
    uint32_t period = (system_clock / target_freq) - 1;
    
    TIM2->PSC = prescaler;      
    TIM2->ATRLR = period;       
    
    TIM2->CTLR1 |= TIM_CEN;     
    TIM2->DMAINTENR |= TIM_UIE; 
 
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_SetPriority(TIM2_IRQn, 1);
}
void GPIO_Init_Library(void) {
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(clk_periph, ENABLE);
    
    
    GPIO_InitStructure.GPIO_Pin = RST | SCK | SDI | DC | CS;
	if(REG_PIN)
		GPIO_InitStructure.GPIO_Pin |= REG_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIO, &GPIO_InitStructure);
  
	if(REG_PIN)
		GPIO_SetBits(GPIO,REG_PIN);

		  

    GPIO_ResetBits(GPIO,SCK | SDI | DC | CS | RST );
	
}


int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();
	Delay_Init();

	GPIO_Init_Library();
	Delay_Ms(10);
    TIM2_Init();
	Delay_Ms(10);
	
	st7565_init();
	
	write_string(1,0,"Michelle");
	write_pixels_raw();
    while(1) {
    

        Delay_Ms(5000);
		write_string(2,0,"Michelle is cute");
	    write_string(3,0,":;'[]{}\\|!~`-_!@");
		write_string(4,0,"#$%^&*()+=/?<>,.\"");
		write_pixels_raw();
        
    }

}



