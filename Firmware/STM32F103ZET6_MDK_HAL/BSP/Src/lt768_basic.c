#include "lt768_basic.h"
#include "lt768_port.h"

void LT768_BasicInit(void)
{
    (void)LT768_PortInit();
}

void LT768_ShowBootText(const char *text)
{
    (void)text;
}
