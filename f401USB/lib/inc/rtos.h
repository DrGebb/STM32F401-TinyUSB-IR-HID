#ifndef RTOS_H
#define RTOS_H

#include "main.h"


extern osThreadId_t Task_Usb_id;
extern const osThreadAttr_t Task_Usb_att;

extern osThreadId_t Task_Main_id;
extern const osThreadAttr_t Task_Main_att;

extern osThreadId_t Task_NEC_id;
extern const osThreadAttr_t Task_NEC_att;


//tasks
void Task_Main(void *argument);

void Task_Usb(void *argument);

void Task_NEC(void *argument);



#endif