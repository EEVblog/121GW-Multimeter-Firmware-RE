/*
 * Bootloader interrupt handlers.
 */
#include "sd_card.h"
#include "sys_init.h"
#include "stm32l1xx.h"

/* orig: 0x08004DE8 */
void TIM2_IRQHandler(void)
{
    TIM2->SR = (uint16_t)~TIM_SR_UIF;
    g_tick_flag = 1;
}

/* orig: 0x08004E02 */
void SDIO_IRQHandler(void)
{
    (void)sd_process_irq();
}

/* orig: 0x08004E0A */
void DMA2_Channel4_IRQHandler(void)
{
    sd_process_dma_irq();
}
