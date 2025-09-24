/*******************************************************************************
* File Name:   gfx_task.c
*
* Description: This file contains the task definition for initializing the
* graphics subsystem, allocate memory for frame buffers, draw and display the 
* images in either single buffering mode or double buffering mode based on the 
* user input.
*
* Related Document: See README.md
*
********************************************************************************
* Copyright 2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
/*******************************************************************************
* Header Files
*******************************************************************************/
#include "retarget_io_init.h"

#include "cy_graphics.h"

#include "FreeRTOS.h"
#include "task.h"

#include "vg_lite.h"
#include "vg_lite_platform.h"
#include "infineon_logo_paths.h"
#include "tiger_paths.h"
#include "gfx_task.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#define VG_PARAMS_POS        (0UL)

#define I2C_CONTROLLER_IRQ_PRIORITY         (2UL)

/*******************************************************************************
* Global Variable(s)
*******************************************************************************/
static GFXSS_Type* base = (GFXSS_Type*) GFXSS;
cy_stc_gfx_context_t gfx_context;

static vg_lite_buffer_t buffer0;
static vg_lite_buffer_t buffer1;
static vg_lite_matrix_t matrix;

/* Heap memory for VGLite to allocate memory for buffers, command, and
 * tessellation buffers
 */
CY_SECTION(".cy_gpu_buf") uint8_t contiguous_mem[VGLITE_HEAP_SIZE];

volatile void *vglite_heap_base = &contiguous_mem;

/* Variable for storing character read from terminal */
uint32_t uart_read_value;

cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc = gfxss_interrupt_dc_IRQn,
    .intrPriority = DC_INT_PRIORITY
};

cy_stc_scb_i2c_context_t disp_i2c_controller_context;

cy_stc_sysint_t disp_i2c_controller_irq_cfg =
{
    .intrSrc      = CYBSP_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

static volatile uint8_t toggle_buffer_mode = RESET_VAL;
static volatile uint8_t image_select       = RESET_VAL;

static bool double_buffer_first_iter = true;

static __IO int32_t  active_buffer  = RESET_VAL;
static __IO int32_t  pending_buffer = INVALID_VAL;

/* Struct for image parameters of Infineon logo image */
static struct image_params image_infineon = {

        .x_translate = ((DISPLAY_W / TRANSFORMATION_OFFSET_INFI) - X_POS_OFFSET_INFI),
        .y_translate = ((DISPLAY_H / TRANSFORMATION_OFFSET_INFI) - Y_POS_OFFSET_INFI),
        .x_scale = DEF_X_SCALE_INFI,
        .y_scale = DEF_Y_SCALE_INFI,
        .path_count = PATH_COUNT_INFI,
        .path_data = path_infi,
        .color_data = color_data_infi
};

/* Struct for image parameters of tiger image */
static struct image_params image_tiger = {

        .x_translate = ((DISPLAY_W / TRANSFORMATION_OFFSET_TIGER) - X_POS_OFFSET_TIGER),
        .y_translate = ((DISPLAY_H / TRANSFORMATION_OFFSET_TIGER) - Y_POS_OFFSET_TIGER),
        .x_scale = DEF_X_SCALE_TIGER,
        .y_scale = DEF_Y_SCALE_TIGER,
        .path_count = PATH_COUNT_TIGER,
        .path_data = path,
        .color_data = color_data
};

/* Array of pointers to image parameters structs */
static struct image_params *images[NUM_OF_IMAGES] = { &image_infineon, &image_tiger };

/* Array of pointers to frame buffers */
static vg_lite_buffer_t *Buffers[APP_BUFFER_COUNT] = { &buffer0, &buffer1 };


/*******************************************************************************
* Function Name: render_image
********************************************************************************
* Summary:
*  This function displays the images using single buffering and double
*  buffering mode. After a delay the next image is selected to be displayed. 
*  The buffering mode is switched by giving enter key input.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void render_image()
{
    while (true)
    {
        uart_read_value = Cy_SCB_UART_Get(CYBSP_DEBUG_UART_HW);
        if(CY_SCB_UART_RX_NO_DATA != uart_read_value)
        {
            if (ENTER_KEY_ASCII == uart_read_value)
            {
                toggle_buffer_mode ^= SET_VAL;

                if (!double_buffer_first_iter)
                {
                    double_buffer_first_iter = true;
                }
            }
        }

        /* Move cursor to previous line */
        printf("\x1b[1F");

        /* If no pending buffer to be displayed */
        if (pending_buffer < RESET_VAL)
        {
            if (!toggle_buffer_mode)
            {
                /* Single Buffering Mode */
                printf("Current mode:  Single Buffering Mode\n");

                if (image_select >= NUM_OF_IMAGES)
                {
                    image_select = RESET_VAL;
                }

                /* Draw the image in frame buffer */
                draw(Buffers[BUF_ZERO_POS], images[image_select++]);
                pending_buffer = BUF_ZERO_POS;

                /* Display the frame buffer */
                Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(Buffers[pending_buffer]->address),
                        &gfx_context);
            }
            else
            {
                /* Double Buffering Mode */
                printf("Current mode:  Double Buffering Mode\n");

                /* Check if first iteration */
                if (double_buffer_first_iter)
                {
                    if (image_select >= NUM_OF_IMAGES)
                    {
                        image_select = RESET_VAL;
                    }

                    draw(Buffers[BUF_ONE_POS-active_buffer], images[image_select++]);
                    double_buffer_first_iter = false;
                }

                pending_buffer = BUF_ONE_POS - active_buffer;

                if (image_select >= NUM_OF_IMAGES)
                {
                    image_select = RESET_VAL;
                }

                /* Display the pending frame buffer */
                Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(Buffers[pending_buffer]->address),
                        &gfx_context);

                /* Draw into the active buffer while rendering the pending buffer. */
                if (!double_buffer_first_iter)
                {
                    draw(Buffers[active_buffer], images[image_select++]);
                }
            }

            /* Delay before switching to next image */
            vTaskDelay(pdMS_TO_TICKS(IMAGE_DISPLAY_WAIT_MS));
        }
    }
}


/*******************************************************************************
* Function Name: draw
********************************************************************************
* Summary:
*  This function performs GPU aided drawing of the image into the memory
*  buffer passed as arguments to it.
*
* Parameters:
*  vg_lite_buffer_t *target: Pointer to a vglite struct.
*  struct image_params *image: Pointer to image parameters struct.
*
* Return:
*  void
*
*******************************************************************************/
void draw(vg_lite_buffer_t *target, struct image_params *image)
{
    uint8_t count;
    vg_lite_error_t error = VG_LITE_SUCCESS;

    /* Loads an identity matrix */
    vg_lite_identity(&matrix);

    /* Translate the matrix to the given coordinates */
    vg_lite_translate( image->x_translate, image->y_translate, &matrix);

    /* Set the default scale of the matrix */
    vg_lite_scale(image->x_scale, image->y_scale, &matrix);

    /* Draw the path using the matrix. */
    error = vg_lite_clear(target, NULL, LIGHT_GREEN_COLOR);
    if (error)
    {
        printf("Clear failed: vg_lite_clear() returned error %d\n", error);
        handle_app_error();
    }

    for (count = 0; count < (image->path_count); count++)
    {
        error = vg_lite_draw(target, &image->path_data[count], VG_LITE_FILL_EVEN_ODD,
                &matrix, VG_LITE_BLEND_NONE, image->color_data[count]);

        if (error)
        {
            printf("vg_lite_draw() returned error %d\n", error);
            handle_app_error();
        }
    }

    vg_lite_finish();

}


/*******************************************************************************
* Function Name: dc_irq_handler
********************************************************************************
* Summary:
*  Display Controller interrupt handler which gets invoked when the DC finishes
*  utilizing the current frame buffer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void dc_irq_handler(void)
{
    if (pending_buffer >= RESET_VAL)
    {
        active_buffer = pending_buffer;
        pending_buffer = INVALID_VAL;
    }

    /* Clear the DC interrupt */
    Cy_GFXSS_Clear_DC_Interrupt(base, &gfx_context);
}


/*******************************************************************************
* Function Name: disp_i2c_controller_interrupt
********************************************************************************
* Summary:
*  I2C controller ISR which invokes Cy_SCB_I2C_Interrupt to perform I2C transfer
*  as controller.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void disp_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(CYBSP_I2C_CONTROLLER_HW, &disp_i2c_controller_context);
}


/*******************************************************************************
* Function Name: cm55_ns_gfx_task
********************************************************************************
* Summary:
*   This is the FreeRTOS task callback function.
*   It initializes:
*       - GFX subsystem.
*       - Initializes "WF101JTYAHMNB0 panel".
*       - Performs single buffering or double buffering operation based on the
*         serial terminal input.
*
* Parameters:
*  void *arg (unused)
*
* Return:
*  void
*
*******************************************************************************/
void cm55_ns_gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    cy_en_gfx_status_t status = CY_GFX_SUCCESS;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    cy_rslt_t panel_status = CY_RSLT_SUCCESS;
    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;

    /* MIPI-DSI Display specific configs */
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;

    /* Initializes the graphics system according to the configuration */
    status = Cy_GFXSS_Init(base, &GFXSS_config, &gfx_context);

    if (CY_GFX_SUCCESS == status)
    {
        /* Initialize GFXXs DC interrupt */
        sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);
 
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            printf("Error in registering DC interrupt: %d\r\n", sysint_status);
            handle_app_error();
        }

        /* Enable interrupt in NVIC. */
        NVIC_EnableIRQ(GFXSS_DC_IRQ);

                /* Initialize the I2C in controller mode. */
        i2c_result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW,
                    &CYBSP_I2C_CONTROLLER_config, &disp_i2c_controller_context);

        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            printf("I2C controller initialization failed !!\n");
            handle_app_error();
        }

        /* Initialize the I2C interrupt */
        sysint_status = Cy_SysInt_Init(&disp_i2c_controller_irq_cfg,
                                       &disp_i2c_controller_interrupt);

        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            printf("I2C controller interrupt initialization failed\r\n");
            handle_app_error();
        }

        /* Enable the I2C interrupts. */
        NVIC_EnableIRQ(disp_i2c_controller_irq_cfg.intrSrc);

        /* Enable the I2C */
        Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

        /* Initialize the display */
        panel_status = mtb_disp_waveshare_4p3_init(CYBSP_I2C_CONTROLLER_HW,
                                                  &disp_i2c_controller_context);

        if (CY_RSLT_SUCCESS != panel_status)
        {
            printf("Waveshare 4.3-inch R-Pi display init failed with status = %u\r\n", (unsigned int) panel_status);
            handle_app_error();
        }

        /* Allocate memory for VGLite from the vglite_heap_base */
        vg_module_parameters_t vg_params;
        vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
        vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
        vg_params.contiguous_mem_base[VG_PARAMS_POS] = (volatile void *) vglite_heap_base;
        vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;

        /* Initialize VGlite memory. */
        vg_lite_init_mem(&vg_params);

        /* Initialize the vglite context */
        error = vg_lite_init(DISPLAY_W, DISPLAY_H);

        if (VG_LITE_SUCCESS == error)
        {
            /* Allocate the off-screen buffer0 */
            buffer0.width  = DISPLAY_W;
            buffer0.height = DISPLAY_H;
            buffer0.format = VG_LITE_BGR565;

            error = vg_lite_allocate(&buffer0);
            if (error)
            {
                printf("Buffer0 allocation failed in vglite space: vg_lite_allocate() returned error %d\n", error);
                handle_app_error();
            }

            /* Allocate the off-screen buffer1 */
            buffer1.width  = DISPLAY_W;
            buffer1.height = DISPLAY_H;
            buffer1.format = VG_LITE_BGR565;

            error = vg_lite_allocate(&buffer1);
            if (error)
            {
                printf("Buffer1 allocation failed in vglite space: vg_lite_allocate() returned error %d\n", error);
                handle_app_error();
            }

            /* Draw image for fisrt frame */
            draw(Buffers[active_buffer], images[image_select++]);

            /* Display the pending frame buffer */
            Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(Buffers[pending_buffer]->address),
                    &gfx_context);

            printf("Press 'Enter' key to switch between the buffering modes.\n\n\n");

            /* Render image in single or dual buffering mode */
            render_image();
        }
        else
        {
            printf("vg_lite engine init failed: vg_lite_init() returned error %d\n", error);
            handle_app_error();
        }
    }
    else
    {
        printf("Graphics subsystem initialization failed: Cy_GFXSS_Init() returned error %d\n", status);
        handle_app_error();
    }
}

/* [] END OF FILE */