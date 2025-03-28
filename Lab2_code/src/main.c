/*
 * ECE 455
 * Spring 2025
 * Project 2
 */

/* Lab 1 Code Provided Includes */
#include <stdint.h>
#include <stdio.h>
#include "stm32f4_discovery.h"
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

#define UNIT_TIME 10

/* Create enum Types */
// Lab Manual recommended task_type enum
typedef enum {
	PERIODIC, 
	APERIODIC
} task_type;

// Request Task Status
typedef enum {
	ACTIVE = 0,
	COMPLETE = 1,
	OVERDUE = 2
} task_status;


/* Create struct Types */
// Lab Manual recommended task definition
typedef struct 
{
	TaskHandle_t t_handle;
	task_type type;
	uint32_t task_id;
	uint32_t release_time;
	uint32_t absolute_deadline;
	uint32_t completion_time;
	uint32_t execution_time;
	uint32_t unique_num;
} dd_task;

// Lab Manual recommended task list definition (single linked list)
// Note: struct is necessary for recursive defintion to compile, and
//  pointer must be defined with type* NOT type *
typedef struct dd_task_list {
	dd_task* task;
	struct dd_task_list* next_task;
} dd_task_list;

// Store basic info needed to complete a task
typedef struct 
{
	uint32_t unique_num;
	uint32_t completion_time;
} dd_task_complete;


/* Supporting Functions Prototypes */
// Setup
static void prvSetupHardware(void);

// Access DDS
void create_dd_task(task_type type, uint32_t task_id, uint32_t absolute_deadline, uint32_t execution_time);
void delete_dd_task(uint32_t unique_num);
dd_task_list** get_active_dd_task_list(void);
dd_task_list** get_completed_dd_task_list(void);
dd_task_list** get_overdue_dd_task_list(void);

// Help DDS
void add_task_to_task_list(dd_task_list **head, dd_task *task);
dd_task* remove_task_from_task_list(dd_task_list **head, uint32_t task_id);
void update_dd_task_priority(dd_task_list *head);

// Help User Tasks
int count_dd_tasks(dd_task_list **head);

/* FreeRTOS Setup */
// Define Tasks
void Deadline_Driven_Scheduler(void *pvParameters);
void Deadline_Driven_Task_Generator1(void *pvParameters);
void Deadline_Driven_Task_Generator2(void *pvParameters);
void Deadline_Driven_Task_Generator3(void *pvParameters);
void Monitor_Task(void *pvParameters);
void User_Defined_Task(void *pvParameters); 

// Define Queues
xQueueHandle xQueue_Release_Task = 0;
xQueueHandle xQueue_Complete_Task = 0;
xQueueHandle xQueue_Get_Task_List = 0;
xQueueHandle xQueue_Return_Active_List = 0;
xQueueHandle xQueue_Return_Complete_List = 0;
xQueueHandle xQueue_Return_Overdue_List = 0;

/* Main Functionality */
int main(void)
{
	prvSetupHardware();
	
	// Create Queues
	xQueue_Release_Task = xQueueCreate(3, sizeof(dd_task *));				// used to create a task
	xQueue_Complete_Task = xQueueCreate(3, sizeof(dd_task_complete *));		// used to complete an active task
	xQueue_Get_Task_List = xQueueCreate(1, sizeof(task_status *));			// used to request task list
	xQueue_Return_Active_List = xQueueCreate(1, sizeof(dd_task_list *));		// used to return pointer to requested task list
	xQueue_Return_Complete_List = xQueueCreate(1, sizeof(dd_task_list *));		// used to return pointer to requested task list
	xQueue_Return_Overdue_List = xQueueCreate(1, sizeof(dd_task_list *));		// used to return pointer to requested task list

	// Create the tasks defined within this file.
	xTaskCreate(Deadline_Driven_Scheduler, "DD Scheduler", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
	xTaskCreate(Deadline_Driven_Task_Generator1, "DD Task 1 Generator", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
	xTaskCreate(Deadline_Driven_Task_Generator2, "DD Task 2 Generator", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
	xTaskCreate(Deadline_Driven_Task_Generator3, "DD Task 3 Generator", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
	xTaskCreate(Monitor_Task, "Monitor", configMINIMAL_STACK_SIZE, NULL, 4, NULL);

	/* Start the tasks and timer running. */
	vTaskStartScheduler();

	// Should not reach here
	while(1);
}


/* Supporting Functions Implementation */

/*
 * Function that creates a task using the dd_task struct, then adds the task to xQueue_Release_Task
 */
void create_dd_task(task_type type, uint32_t task_id, uint32_t absolute_deadline, uint32_t execution_time)
{
	// Allocate memory to store task
	dd_task* task = (dd_task *) pvPortMalloc(sizeof(dd_task));
	
	TickType_t current_time = xTaskGetTickCount();
	
	// Define known task values
	task->type = type;
	task->task_id = task_id;
	task->release_time = current_time;
	task->absolute_deadline = current_time + pdMS_TO_TICKS(absolute_deadline);
	task->execution_time = execution_time;
	
	// Define placeholder task values
	task->t_handle = NULL;
	task->completion_time = -1;
	task->unique_num = -1;

	// Add task to queue to be released
	xQueueSend(xQueue_Release_Task, &task, UNIT_TIME);
}

/*
 * Function that completes a task, by adding task_id to xQueue_Complete_Task
 */
void delete_dd_task(uint32_t unique_num)
{
	TickType_t current_time = xTaskGetTickCount();
	dd_task_complete *task = (dd_task_complete *) pvPortMalloc(sizeof(dd_task_complete));
	task->unique_num = unique_num;
	task->completion_time = current_time;
	
	// Add task to queue to be completed
	xQueueSend(xQueue_Complete_Task, &task, UNIT_TIME);
}

/*
 * Function that requests active task list through xQueue_Get_Task_List, and waits for
 *  a response in the xQueue_Return_Task_List
 */
dd_task_list** get_active_dd_task_list(void)
{
	task_status status = ACTIVE;
	xQueueSend(xQueue_Get_Task_List, &status, UNIT_TIME);
	
	vTaskDelay(pdMS_TO_TICKS(1));
	
	dd_task_list **task_list = NULL;
	if(xQueueReceive(xQueue_Return_Active_List, &task_list, 15 * UNIT_TIME) == pdPASS)
	{
		return task_list;
	}
	return NULL;
}

/*
 * Function that requests complete task list through xQueue_Get_Task_List, and waits for
 *  a response in the xQueue_Return_Task_List
 */
dd_task_list** get_completed_dd_task_list(void)
{
	task_status status = COMPLETE;
	xQueueSend(xQueue_Get_Task_List, &status, UNIT_TIME);
	
	vTaskDelay(pdMS_TO_TICKS(1));
	
	dd_task_list **task_list = NULL;
	if(xQueueReceive(xQueue_Return_Complete_List, &task_list, 10 * UNIT_TIME) == pdPASS)
	{
		return task_list;
	}
	return NULL;
}

/*
 * Function that requests overdue task list through xQueue_Get_Task_List, and waits for
 *  a response in the xQueue_Return_Task_List
 */
dd_task_list** get_overdue_dd_task_list(void)
{
	task_status status = OVERDUE;
	xQueueSend(xQueue_Get_Task_List, &status, UNIT_TIME);
	
	vTaskDelay(pdMS_TO_TICKS(1));
	
	dd_task_list **task_list = NULL;
	if(xQueueReceive(xQueue_Return_Overdue_List, &task_list, 10 * UNIT_TIME) == pdPASS)
	{
		return task_list;
	}
	return NULL;
}


/*
 * Function adds a given task to the end of a given list
 */
void add_task_to_task_list(dd_task_list **head, dd_task *task)
{
	// Allocate memory for new list item
	dd_task_list *new = (dd_task_list *) pvPortMalloc(sizeof(dd_task_list));
	new->task = task;
	
	// sort task into list by absolute_deadline, 
	//  if they have same deadline put new task at the end
	dd_task_list *current = *head;
	dd_task_list *previous = NULL;
	if(current == NULL) {
		new->next_task = NULL;
		*head = new;
	} else {
		while (current->next_task != NULL && current->task->absolute_deadline <= task->absolute_deadline) {
			previous = current;
			current = current->next_task;
		}
		if (current != NULL) {
			new->next_task = current->next_task;
			current->next_task = new;
		} else {
			new->next_task = NULL;
			previous->next_task = new;
		}
	}
}

/*
 * Function removes a given task from the given list, and returns removed task
 */
dd_task* remove_task_from_task_list(dd_task_list **head, uint32_t unique_num)
{
	dd_task *task = NULL;
	dd_task_list *current = *head;
	dd_task_list *previous = NULL;
	while(current != NULL && current->task->unique_num != unique_num)
	{
		previous = current;
		current = current->next_task;
	}
	
	if(current == NULL) {
		// Unable to find given task
		return NULL;		
	} else if (previous == NULL) {
		// Task is front of list
		*head = current->next_task;
	} else {
		// Task was found, but not front of list
		previous->next_task = current->next_task;
	}
	
	task = current->task;
	vPortFree(current);
	return task;
}

/*
 * Function that updates user defined task priority based on EDF ordering
 */
void update_dd_task_priority(dd_task_list *head)
{
	dd_task_list *current = head;
	int priority = 3; // maximum priority for dd tasks
	
	while(current != NULL)
	{
		if(current->task->t_handle != NULL)
			vTaskPrioritySet(current->task->t_handle, priority);
		if (priority > 1)
			priority--;
		current = current->next_task;
	}
}

/*
 * Function that counts number of tasks in a given list
 */
int count_dd_tasks(dd_task_list **head)
{
	int count = 0;
	if (head == NULL) {
		// invalid pointer 
		return count;
	}
	
	dd_task_list *current = *head;
	
	// Loop through all tasks in list, count each one found
	while(current != NULL) 
	{
		current = current->next_task;
		count++;
	}
	
	return count;
}


/* FreeRTOS Tasks */
/* 
 * Task implements the deadline driven scheduler (DDS)
 */
void Deadline_Driven_Scheduler(void *pvParameters)
{
	int count_tasks_created = 0;
	
	// Define lists
	dd_task_list *active_head = NULL;
	dd_task_list *complete_head = NULL;
	dd_task_list *overdue_head = NULL;

	// Temporary variables for queue values
	dd_task *current_task;	
	dd_task_complete *complete_task;
	task_status list_request;
	dd_task_list **list_response = (dd_task_list **) pvPortMalloc(sizeof(dd_task_list *));

	while(1)
	{
		// Check if there is a released task
		while(xQueueReceive(xQueue_Release_Task, &current_task, UNIT_TIME) == pdPASS)
		{
			current_task->unique_num = count_tasks_created;
			printf("Task %d is released at %d ms\n", (int) (current_task->task_id + 1), (int) (current_task->release_time));
			
			// Create user defined task for the released task
			char name[8];
			sprintf(name, "Task %d", (int) (current_task->task_id + 1));
			xTaskCreate(User_Defined_Task, name, configMINIMAL_STACK_SIZE, (void *) current_task, 1, &current_task->t_handle);
			
			// Add task and determine EDF priorities
			add_task_to_task_list(&active_head, current_task);
			update_dd_task_priority(active_head);
			
			// Keep track of all tasks created
			count_tasks_created++;
		}
		
		
		// Check if there is a completed task
		while(xQueueReceive(xQueue_Complete_Task, &complete_task, UNIT_TIME) == pdPASS)
		{
			// Remove from active list, and get task object
			current_task = remove_task_from_task_list(&active_head, complete_task->unique_num);
			current_task->completion_time = complete_task->completion_time;
			update_dd_task_priority(active_head);
			
			vPortFree(complete_task);

			// End associated user defined task
			vTaskSuspend(current_task->t_handle);
			vTaskDelete(current_task->t_handle);
			
			// Add to complete or overdue lists
			if(current_task->completion_time > current_task->absolute_deadline) {
				// Task was overdue
				printf("Task %d is overdue at %d ms\n", (int) (current_task->task_id + 1), (int) (current_task->completion_time));
				add_task_to_task_list(&overdue_head, current_task);
			} else {
				// Task was completed on time
				printf("Task %d is completed at %d ms\n", (int) (current_task->task_id + 1), (int) (current_task->completion_time));
				add_task_to_task_list(&complete_head, current_task);
			}
		}
		
		// Check if there is a request for task lists
		if(xQueueReceive(xQueue_Get_Task_List, &list_request, UNIT_TIME) == pdPASS)
		{
			if (list_request == ACTIVE) {
				list_response = &active_head;
				xQueueOverwrite(xQueue_Return_Active_List, &list_response);
			} else if (list_request == COMPLETE) {
				list_response = &complete_head;
				xQueueOverwrite(xQueue_Return_Complete_List, &list_response);
			} else if (list_request == OVERDUE) {
				list_response = &overdue_head;
				xQueueOverwrite(xQueue_Return_Overdue_List, &list_response);
			} else {
				list_response = NULL;
			}
		}
		
		vTaskDelay(pdMS_TO_TICKS(5 * UNIT_TIME));
	}
}

/* 
 * Task that generates dd task 1, at a pre-determined periodic rate
 */
void Deadline_Driven_Task_Generator1(void *pvParameters)
{
	// Task 1 Details (depends on test case)
	// task_id = 0
	
	uint32_t execute = 95;
	uint32_t period = 500;
	
	while(1) 
	{
		create_dd_task(PERIODIC, 0, period, execute);
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

/* 
 * Task that generates dd task 2, at a pre-determined periodic rate
 */
void Deadline_Driven_Task_Generator2(void *pvParameters)
{
	// Task 2 Details (depends on test case)
	// task_id = 1
	
	uint32_t execute = 150;
	uint32_t period = 500;
	
	while(1) 
	{
		create_dd_task(PERIODIC, 1, period, execute);
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

/* 
 * Task that generates dd task 3, at a pre-determined periodic rate
 */
void Deadline_Driven_Task_Generator3(void *pvParameters)
{
	// Task 3 Details (depends on test case)
	// task_id = 2
	
	uint32_t execute = 250;
	uint32_t period = 750;
	
	while(1) 
	{
		create_dd_task(PERIODIC, 2, period, execute);
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

/*
 * Task that periodically prints status of the system/progress of dd tasks
 */
void Monitor_Task(void *pvParameters)
{
	TickType_t active_time, complete_time, overdue_time = 0;
	int active_count, complete_count, overdue_count = 0;
	
	while(1)
	{
		active_time = xTaskGetTickCount();
		dd_task_list **active = get_active_dd_task_list();
		active_count = count_dd_tasks(active);
		printf("Active Count at %d ms = %d\n", (int) active_time, active_count);
		
		complete_time = xTaskGetTickCount();
		dd_task_list **complete = get_completed_dd_task_list();
		complete_count = count_dd_tasks(complete);
		printf("Complete Count at %d ms = %d\n", (int) complete_time, complete_count);
		
		overdue_time = xTaskGetTickCount();
		dd_task_list **overdue = get_overdue_dd_task_list();
		overdue_count = count_dd_tasks(overdue);
		printf("Overdue Count at %d ms = %d\n", (int) overdue_time, overdue_count);

		// Delay the monitor task by a hyper period (1500 ms)
		vTaskDelay(pdMS_TO_TICKS(1500));
	}
}


/*
 * Task that runs for given execution time, representing dd task running
 */
void User_Defined_Task(void *pvParameters) 
{
	dd_task *task = (dd_task *) pvParameters;
	TickType_t previous_time, current_time = xTaskGetTickCount();
	TickType_t time_executing = pdMS_TO_TICKS(task->execution_time);
	
	// Continue to execute until task has exceeded deadline or spend enough time executing loop to reach execution time.
	// Cannot calculate execution time based on difference between previous and current time because this task
	//  may be interrupted during execution, and that time wasn't spend in this task
	while (time_executing > 0 && task->absolute_deadline > current_time)
	{
		previous_time = current_time;
		current_time = xTaskGetTickCount();
		if (previous_time != current_time) 
		{
			time_executing--;
		}
	}
	
	delete_dd_task(task->unique_num);
	
	// Wait task forever (this will be deleted by Deadline_Driven_Scheduler Task)
	while(1) vTaskDelay(pdMS_TO_TICKS(50 * UNIT_TIME));
}


// Provided in Lab 0/1 Code
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

static void prvSetupHardware( void )
{
	/* Ensure all priority bits are assigned as preemption priority bits.
	http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	NVIC_SetPriorityGrouping( 0 );

	/* TODO: Setup the clocks, etc. here, if they were not configured before
	main() was called. */
}
