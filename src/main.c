#include <stm32f0xx.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "clock_.h"
#include "fifo.h"

#define LOG(msg...) printf(msg);

// UART
#define BAUDRATE 115200

const uint8_t USART2_RX_PIN = 3; // PA3 is used as USART2_RX
const uint8_t USART2_TX_PIN = 2; // PA2 is used as USART2_TX

// FIFO
volatile Fifo_t uart_rx_fifo;
volatile char uart_read_out[FIFO_SIZE+1];

// Game Components
#define COLS 10
#define ROWS 10 
short int nState;
uint8_t enemy_cs[10];
unsigned int x, y;
char result;

char my_game_field[ROWS * COLS] = {	
	'2','0','0','3','0','0','0','0','0','0',
	'2','0','0','3','0','4','4','4','4','0',
	'0','0','0','3','0','0','0','0','0','0',
	'5','0','0','0','0','0','2','0','0','0',
	'5','0','2','0','4','0','2','0','0','0',
	'5','0','2','0','4','0','0','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'0','0','0','0','0','0','0','0','0','0',
	'0','3','3','3','0','0','0','0','0','0'};

char my_game_field_copy[ROWS * COLS] = {	
	'2','0','0','3','0','0','0','0','0','0',
	'2','0','0','3','0','4','4','4','4','0',
	'0','0','0','3','0','0','0','0','0','0',
	'5','0','0','0','0','0','2','0','0','0',
	'5','0','2','0','4','0','2','0','0','0',
	'5','0','2','0','4','0','0','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'0','0','0','0','0','0','0','0','0','0',
	'0','3','3','3','0','0','0','0','0','0'};

char enemy_game_field[ROWS * COLS] = {	
	'2','0','0','3','0','0','0','0','0','0',
	'2','0','0','3','0','4','4','4','4','0',
	'0','0','0','3','0','0','0','0','0','0',
	'5','0','0','0','0','0','2','0','0','0',
	'5','0','2','0','4','0','2','0','0','0',
	'5','0','2','0','4','0','0','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'5','0','0','0','4','0','2','0','0','3',
	'0','0','0','0','0','0','0','0','0','0',
	'0','3','3','3','0','0','0','0','0','0'};

uint32_t my_cs = 2612444403;
int hits = 30;

// For supporting printf function we override the _write function to redirect the output to UART
int _write(int handle, char *data, int size)
{
	int count = size; // Make a copy of the size to use in the loop

	while (count--)	// Loop through each byte in the data buffer
	{
		// Wait until the transmit data register (TDR) is empty, indicating that USART2 is ready to send a new byte
		while (!(USART2->ISR & USART_ISR_TXE))
		{
			// Wait here (busy wait) until TXE (Transmit Data Register Empty) flag is set
		}
		// Load the next byte of data into the transmit data register (TDR). This sends the byte over UART
		USART2->TDR = *data++;	// The pointer 'data' is incremented to point to the next byte to send
	}
	return size;				// Return the total number of bytes that were written
}

void USART2_IRQHandler(void)
{
	static int ret; 								// You can do some error checking
	if (USART2->ISR & USART_ISR_RXNE)				// Check if RXNE flag is set (data received)
	{
		uint8_t c = USART2->RDR;					// Read received byte from RDR (this automatically clears the RXNE flag)
		ret = fifo_put((Fifo_t *)&uart_rx_fifo, c); // Put incoming Data into the FIFO Buffer for later handling

		if (c == '\n')
		{
			uint8_t i = 0;
			while(uart_rx_fifo.amount != 0)
			{
				fifo_get((Fifo_t *)&uart_rx_fifo, (uint8_t *)&uart_read_out[i]);
				i++;
			}
			uart_read_out[i] = '\0';
    	}
	}
	//USART2->ISR &= ~(0b1 << 3);
	USART2->ICR = 0xffffffff;
}

int main(void)
{
	SystemClock_Config(); // Configure the system clock to 48 MHz

	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;		// Enable GPIOA clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN; 	// Enable USART2 clock

	// ---------------- UART TX Pin Configuration (PA2) ----------------
	GPIOA->MODER |= 0b10 << (USART2_TX_PIN * 2);		// Set PA2 to Alternate Function mode
	GPIOA->AFR[0] |= 0b0001 << (4 * USART2_TX_PIN); 	// Set AF for PA2 (USART2_TX)
	// ---------------- UART RX Pin Configuration (PA3) ----------------
	GPIOA->MODER |= 0b10 << (USART2_RX_PIN * 2);		// Set PA3 to Alternate Function mode
	GPIOA->AFR[0] |= 0b0001 << (4 * USART2_RX_PIN); 	// Set AF for PA3 (USART2_RX)

	USART2->BRR = (APB_FREQ / BAUDRATE); 				// Set baud rate (requires APB_FREQ to be defined)
	USART2->CR1 |= 0b1 << 2;						 	// Enable receiver (RE bit)
	USART2->CR1 |= 0b1 << 3;						 	// Enable transmitter (TE bit)
	USART2->CR1 |= 0b1 << 0;						 	// Enable USART (UE bit)
	USART2->CR1 |= 0b1 << 5;						 	// Enable RXNE interrupt (RXNEIE bit)

	NVIC_SetPriorityGrouping(0);								// Use 4 bits for priority, 0 bits for subpriority
	uint32_t uart_pri_encoding = NVIC_EncodePriority(0, 1, 0); 	// Encode priority: group 1, subpriority 0
	NVIC_SetPriority(USART2_IRQn, uart_pri_encoding);			// Set USART2 interrupt priority
	NVIC_EnableIRQ(USART2_IRQn);								// Enable USART2 interrupt

	fifo_init((Fifo_t *)&uart_rx_fifo); // Init the FIFO
	nState = 0;
	for(int i = 0;i <= 0;i++) enemy_cs[i] = 0;
	x = 0;
	y = 0;
	for (;;) // Infinite loop
	{
		switch (nState) {
			case 0: // Read Game Start
				// Wait for Game Start
				if (strcmp((const char *)uart_read_out, "HD_START\r\n")) {
					break;
				}else nState = 1;

			case 1: // Write Game Start
				LOG("DH_START_KEVIN\r\n");
				nState = 2;
				break;

			case 2: // Read CS
				if (strncmp((const char *)uart_read_out, "HD_CS_", 6) == 0){
					for (int i = 0;i <= 9;i++) enemy_cs[i] = uart_read_out[i+6];
					nState = 3;
					break;
				} else break;
				
			case 3: // Write CS
				LOG("DH_CS_%lu\r\n", my_cs);
				nState = 4;
				break;

			case 4: // Read Boom
				// Wait for Boom 
				if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0){
					x = uart_read_out[8] - '0';
					y = uart_read_out[10] - '0';
					uart_read_out[0] = '\0';
					nState = 5;
					break;
				} else break;

			case 5: // Write HM
				if (my_game_field_copy[x * COLS + y] == '0') {
					my_game_field_copy[x * COLS + y] = 'M';
					LOG("DH_BOOM_M\r\n");
				}else {
					my_game_field_copy[x * COLS + y] = 'H';
					hits -= 1;
					if (hits <= 0) {
						nState = 8;
						break;
					}
					LOG("DH_BOOM_H\r\n");
				}

				nState = 6;
				break;

			case 6: // Write Boom
				LOG("DH_BOOM_%u_%u\r\n", x, y);
				nState = 7;
				break;

			case 7: // Read HM
				// Wait HM
				if (strncmp((const char *)uart_read_out, "HD_SF9", 6) == 0)
				{
					nState = 9;
					break;
				}
				if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0){
					//sscanf((const char *)uart_read_out, "HD_BOOM_%c\r\n", &result);
					result = uart_read_out[8];
					uart_read_out[0] = '\0';
				} else break;

				nState = 4;
				break;
			
			case 8: // Lose
				for (int i = 0; i <= ROWS-1; i++)
				{
					LOG("DH_SF%uD", i);	
					for (int j = 0; j <= COLS-1; j++)
					{
						LOG("%d", my_game_field[i * COLS + j] - '0');
					}
					LOG("\r\n");
				}
			nState = 0;
			break;

			case 9:
				if (strncmp((const char *)uart_read_out, "HD_SF9", 6) == 0){
					for (int i = 0; i <= ROWS-1; i++)
					{
						LOG("DH_SF%uD", i);	
						for (int j = 0; j <= COLS-1; j++)
							LOG("%d", my_game_field[i * COLS + j]);
						LOG("\r\n");
					}
				} else break;
				nState = 0;
		}
	}
	return 0;
}