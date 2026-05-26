                PRESERVE8
                THUMB

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
Stack_Mem       SPACE   0x00001000
__initial_sp

                AREA    RESET, DATA, READONLY
                EXPORT  __Vectors
                EXPORT  __Vectors_End
                EXPORT  __Vectors_Size

__Vectors       DCD     __initial_sp
                DCD     Reset_Handler
                DCD     NMI_Handler
                DCD     HardFault_Handler
                DCD     MemManage_Handler
                DCD     BusFault_Handler
                DCD     UsageFault_Handler
                DCD     0, 0, 0, 0
                DCD     SVC_Handler
                DCD     DebugMon_Handler
                DCD     0
                DCD     PendSV_Handler
                DCD     SysTick_Handler
__Vectors_End
__Vectors_Size  EQU     __Vectors_End - __Vectors

                AREA    |.text|, CODE, READONLY
                EXPORT  Reset_Handler
                IMPORT  SystemInit
                IMPORT  __main

Reset_Handler   PROC
                BL      SystemInit
                BL      __main
                ENDP

                EXPORT  NMI_Handler              [WEAK]
                EXPORT  HardFault_Handler        [WEAK]
                EXPORT  MemManage_Handler        [WEAK]
                EXPORT  BusFault_Handler         [WEAK]
                EXPORT  UsageFault_Handler       [WEAK]
                EXPORT  SVC_Handler              [WEAK]
                EXPORT  DebugMon_Handler         [WEAK]
                EXPORT  PendSV_Handler           [WEAK]
                EXPORT  SysTick_Handler          [WEAK]

Default_Handler PROC
                B       .
                ENDP

NMI_Handler
HardFault_Handler
MemManage_Handler
BusFault_Handler
UsageFault_Handler
SVC_Handler
DebugMon_Handler
PendSV_Handler
SysTick_Handler
                B       Default_Handler

                ALIGN
                END
