#include <stm32f0xx.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "clock_.h"
#include "fifo.h"

#define LOG(msg...) printf(msg);

#define BAUDRATE 115200

#define COLS 10
#define ROWS 10

typedef struct
{
	int length;
	int amount;
} Ship;

// UART
const uint8_t USART2_RX_PIN = 3; // PA3 is used as USART2_RX
const uint8_t USART2_TX_PIN = 2; // PA2 is used as USART2_TX

// FIFO
volatile Fifo_t uart_rx_fifo;
volatile char uart_read_out[FIFO_SIZE + 1];

// Game Components
short int nState = 0;
unsigned int x = 0, y = 0;
char result = '0';
int hits = 30;

Ship ships[] = {
	{5, 1}, // Schlachtschiff
	{4, 2}, // Kreuzer
	{3, 3}, // Zerstörer
	{2, 4}	// U-Boote
};

char my_game_field[ROWS * COLS];
char my_game_field_copy[ROWS * COLS];
unsigned char my_cs[11];

char enemy_game_field[ROWS * COLS];
unsigned char enemy_cs[11];

// Field Creation Functions
int is_conflict(int x_pos, int y_pos, int dir, int length)
{
	// Determine the bounding box for the ship and its surroundings
	int x_start = x_pos - 1;
	int y_start = y_pos - 1;
	int x_end = x_pos + (dir == 1 ? length + 1 : 2);
	int y_end = y_pos + (dir == 0 ? length + 1 : 2);

	// Clamp bounds to game field size
	if (x_start < 0)
		x_start = 0;
	if (y_start < 0)
		y_start = 0;
	if (x_end > ROWS)
		x_end = ROWS;
	if (y_end > COLS)
		y_end = COLS;

	// Check all cells in the bounding box
	for (int x_search = x_start; x_search < x_end; x_search++)
	{
		for (int y_search = y_start; y_search < y_end; y_search++)
		{
			if (my_game_field[x_search * COLS + y_search] != '0')
			{
				return 1; // Conflict found
			}
		}
	}

	return 0; // No conflict
}

int place_ship(int x_pos, int y_pos, int dir, int length)
{
	if (dir == 0) // horizontal
		for (int i = 0; i < length; i++)
			my_game_field[x_pos * COLS + (y_pos + i)] = '0' + (char)length;
	else // vertical
		for (int i = 0; i < length; i++)
			my_game_field[(x_pos + i) * COLS + y_pos] = '0' + (char)length;
	return 0;
}

int create_field()
{
	// Clear the field
	for (int i = 0; i < ROWS * COLS; i++)
	{
		my_game_field[i] = '0';
	}

	// Place each ship type
	for (int i = 0; i < sizeof(ships) / sizeof(ships[0]); i++)
	{
		for (int j = 0; j < ships[i].amount; j++)
		{
			int dir, x_ship, y_ship;

			do
			{
				dir = rand() % 2; // 0 = horizontal, 1 = vertical

				if (dir == 0)
				{
					x_ship = rand() % ROWS;
					y_ship = rand() % (COLS - ships[i].length + 1);
				}
				else
				{
					x_ship = rand() % (ROWS - ships[i].length + 1);
					y_ship = rand() % COLS;
				}

			} while (is_conflict(x_ship, y_ship, dir, ships[i].length));

			place_ship(x_ship, y_ship, dir, ships[i].length);
		}
	}
	return 0;
}

int calc_checksum()
{
	int total = 0;
	for (int i = 0; i < ROWS; i++)
	{
		int count = 0;
		for (int j = 0; j < COLS; j++)
		{
			if (my_game_field[i * COLS + j] != '0')
				count++;
		}
		total += count;
		my_cs[i] = (char)count;
	}
	if (total == 30)
		return 0;
	else
		return -1;
}

// For supporting printf function we override the _write function to redirect the output to UART
int _write(int handle, char *data, int size)
{
	int count = size; // Make a copy of the size to use in the loop

	while (count--) // Loop through each byte in the data buffer
	{
		// Wait until the transmit data register (TDR) is empty, indicating that USART2 is ready to send a new byte
		while (!(USART2->ISR & USART_ISR_TXE))
		{
			// Wait here (busy wait) until TXE (Transmit Data Register Empty) flag is set
		}
		// Load the next byte of data into the transmit data register (TDR). This sends the byte over UART
		USART2->TDR = *data++; // The pointer 'data' is incremented to point to the next byte to send
	}
	return size; // Return the total number of bytes that were written
}

void USART2_IRQHandler(void)
{
	static int ret;					  // You can do some error checking
	if (USART2->ISR & USART_ISR_RXNE) // Check if RXNE flag is set (data received)
	{
		uint8_t c = USART2->RDR;					// Read received byte from RDR (this automatically clears the RXNE flag)
		ret = fifo_put((Fifo_t *)&uart_rx_fifo, c); // Put incoming Data into the FIFO Buffer for later handling

		if (c == '\n')
		{
			uint8_t i = 0;
			while (uart_rx_fifo.amount != 0)
			{
				fifo_get((Fifo_t *)&uart_rx_fifo, (uint8_t *)&uart_read_out[i]);
				i++;
			}
			uart_read_out[i] = '\0';
		}
	}
	// USART2->ISR &= ~(0b1 << 3);
	USART2->ICR = 0xffffffff;
}

void game_init()
{
	srand(time(NULL));
	create_field();
	memcpy(my_game_field_copy, my_game_field, sizeof(my_game_field));
	calc_checksum();

	for (int i = 0; i < ROWS * COLS; i++)
		enemy_game_field[i] = '0';
}

int main(void)
{
	SystemClock_Config(); // Configure the system clock to 48 MHz

	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;	  // Enable GPIOA clock
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN; // Enable USART2 clock

	// ---------------- UART TX Pin Configuration (PA2) ----------------
	GPIOA->MODER |= 0b10 << (USART2_TX_PIN * 2);	// Set PA2 to Alternate Function mode
	GPIOA->AFR[0] |= 0b0001 << (4 * USART2_TX_PIN); // Set AF for PA2 (USART2_TX)
	// ---------------- UART RX Pin Configuration (PA3) ----------------
	GPIOA->MODER |= 0b10 << (USART2_RX_PIN * 2);	// Set PA3 to Alternate Function mode
	GPIOA->AFR[0] |= 0b0001 << (4 * USART2_RX_PIN); // Set AF for PA3 (USART2_RX)

	USART2->BRR = (APB_FREQ / BAUDRATE); // Set baud rate (requires APB_FREQ to be defined)
	USART2->CR1 |= 0b1 << 2;			 // Enable receiver (RE bit)
	USART2->CR1 |= 0b1 << 3;			 // Enable transmitter (TE bit)
	USART2->CR1 |= 0b1 << 0;			 // Enable USART (UE bit)
	USART2->CR1 |= 0b1 << 5;			 // Enable RXNE interrupt (RXNEIE bit)

	NVIC_SetPriorityGrouping(0);							   // Use 4 bits for priority, 0 bits for subpriority
	uint32_t uart_pri_encoding = NVIC_EncodePriority(0, 1, 0); // Encode priority: group 1, subpriority 0
	NVIC_SetPriority(USART2_IRQn, uart_pri_encoding);		   // Set USART2 interrupt priority
	NVIC_EnableIRQ(USART2_IRQn);							   // Enable USART2 interrupt

	fifo_init((Fifo_t *)&uart_rx_fifo); // Init the FIFO
	game_init();

	for (;;) // Infinite loop
	{
		switch (nState)
		{
		case 0: // Read Game Start
			// Wait for Game Start
			if (strcmp((const char *)uart_read_out, "HD_START\r\n") == 0)
				nState = 1;
			break;

		case 1: // Write Game Start
			LOG("DH_START_KEVIN\r\n");
			nState = 2;
			break;

		case 2: // Read CS
			if (strncmp((const char *)uart_read_out, "HD_CS_", 6) == 0)
			{
				for (int i = 0; i <= 9; i++)
					enemy_cs[i] = uart_read_out[i + 6];
				enemy_cs[10] = '\0';
				uart_read_out[0] = '\0';
				nState = 3;
			}
			break;

		case 3: // Write CS
			LOG("DH_CS_");
			for (int i = 0; i < COLS; i++) LOG("%d", my_cs[i]);
			LOG("\r\n");
			nState = 4;
			break;

		case 4: // Read Boom
			// Wait for Boom
			if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0)
			{
				x = uart_read_out[8] - '0';
				y = uart_read_out[10] - '0';
				uart_read_out[0] = '\0';
				nState = 5;
			}
			break;

		case 5: // Write HM
			if (my_game_field_copy[x * COLS + y] == '0')
			{
				my_game_field_copy[x * COLS + y] = 'M';
				LOG("DH_BOOM_M\r\n");
			}
			else
			{
				my_game_field_copy[x * COLS + y] = 'H';
				hits -= 1;
				if (hits <= 0)
				{
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
			// Wait for HM or Win Message
			if (strncmp((const char *)uart_read_out, "HD_SF", 5) == 0)
			{
				nState = 9;
				break;
			}
			if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0)
			{
				result = uart_read_out[8];
				uart_read_out[0] = '\0';
			}
			else
				break;

			nState = 4;
			break;

		case 8: // Lose or Print DH_SF
			for (int i = 0; i < ROWS; i++)
			{
				LOG("DH_SF%uD", i);
				for (int j = 0; j < COLS; j++)
					LOG("%c", my_game_field[i * COLS + j]);
				LOG("\r\n");
			}
			nState = 0;

			game_init();	// After Lose or Win regenerate field/cs
			break;

		case 9: // Win
			if (strncmp((const char *)uart_read_out, "HD_SF", 5) == 0)
			{
				int row = uart_read_out[5] - '0';
				for (int i = 0; i < COLS; i++) enemy_game_field[row * COLS + i] = uart_read_out[i+7];

				if (row == 9) nState = 8;
			}
			break;
		}
	}
	return 0;
}