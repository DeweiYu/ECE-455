/*
*	ECE 455 Lab Project 1
* 	February 27, 2025
*/

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include "stm32f4_discovery.h"
#include <math.h>
/* Kernel includes. */
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

// Define the maximum value of the potentiometer and a value for one second
#define MAX_POT 4095 // based on max ADC value that can be represented in 12 bits
#define ONE_SEC 1000 // value in milliseconds

// Define the GPIO pins for LEDs and Shift Registers
uint32_t Red_Light = GPIO_Pin_0;
uint32_t Yellow_Light = GPIO_Pin_1;
uint32_t Green_Light = GPIO_Pin_2;
uint32_t Pot_Output = GPIO_Pin_3;
uint32_t Shift_Register_Data = GPIO_Pin_6;
uint32_t Shift_Register_Clock = GPIO_Pin_7;
uint32_t Shift_Register_Reset = GPIO_Pin_8;

// Define the queues
xQueueHandle xQueue_POT = 0;
xQueueHandle xQueue_Traffic_Generated = 0;
xQueueHandle xQueue_Lights_Status = 0;
// Define the timers
TimerHandle_t  xGreen_Light = 0;
TimerHandle_t  xYellow_Light = 0;
TimerHandle_t  xRed_Light = 0;

// Stubs for the functions
static void prvSetupHardware(void);
static void GPIO_Init_C(void);
static void ADC_Init_C(void);
static uint16_t Get_ADC_Value(void);
void led_off(uint32_t Shift_Register_Data, uint32_t Shift_Register_Clock);
void led_on(uint32_t Shift_Register_Data, uint32_t Shift_Register_Clock);
void output_cars(int cars[19]);
void shift_with_stop(int cars[20]);
static void Traffic_Flow_Adjustment_Task(void *pvParameters);
static void Traffic_Generator_Task(void *pvParameters);
static void Traffic_Light_State_Task(void *pvParameters);
static void System_Display_Task(void *pvParameters);
static void vGreenLightCallBack(TimerHandle_t xTimer);
static void vYellowLightCallBack(TimerHandle_t xTimer);
static void vRedLightCallBack(TimerHandle_t xTimer);


/*-----------------------------------------------------------*/

/*
 * Main
 */

/*-----------------------------------------------------------*/

int main(void)
{
	prvSetupHardware();

	// Enable the GPIO Clock
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	// Enable clock for ADC
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

	// Initialize the GPIO pins for the LEDs and the Shift Registers
	GPIO_Init_C();
	// Initialize the ADC
	ADC_Init_C();

	// Create queues for potentiometer value, indicate when traffic generated, and which traffic light is on 
	xQueue_POT = xQueueCreate(1, sizeof(uint16_t));
	xQueue_Traffic_Generated = xQueueCreate(1, sizeof(uint16_t));
	xQueue_Lights_Status = xQueueCreate(1, sizeof(uint32_t));

	// Create tasks for traffic flow adjustment, traffic generation, traffic light state, and system display
	// All tasks have the same priority, 1
	xTaskCreate(Traffic_Flow_Adjustment_Task, "Traffic_Flow_Adjustment_Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(Traffic_Generator_Task, "Traffic_Generator_Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(Traffic_Light_State_Task, "Traffic_Light_State_Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(System_Display_Task, "System_Display_Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	
	// Create timers for red, yellow, and green traffic lights
	xGreen_Light = xTimerCreate("Green_Light_Timer", pdMS_TO_TICKS(2 * ONE_SEC), pdFALSE, (void *) 0, vGreenLightCallBack);
	xYellow_Light = xTimerCreate("Yellow_Light_Timer", pdMS_TO_TICKS(ONE_SEC), pdFALSE, (void *) 0, vYellowLightCallBack);
	xRed_Light = xTimerCreate("Red_Light_Timer", pdMS_TO_TICKS(2 * ONE_SEC), pdFALSE, (void *) 0, vRedLightCallBack);
	
	// Start the tasks and timer running.
	vTaskStartScheduler();

	// Should not reach here.
	for( ;; );
}

/*-----------------------------------------------------------*/

/*
* Helper Functions
*/

/*-----------------------------------------------------------*/

/*
 * Based on provided lab slides and video
 */
static void GPIO_Init_C (void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	// Configure the GPIO pin for LEDs
	GPIO_InitStructure.GPIO_Pin = Red_Light | Yellow_Light | Green_Light | Shift_Register_Data | Shift_Register_Clock | Shift_Register_Reset;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*-----------------------------------------------------------*/

/*
 * Based on provided lab slides and video
 */
static void ADC_Init_C(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

	// Configure the GPOIO Pin for POT
	GPIO_InitStruct.GPIO_Pin = Pot_Output;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

	// Initialize ADC
	ADC_InitTypeDef ADC_InitStruct;
	ADC_InitStruct.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStruct.ADC_ExternalTrigConv = DISABLE;
	ADC_InitStruct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
	ADC_InitStruct.ADC_NbrOfConversion = 1;
	ADC_InitStruct.ADC_Resolution = ADC_Resolution_12b;
	ADC_InitStruct.ADC_ScanConvMode = DISABLE;
	ADC_Init(ADC1, &ADC_InitStruct);
	
	// Enable the ADC
	ADC_Cmd(ADC1, ENABLE);

	// Select input channel for ADC
	ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 1, ADC_SampleTime_3Cycles);
}

/*-----------------------------------------------------------*/

/*
 * Based on provided lab slides and video
 */
static uint16_t Get_ADC_Value(void)
{
	uint16_t data;
	// Start ADC conversion
	ADC_SoftwareStartConv(ADC1);
	// Wait until conversion is finished
	while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

	// Get the value
	data =  ADC_GetConversionValue(ADC1);
	return data;
}

/*-----------------------------------------------------------*/

/*
 * Based on provided lab slides
 * Given data and clock pins for the shift register, set the next led to low (off)
 */
void led_off(uint32_t Shift_Register_Data, uint32_t Shift_Register_Clock) 
{
	GPIO_ResetBits(GPIOC, Shift_Register_Data);
	GPIO_ResetBits(GPIOC, Shift_Register_Clock);
	GPIO_SetBits(GPIOC, Shift_Register_Clock);
}

/*-----------------------------------------------------------*/

/*
 * Based on provided lab slides
 * Given data and clock pins for the shift register, set the next led to high (on)
 */
void led_on(uint32_t Shift_Register_Data, uint32_t Shift_Register_Clock) 
{
	GPIO_SetBits(GPIOC, Shift_Register_Data);
	GPIO_ResetBits(GPIOC, Shift_Register_Clock);
	GPIO_SetBits(GPIOC, Shift_Register_Clock);
	GPIO_ResetBits(GPIOC, Shift_Register_Data);
}

/*-----------------------------------------------------------*/

/*
 * Given an array representing the current traffic, display the traffic with the shift registers and LEDs.
 * First element is leftmost LED, last element is rightmost LED.
 */
void output_cars(int cars[19]) 
{
	// If element of array is 1, turn led on, else leave led off
	for(int i = 18; i >= 0; i--)
	{
		if(cars[i] == 1)
			led_on(Shift_Register_Data, Shift_Register_Clock);
		else
			led_off(Shift_Register_Data, Shift_Register_Clock);
	}
}

/*-----------------------------------------------------------*/


/*
 * Based on provided lab slides
 *
 */
void shift_with_stop(int cars[19])
{
	// Indexes 0-7 are cars before stop light, move forward only if there's space
	for(int i = 7; i > 0; i--)
	{
		// No car in index, move forward previous car
		if(cars[i] == 0)
		{
			cars[i] = cars[i-1];
			cars[i-1] = 0;
		}
	}

	// Indexes 8-18 are cars after stop light, move all forward
	for(int i = 18; i > 8; i--)
	{
		cars[i] = cars[i-1];
		cars[i-1] = 0;
	}
}

/*-----------------------------------------------------------*/

/*
 * Tasks
 */

/*-----------------------------------------------------------*/

/*
 * Task checks the potentiometer value using the ADC, and adds value to POT queue.
 */
void Traffic_Flow_Adjustment_Task(void *pvParameters)
{
	// Initialize the value of potentiometer to 0
	int pot_value = 0;
	while(1)
	{
		// Read the value of potentiometer and store it in POT
		pot_value = Get_ADC_Value();

		// Send the value of potentiometer to the queue
		if(xQueueOverwrite(xQueue_POT, &pot_value) == pdTRUE)
			// Delay the task for one second
			vTaskDelay(pdMS_TO_TICKS(ONE_SEC));
	}
}

/*-----------------------------------------------------------*/

/*
 * Task generates new traffic by adding 1 to the Traffic Generated queue. The rate at which the task runs
 * is inversely proportional to the potentiometer value in the POT queue.
 */
void Traffic_Generator_Task(void *pvParameters)
{
	int pot_value = 0;
	int generate_flag = 1;

	while(1)
	{
		// If the queue is not empty, we read the value of potentiometer and calculate the period of generation
		if(xQueuePeek(xQueue_POT, &pot_value, (TickType_t) ONE_SEC) == pdTRUE)
		{
			xQueueOverwrite(xQueue_Traffic_Generated, &generate_flag);
			
			// Delay the task for the calculated period, inversely proportional to traffic flow
			// as a result the traffic generation will appear directly proportional to traffic flow
			double factor = ((double)(MAX_POT - pot_value)) / MAX_POT;
			
			// Multiply by 12 as this was experimentally determined to have correct spacing between cars
			vTaskDelay(pdMS_TO_TICKS((int)ceil(12 * factor * ONE_SEC)));
		}
	}
}

/*-----------------------------------------------------------*/

/*
 * Task starts the green light timer, the first in a series of traffic light timers. Cannot guarentee POT value has been added to queue,
 * so default to green light being on for one second.
 */
void Traffic_Light_State_Task(void *pvParameters)
{
	// Start the green light timer, which will trigger the callback to turn on the yellow light upon expiration
	xTimerStart(xGreen_Light, 0);
	
	// Overwrite the queue with the green light status
	xQueueOverwrite(xQueue_Lights_Status, &Green_Light);

	// Delay forever, traffic light status will be handled by timers from here
	while(1) vTaskDelay(pdMS_TO_TICKS(ONE_SEC));
}

/*-----------------------------------------------------------*/

/*
 * Task displays the current traffic light, cars, and shift the car traffic using Lights Status queue and Traffic Generated queue.
 */
void System_Display_Task(void *pvParameters)
{
	uint32_t current_light;
	uint16_t generate_flag;
	// Initialize a 19 wide array representing all car LEDs
	int cars[19] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	// Reset the Shift Register for the current run
	GPIO_SetBits(GPIOC, Shift_Register_Reset);
	while(1)
	{
		// Get the current traffic light to be turned on
		if(xQueuePeek(xQueue_Lights_Status, &current_light, (TickType_t) ONE_SEC) == pdTRUE)
		{
			// Turn off all traffic lights
			GPIO_ResetBits(GPIOC, Green_Light);
			GPIO_ResetBits(GPIOC, Yellow_Light);
			GPIO_ResetBits(GPIOC, Red_Light);
			
			// Turn the current traffic light on
			GPIO_SetBits(GPIOC, current_light);
		}

		// Turn on corresponding LEDs
		output_cars(cars);

		// If the current light is green, all cars can shift forward
		if(current_light == Green_Light)
			for(int i = 18; i > 0; i--) cars[i] = cars[i-1];
		else
			// If the current light is yellow or red, shift cars behind and after the stop line forwards
			shift_with_stop(cars);
			
		// Set the first LED to off, to anticipate the next run
		cars[0] = 0;
		
		// Get the generate flag value, and set first LED accordingly
		if((xQueueReceive(xQueue_Traffic_Generated, &generate_flag, (TickType_t) ONE_SEC) == pdTRUE) && (generate_flag == 1))
			cars[0] = 1;

		// Delay the task for one second
		vTaskDelay(pdMS_TO_TICKS(ONE_SEC));
	}
}

/*-----------------------------------------------------------*/

/*
 * Timer Callbacks
 */

/*-----------------------------------------------------------*/

/*
 * Once green light timer has expired, start running the yellow light by adding it to queue, and
 * start the associated yellow light timer. Yellow light runs for a constant one second
 */
void vGreenLightCallBack(TimerHandle_t xTimer)
{
	// Start the yellow light timer, once it expires it will trigger a callback to start next light
	xTimerStart(xYellow_Light, pdMS_TO_TICKS(ONE_SEC));
	xQueueOverwrite(xQueue_Lights_Status, &Yellow_Light);
}

/*-----------------------------------------------------------*/

/*
 * Once yellow light timer has expired, start running the red light by adding it to Lights Status queue,
 * and start the associated red light timer. Red light is inversely proportional to the potentiometer 
 * that's value read from POT queue.
 */
void vYellowLightCallBack(TimerHandle_t xTimer)
{
	// Initialize the value of potentiometer to 0
	int pot_value = 0;

	// If the queue is not empty, we read the value of potentiometer and calculate the period of red light
	if(xQueuePeek(xQueue_POT, &pot_value, (TickType_t) ONE_SEC) == pdTRUE)
	{
		// Start the red light timer, once it expires it will trigger a callback to start next light
		double factor = ((double)(MAX_POT - pot_value)) / MAX_POT;
		if (factor == 0) {
			factor = 1;
		}
		xTimerChangePeriod(xRed_Light, pdMS_TO_TICKS((int)ceil(8 * factor * ONE_SEC)), 0);
		// Overwrite the queue with the red light status
		xQueueOverwrite(xQueue_Lights_Status, &Red_Light);
	}
}

/*-----------------------------------------------------------*/

/*
 * Once red light timer has expired, start running the green light by adding it to Lights Status queue,
 * and start the associated green light timer. Red light is directly proportional to the potentiometer 
 * that's value read from POT queue.
 */
void vRedLightCallBack(TimerHandle_t xTimer)
{
	int pot_value = 0;

	// If the queue is not empty, we read the value of potentiometer and calculate the period of green light
	if(xQueuePeek(xQueue_POT, &pot_value, (TickType_t) ONE_SEC) == pdTRUE)
	{
		// Start the green light timer, once it expires it will trigger a callback to start next light
		double factor = ((double)pot_value) / MAX_POT;
		if (factor == 0) {
			factor = 1;
		}
		xTimerChangePeriod(xGreen_Light, pdMS_TO_TICKS((int)ceil(8 * factor * ONE_SEC), 0);

		// Overwrite the queue with the green light status
		xQueueOverwrite(xQueue_Lights_Status, &Green_Light);
	}
}

/*-----------------------------------------------------------*/

/*
 * Lab 1 Provided Code, Default Hooks
 */
 
 /*-----------------------------------------------------------*/
 
void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.

	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}

/*-----------------------------------------------------------*/

/*
 * Lab 1 Provided Code
 */
void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}

/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.

	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}

/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
	/* Ensure all priority bits are assigned as preemption priority bits.
	http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	NVIC_SetPriorityGrouping( 0 );
}
