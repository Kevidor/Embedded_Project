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

#define MAX_PLACEMENTS 1000

// UART
const uint8_t USART2_RX_PIN = 3; // PA3 is used as USART2_RX
const uint8_t USART2_TX_PIN = 2; // PA2 is used as USART2_TX

// FIFO
volatile Fifo_t uart_rx_fifo;
volatile char uart_read_out[FIFO_SIZE + 1];

// Game Components
typedef struct { int x, y; }Vec2;

typedef struct { int length, amount; } Ship;

typedef struct {
    Vec2 pos;
    int dir; // 0 = horizontal, 1 = vertical
} ShipPlacement;

Ship ships[] = {
	{5, 1}, // Schlachtschiff
	{4, 2}, // Kreuzer
	{3, 3}, // Zerstörer
	{2, 4}	// U-Boote
};

short int nState = 0;
Vec2 position = {0, 0};
Vec2 pre_position = {0, 0};
char result = '0';
int hits = 30;

char my_game_field[ROWS * COLS];
char my_game_field_copy[ROWS * COLS];
unsigned char my_cs[11];
int adc_value;

char enemy_game_field[ROWS * COLS];
unsigned char enemy_cs[11];

// Field Creation Functions
void seed_rng_from_adc(void) // Von Hannes
{
    // Starte eine Conversion auf Kanal 0
    ADC1->CHSELR = ADC_CHSELR_CHSEL0;         // Wähle Kanal 0 (PA0)
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC));       // Warte auf End-of-Conversion

    adc_value = ADC1->DR;                     // Lies das Ergebnis
}

int is_conflict(Vec2 pos, int dir, int length) // Checks for conflicts with other ships
{
    int x_start = pos.x - 1;
    int y_start = pos.y - 1;
    int x_end = pos.x + (dir == 1 ? length : 1) + 1;
    int y_end = pos.y + (dir == 0 ? length : 1) + 1;

    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > ROWS) x_end = ROWS;
    if (y_end > COLS) y_end = COLS;

    for (int x = x_start; x < x_end; ++x)
        for (int y = y_start; y < y_end; ++y)
            if (my_game_field[x * COLS + y] != '0')
                return 1;
    return 0;
}

void place_ship(Vec2 pos, int dir, int length) // Places the ship in the GameField
{
    for (int i = 0; i < length; ++i) {
        int x = pos.x + (dir == 1 ? i : 0);
        int y = pos.y + (dir == 0 ? i : 0);
        my_game_field[x * COLS + y] = '0' + length;
    }
}

// Fisher–Yates shuffle
void shuffle(ShipPlacement *arr, int n)
{
    for (int i = n - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        ShipPlacement tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

int create_field() // Creates ramdom GameField
{
    memset(my_game_field, '0', ROWS * COLS);

    for (int s = 0; s < sizeof(ships)/sizeof(ships[0]); ++s) {
        int len = ships[s].length;
        int needed = ships[s].amount;

        ShipPlacement placements[MAX_PLACEMENTS];
        int count = 0;

        // Generate all valid positions
        for (int x = 0; x < ROWS; ++x) {
            for (int y = 0; y < COLS; ++y) {
                if (y + len <= COLS)
                    placements[count++] = (ShipPlacement){{x, y}, 0}; // horizontal
                if (x + len <= ROWS)
                    placements[count++] = (ShipPlacement){{x, y}, 1}; // vertical
            }
        }

        shuffle(placements, count);

        int placed = 0;
        for (int i = 0; i < count && placed < needed; ++i) {
            Vec2 pos = placements[i].pos;
            int dir = placements[i].dir;
            if (!is_conflict(pos, dir, len)) {
                place_ship(pos, dir, len);
                placed++;
            }
        }

        if (placed < needed) return -1;
    }
	return 0;
}

int calc_checksum() // Calculate the Checksum
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
		my_cs[i] = '0' + count;
	}
	if (total == 30)
		return 0;
	else
		return -1;
}

// Smart Shooting - Checkerboard with Target method
int choose_shot(char last_result, Vec2 last_shot, Vec2* shot)
{
    /* ---------- 1.  if last shot was a hit, push neighbours  ---------- */
    static Vec2 target_stack[32];
    static int  top = 0;

    if (last_result == 'H') {
        /* push the four orthogonal neighbours, if still untried           */
        const int dir[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
        for (int d = 0; d < 4; ++d) {
            int nx = last_shot.x + dir[d][0];
            int ny = last_shot.y + dir[d][1];
            if (nx < 0 || nx >= ROWS || ny < 0 || ny >= COLS) continue;
            if (enemy_game_field[nx * COLS + ny] != '0')      continue;
            if (top < (int)(sizeof target_stack / sizeof target_stack[0]))
                target_stack[top++] = (Vec2){nx, ny};
        }
    }

    /* ---------- 2.  use trace_hit_ship / stack while targets exist ---- */
    while (top) {
        Vec2 p = target_stack[--top];
        if (enemy_game_field[p.x * COLS + p.y] == '0') {   /* still free */
            *shot = p;
            return 1;
        }
    }

    /* ---------- 3.  regular checkerboard search with row checksums ---- */
    for (int r = 0; r < ROWS; ++r) {
        if (enemy_cs[r] == '0') continue;                    /* empty row */

        for (int c = (r & 1); c < COLS; c += 2) {         /* checkerboard */
            if (enemy_game_field[r * COLS + c] == '0') {
                *shot = (Vec2){r, c};
                return 1;
            }
        }
    }

    /* ---------- 4.  fallback: any remaining unshot cell --------------- */
    for (int idx = 0; idx < ROWS * COLS; ++idx)
        if (enemy_game_field[idx] == '0') {
            *shot = (Vec2){ idx / COLS, idx % COLS };
            return 1;
        }

    return 0;   /* board exhausted – should never happen in normal play */
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

void USART2_IRQHandler(void) // Gets an Interupt on a incoming Message
{
	//static int ret;					  // You can do some error checking
	if (USART2->ISR & USART_ISR_RXNE) // Check if RXNE flag is set (data received)
	{
		uint8_t c = USART2->RDR;					// Read received byte from RDR (this automatically clears the RXNE flag)
		fifo_put((Fifo_t *)&uart_rx_fifo, c); // Put incoming Data into the FIFO Buffer for later handling //ret = 

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

	// ---------------- ADC Pin Configuration (PA0) ----------------
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;		// ADC und GPIOA Takt aktivieren
    RCC->AHBENR  |= RCC_AHBENR_GPIOAEN;

    GPIOA->MODER |= (3 << (0 * 2)); 		// Analogmodus für PA0

    ADC1->CR |= ADC_CR_ADCAL;				// Kalibriere ADC
    while (ADC1->CR & ADC_CR_ADCAL); 		// Warte auf Ende

    ADC1->CR |= ADC_CR_ADEN;				// Aktivieren
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) {}; 	// Warte bis ADC bereit

	fifo_init((Fifo_t *)&uart_rx_fifo); 	// Init the FIFO

	for (;;) // Infinite loop
	{
		switch (nState)
		{
		case 0: // Make Game
			seed_rng_from_adc();
			srand(adc_value);

			int field_err = create_field();
			memcpy(my_game_field_copy, my_game_field, sizeof(my_game_field));
			int checksum_err = calc_checksum();

			memset(enemy_game_field, '0', sizeof(enemy_game_field));
			hits = 30;
			result = '0';

			if (checksum_err == 0 && field_err == 0) 
				nState = 1;

		case 1: // Game Start
			// Wait for Game Start
			if (strcmp((const char *)uart_read_out, "HD_START\r\n") == 0)
			{
				uart_read_out[0] = '\0';
				LOG("DH_START_KEVIN\r\n");
				nState = 2;
			}
			break;

		case 2: // Checksum
			if (strncmp((const char *)uart_read_out, "HD_CS_", 6) == 0)
			{
				for (int i = 0; i <= 9; i++)
					enemy_cs[i] = uart_read_out[i + 6];
				enemy_cs[10] = '\0';
				uart_read_out[0] = '\0';

				LOG("DH_CS_");
				for (int i = 0; i < COLS; i++)
					LOG("%c", my_cs[i]);
				LOG("\r\n");

				nState = 3;
			}
			break;

		case 3: // Enemy Shot
			// Wait for Boom
			if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0)
			{
				position.x = uart_read_out[8] - '0';
				position.y = uart_read_out[10] - '0';
				uart_read_out[0] = '\0';
				nState = 4;
			} else if (strcmp((const char *)uart_read_out, "HD_START\r\n") == 0)
			{
				//nState = 1;
				LOG("expected DH_BOOM_ message, got something else\r\n")
			}
			break;

		case 4: // My HM
			if (my_game_field[position.x * COLS + position.y] == '0' && 
				my_game_field_copy[position.x * COLS + position.y] != 'M')
			{
				my_game_field_copy[position.x * COLS + position.y] = 'M';
				LOG("DH_BOOM_M\r\n");
				nState = 5;
				break;
			}
			else if (my_game_field[position.x * COLS + position.y] != '0' && 
					 my_game_field_copy[position.x * COLS + position.y] != 'H')
			{
				my_game_field_copy[position.x * COLS + position.y] = 'H';
				hits -= 1;
				if (hits <= 0)
				{ 
					nState = 8;
					break;
				}
				else LOG("DH_BOOM_H\r\n");
				nState = 5;
			}
			break;

		case 5: // My Shot
			choose_shot(result, pre_position, &position);

			LOG("DH_BOOM_%d_%d\r\n", position.x, position.y);
			pre_position = position;
			nState = 6;
			break;

		case 6: // Enemy HM
			// Wait for HM or Win Message
			if (strncmp((const char *)uart_read_out, "HD_SF", 5) == 0)
			{
				nState = 7;
			}
			else if (strncmp((const char *)uart_read_out, "HD_BOOM_", 8) == 0)
			{
				result = uart_read_out[8];
				uart_read_out[0] = '\0';
				enemy_game_field[position.x * COLS + position.y] = result;
				if (result == 'H' && enemy_cs[position.x] - '0' > 0) enemy_cs[position.x]--;
				nState = 3;
			}
			break;

		case 7: // Win
			if (strncmp((const char *)uart_read_out, "HD_SF", 5) == 0)
			{
				int row = uart_read_out[5] - '0';
				for (int i = 0; i < COLS; i++)
					enemy_game_field[row * COLS + i] = uart_read_out[i + 7];
				uart_read_out[0] = '\0';

				if (row == 9)
					nState = 8;
			}
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
			break;
		
		}
	}
	return 0;
}