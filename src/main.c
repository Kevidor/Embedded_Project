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

uint8_t nState = 0;
volatile Fifo_t uart_rx_fifo;
volatile char uart_read_out[FIFO_SIZE+1];

// For supporting printf function we override the _write function to redirect the output to UART
int _write(int handle, char *data, int size)
{
	// 'handle' is typically ignored in this context, as we're redirecting all output to USART2
	// 'data' is a pointer to the buffer containing the data to be sent
	// 'size' is the number of bytes to send

	int count = size; // Make a copy of the size to use in the loop

	// Loop through each byte in the data buffer
	while (count--)
	{
		// Wait until the transmit data register (TDR) is empty,
		// indicating that USART2 is ready to send a new byte
		while (!(USART2->ISR & USART_ISR_TXE))
		{
			// Wait here (busy wait) until TXE (Transmit Data Register Empty) flag is set
		}

		// Load the next byte of data into the transmit data register (TDR)
		// This sends the byte over UART
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
			if (strcmp((const char *)uart_read_out, "HD_START\r\n") == 0) {
				nState = 1;
			}
    	}
	}
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

	USART2->BRR = (APB_FREQ / BAUDRATE); // Set baud rate (requires APB_FREQ to be defined)
	USART2->CR1 |= 0b1 << 2;						 // Enable receiver (RE bit)
	USART2->CR1 |= 0b1 << 3;						 // Enable transmitter (TE bit)
	USART2->CR1 |= 0b1 << 0;						 // Enable USART (UE bit)
	USART2->CR1 |= 0b1 << 5;						 // Enable RXNE interrupt (RXNEIE bit)

	NVIC_SetPriorityGrouping(0);								// Use 4 bits for priority, 0 bits for subpriority
	uint32_t uart_pri_encoding = NVIC_EncodePriority(0, 1, 0); 	// Encode priority: group 1, subpriority 0
	NVIC_SetPriority(USART2_IRQn, uart_pri_encoding);			// Set USART2 interrupt priority
	NVIC_EnableIRQ(USART2_IRQn);								// Enable USART2 interrupt

	fifo_init((Fifo_t *)&uart_rx_fifo); // Init the FIFO

	for (;;) // Infinite loop
	{
		switch (nState) {
			case 0: // Read Game Start
				// Wait until HD_START gets read out from UART 
				break;
			case 1: // Write Game Start
				LOG("DH_START_KEVIN");
				nState = 2;
				break;
			case 2: //
				break;
		}
	}
}