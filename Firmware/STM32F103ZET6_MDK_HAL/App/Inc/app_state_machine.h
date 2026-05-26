#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_USB_MSC
} AppBootMode;

void AppStateMachine_Init(AppBootMode mode);
void AppStateMachine_Task(void);

#endif
