/*
 * Alarmclock_2023_Solution.c
 *
 * Created: 20.03.2018 18:32:07
 * Author : chaos
 */ 

//#include <avr/io.h>
#include <string.h>
#include <stdio.h>
#include "avr_compiler.h"
#include "pmic_driver.h"
#include "TC_driver.h"
#include "clksys_driver.h"
#include "sleepConfig.h"
#include "port_driver.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "stack_macros.h"

#include "mem_check.h"

#include "init.h"
#include "utils.h"
#include "errorHandler.h"
#include "NHD0420Driver.h"
#include "ButtonHandler.h"


extern void vApplicationIdleHook( void );
void vLedBlink(void *pvParameters);
void vTimeHandler(void* param);
void vUIHandler(void* param);
void vButtonHandler(void* param);


/***************************************************************************************************
*                                      Button Eventgroup with Defines
***************************************************************************************************/
#define EVBUTTONS_S1	1<<0
#define EVBUTTONS_S2	1<<1
#define EVBUTTONS_S3	1<<2
#define EVBUTTONS_S4	1<<3
#define EVBUTTONS_L1	1<<4
#define EVBUTTONS_L2	1<<5
#define EVBUTTONS_L3	1<<6
#define EVBUTTONS_L4	1<<7
#define EVBUTTONS_CLEAR	0xFF
EventGroupHandle_t evButtonEvents;

/***************************************************************************************************
*                                      SystemState Eventgroup with Defines
***************************************************************************************************/
#define EV_ALARM_ON		1 << 0
EventGroupHandle_t evSystemState;

/***************************************************************************************************
*                                      FreeRTOS Idle-task. Is called when nothing else is running
***************************************************************************************************/
void vApplicationIdleHook( void )
{	
	
}

/***************************************************************************************************
*                                      Main Entry Point of Program
***************************************************************************************************/
int main(void)
{
    vInitClock();
	vInitDisplay();
	
	//Initialize EventGroups
	evButtonEvents = xEventGroupCreate();
	evSystemState = xEventGroupCreate();
	
	//Create Tasks
	xTaskCreate( vLedBlink, (const char *) "ledBlink", configMINIMAL_STACK_SIZE+10, NULL, 1, NULL);
	xTaskCreate(vButtonHandler, (const char*) "btTask", configMINIMAL_STACK_SIZE+30, NULL, 2, NULL);
	xTaskCreate(vTimeHandler, (const char*) "timeTask", configMINIMAL_STACK_SIZE+30, NULL, 3, NULL);
	xTaskCreate(vUIHandler, (const char*) "uiTask", configMINIMAL_STACK_SIZE+100, NULL, 1, NULL);
	
	//Start FreeRTOS Scheduler
	vTaskStartScheduler();
	return 0;
}

/***************************************************************************************************
*                                      Global Time Variables
***************************************************************************************************/
uint8_t time_hour = 18;
uint8_t time_min = 15;
uint8_t time_sec = 0;
uint8_t alarm_hour = 21;
uint8_t alarm_min = 35;
uint8_t alarm_sec = 0;

/***************************************************************************************************
*                                      TimeHandler Task to track hours, mins and seconds.
***************************************************************************************************/
void vTimeHandler(void* param) {
	TickType_t lasttime = xTaskGetTickCount();
	for(;;) {
		time_sec++;
		if(time_sec >= 60) {
			time_sec = 0;
			time_min++;
		}
		if(time_min >= 60) {
			time_min = 0;
			time_hour++;
		}
		if(time_hour >= 24) {
			time_hour = 0;
		}
		vTaskDelayUntil(&lasttime, 1000/portTICK_RATE_MS);
	}
}

/***************************************************************************************************
*                                      Helper Function to increment Time
***************************************************************************************************/
void incrementTime(uint8_t timeslot) {
	switch(timeslot) {
		case 0:
			time_hour++;
		break;
		case 1:
			time_min++;
		break;
		case 2:
			time_sec++;
		break;
	}
}

/***************************************************************************************************
*                                      Helper Function to decrement Time
***************************************************************************************************/
void decrementTime(uint8_t timeslot) {
	switch(timeslot) {
		case 0:
			time_hour--;
		break;
		case 1:
			time_min--;
		break;
		case 2:
			time_sec=0;
		break;
	}
}

/***************************************************************************************************
*                                      Helper Function to increment Alarm Time
***************************************************************************************************/
void incrementAlarm(uint8_t timeslot) {
	switch(timeslot) {
		case 0:
			alarm_hour++;
		break;
		case 1:
			alarm_min++;
		break;
		case 2:
			alarm_sec++;
		break;
	}
}

/***************************************************************************************************
*                                      Helper Function to decrement Alarm Time
***************************************************************************************************/
void decrementAlarm(uint8_t timeslot) {
	switch(timeslot) {
		case 0:
			alarm_hour--;
		break;
		case 1:
			alarm_min--;
		break;
		case 2:
			alarm_sec--;
		break;
	}
}

/***************************************************************************************************
*                                      Helper Function to check if Alarm Time is reached
***************************************************************************************************/
bool checkIfAlarmTime() {
	if(time_hour == alarm_hour && time_min == alarm_min && time_sec == alarm_sec) {
		return true;
	}
	return false;
}

/***************************************************************************************************
*                                      UIModes for Finite State Machine
***************************************************************************************************/
#define UIMODE_INIT		0
#define UIMODE_MAIN		1
#define UIMODE_TIMESET	2
#define UIMODE_ALARMSET 3
#define UIMODE_ALARM	4

uint8_t uiMode = UIMODE_INIT;

/***************************************************************************************************
*                                      UIHandler Task with Finite State Machine for UIModes
***************************************************************************************************/
void vUIHandler(void* param) {
	char timestring[20] = "              "; //Helper Variable for printing complex variables to screen
	bool alarmOn = false; //Helper Varaible to save if alarm is on/off
	uint8_t uidelay = 10; //Helper Variable to be able to delay certain effects in the FSM
	uint8_t timeslot = 0; //Helper Variable to save selected timeslot when setting time/alarm
	for(;;) {
		vDisplayClear(); //Always clear Display first
		uint32_t buttonState = (xEventGroupGetBits(evButtonEvents)) & 0x000000FF; //Read Button States from EventGroup
		xEventGroupClearBits(evButtonEvents, EVBUTTONS_CLEAR); //As the Button State is saved now, we can clear the EventGroup for new Button presses
		switch(uiMode) { //Primary Finite State Machine for UIMode handling
			case UIMODE_INIT: { //Completely unnescessary, but neat startup effect
				vDisplayWriteStringAtPos(0,0, "ALARM-CLOCK HS 2023"); 
				switch(uidelay) { 
					case 10:
						timestring[0] = '.';
					break;
					case 9:
					timestring[1] = '.';
					break;
					case 8:
						timestring[2] = '.';
					break;
					case 7:
						timestring[3] = '.';
					break;
					case 6:
						timestring[4] = '.';
					break;
					case 5:
						timestring[5] = '.';
					break;
					case 4:
						timestring[6] = '.';
					break;
					case 3:
						timestring[7] = '.';
					break;
					case 2:
						timestring[8] = '.';
					break;
					case 1:
						timestring[9] = '.';
					break;
				}
				vDisplayWriteStringAtPos(2,0, "Loading.%s", timestring);
				if(uidelay > 0) {
					uidelay--;
				} else {				
					uiMode = UIMODE_MAIN; //When delay is reached, move on to Main-Screen
				}
			}
			break;
			case UIMODE_MAIN: //Main Menu
				//Prepare Data to put on Screen
				sprintf(&timestring[0], "%.2i:%.2i:%.2i", time_hour, time_min, time_sec);
				//Write to Screen
				vDisplayWriteStringAtPos(0,0, "ALARM-CLOCK 2023");
				vDisplayWriteStringAtPos(1,0, "Time:  %s", timestring);
				sprintf(&timestring[0], "%.2i:%.2i:%.2i", alarm_hour, alarm_min, alarm_sec);
				vDisplayWriteStringAtPos(2,0, "Alarm: %s", timestring);
				//Depending on alarm state ON/OFF is written to screen
				if(alarmOn == false) {
					vDisplayWriteStringAtPos(2,16, "OFF");
				} else {
					vDisplayWriteStringAtPos(2,16, "ON");
				}
				vDisplayWriteStringAtPos(3,0, "    | EA |_SA_|_ST_", timestring);
				//Change UIMode according to buttonpresses
				if((buttonState & EVBUTTONS_L4) != 0) {
					uiMode = UIMODE_TIMESET;
					timeslot = 0;
				}
				if(buttonState & EVBUTTONS_L3) {
					uiMode = UIMODE_ALARMSET;
					timeslot = 0;
				}
				if(buttonState & EVBUTTONS_S2) {
					alarmOn = (alarmOn?false:true);
				}
				if(alarmOn && checkIfAlarmTime()) {
					uiMode = UIMODE_ALARM;
					uidelay = 50; // After 10s Alarm Menu shall go back to main menu
					xEventGroupSetBits(evSystemState, EV_ALARM_ON); //Enable LED Blinker
				}
			break;
			case UIMODE_TIMESET: //Menu to configure time
				//Prepare Data to put on Screen
				sprintf(&timestring[0], "%.2i:%.2i:%.2i", time_hour, time_min, time_sec);				
				//Write to Screen
				vDisplayWriteStringAtPos(0,0, "Set Time:");				
				vDisplayWriteStringAtPos(1,0, "Time:  %s", timestring);
				vDisplayWriteStringAtPos(3,0, "  - | +  | >  |BACK", timestring);
				//Change UIMode according to buttonpresses
				if((buttonState & EVBUTTONS_S4) != 0) {
					uiMode = UIMODE_MAIN;
				}
				if((buttonState & EVBUTTONS_S1) != 0) {
					decrementTime(timeslot);
				}
				if((buttonState & EVBUTTONS_S2) != 0) {
					incrementTime(timeslot);
				}
				if((buttonState & EVBUTTONS_S3) != 0) {
					timeslot++;
					if(timeslot >= 3) {
						timeslot = 0;
					}
				}
				//Print Timeslot indicator below selected timeslot hour/min/sec
				vDisplayWriteStringAtPos(2,(7+(timeslot*3)), "^^");
			break;
			case UIMODE_ALARMSET: //Menu to configure Alarm
				//Prepare Data to put on Screen
				sprintf(&timestring[0], "%.2i:%.2i:%.2i", alarm_hour, alarm_min, alarm_sec);
				//Write to Screen
				vDisplayWriteStringAtPos(0,0, "Set Alarm");				
				vDisplayWriteStringAtPos(2,0, "Alarm: %s", timestring);
				vDisplayWriteStringAtPos(3,0, "  - | +  | >  |BACK", timestring);
				//Change UIMode according to buttonpresses
				if((buttonState & EVBUTTONS_S4) != 0) {
					uiMode = UIMODE_MAIN;
				}
				if((buttonState & EVBUTTONS_S1) != 0) {
					decrementAlarm(timeslot);
				}
				if((buttonState & EVBUTTONS_S2) != 0) {
					incrementAlarm(timeslot);
				}
				if((buttonState & EVBUTTONS_S3) != 0) {
					timeslot++;
					if(timeslot >= 3) {
						timeslot = 0;
					}
				}
				//Print Timeslot indicator above selected timeslot hour/min/sec
				vDisplayWriteStringAtPos(1,(7+(timeslot*3)), "vv");
			break;
			case UIMODE_ALARM: //Alarm Menu
				vDisplayWriteStringAtPos(0,0, "  !A-L-A-R-M!");
				vDisplayWriteStringAtPos(3,0, "    |    |    |BACK");
				if(uidelay > 0) { //When alarm is not acknowledged within uidelay, state goes back to main-menu
					if(--uidelay == 0) {
						xEventGroupClearBits(evSystemState, EV_ALARM_ON); //Disable Blinking
						uiMode = UIMODE_MAIN;
					}
				}
				//Change UIMode according to buttonpresses
				if((buttonState & EVBUTTONS_S4) != 0) {
					xEventGroupClearBits(evSystemState, EV_ALARM_ON); //Disable Blinking
					uiMode = UIMODE_MAIN;
				}				
			break;
		}
		vTaskDelay(200/portTICK_RATE_MS); //FSM Delay
	}
}

/***************************************************************************************************
*                                      ButtonHandler Task to send Button Events to UIHandler
***************************************************************************************************/
void vButtonHandler(void* param) {
	initButtons(); //Initialize Buttonhandler
	for(;;) {
		updateButtons(); //Update Button States
		
		//Read Button State and set EventBits in EventGroup
		if(getButtonPress(BUTTON1) == SHORT_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_S1);
		}
		if(getButtonPress(BUTTON2) == SHORT_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_S2);
		}
		if(getButtonPress(BUTTON3) == SHORT_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_S3);
		}
		if(getButtonPress(BUTTON4) == SHORT_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_S4);
		}
		if(getButtonPress(BUTTON1) == LONG_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_L1);
		}
		if(getButtonPress(BUTTON2) == LONG_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_L2);
		}
		if(getButtonPress(BUTTON3) == LONG_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_L3);
		}
		if(getButtonPress(BUTTON4) == LONG_PRESSED) {
			xEventGroupSetBits(evButtonEvents, EVBUTTONS_L4);
		}
		vTaskDelay((1000/BUTTON_UPDATE_FREQUENCY_HZ)/portTICK_RATE_MS); //Buttonupdate Delay
	}
}

/***************************************************************************************************
*                                      LedBlink Handler to let LED's blink when Alarm enabled
***************************************************************************************************/
void vLedBlink(void *pvParameters) {
	(void) pvParameters;
	//Initialize LED's
	PORTF.DIRSET = 0x0F;
	PORTE.DIRSET = 0x0F;
	PORTF.OUT = 0x0F;
	PORTE.OUT = 0x0F;
	uint8_t blinkstate = 0;
	for(;;) {
		if(xEventGroupGetBits(evSystemState) & EV_ALARM_ON) {
			//When Alarm is activated, LEDS shall blink
			blinkstate ^=0x01; //toggle Alarm helper variable to let LED's blink
			if(blinkstate == 0) {
				PORTF.OUTSET = 0x0F;
				PORTE.OUTCLR = 0x0F;
			} else {
				PORTF.OUTCLR = 0x0F;
				PORTE.OUTSET = 0x0F;	
			}
			
		} else {
			// When Alarm State is off, disable LED's
			PORTF.OUT = 0x0F;
			PORTE.OUT = 0x0F;
		}
		vTaskDelay(100 / portTICK_RATE_MS); //Blink Delay
	}
}
