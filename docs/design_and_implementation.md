[Click here](../README.md) to view the README.

## Design and implementation

This project supports three displays :
   **[Waveshare 4.3-inch Raspberry Pi DSI LCD display](https://www.waveshare.com/4.3inch-DSI-LCD.htm):** The LCD houses a Chipone ICN6211 display controller and uses the MIPI DSI interface. This display is supported by default
   **[ST7701S 4-inch MIPI DSI display driver](https://www.rocktech.com.hk/lcd-product/rk040hf001):** The TFT LCD houses a [ST7701S](https://datasheet4u.com/pdf-down/S/T/7/ST7701-Sitronix.pdf) display controller and uses the MIPI DSI interface. This display by default shipped with the KIT_PSE84_HMI

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and puts itself (CM33) into DeepSleep mode. The main firmware executes on the CM55 core.

The CM55 application drives the LCD and renders the image using PSOC&trade; Edge graphics subsystem. The graphics subsystem of PSOC&trade; Edge MCU houses an independent 2.5D graphics processing unit (GPU), a display controller (DC), and a MIPI DSI host controller with MIPI D-PHY physical layer interface.

The GPU supports vector graphics (draw circles, rectangles, quadratic curves) and fonts.

VGLite driver APIs are used to carry out operations, such as rotate/scale, color fill, and color conversion on the vector path data. After the GPU renders the frames, they are then transferred to the MIPI DSI host controller via the DC and displayed on the LCD.

Following are the important parts in this code example:

- **proj_cm55/infineon_logo_paths.h:** This file contains the vectored datapath for the Infineon logo image and is consumed by the `vg_lite_draw` API to draw the corresponding image

- **proj_cm55/tiger_paths.h:** This file contains the vectored datapath for tiger image and is consumed by the `vg_lite_draw` API to draw the corresponding image

- **Application code:** Here, the CM55 CPU utilizes the graphics subsystem and VGLite APIs to render the target image. The `main()` function in the *proj_cm55* > *main.c* file first initializes the BSP, followed by retarget-io to use the debug UART port and then creates the graphics application FreeRTOS task `cm55_ns_gfx_task`

   The `cm55_ns_gfx_task` task in the *proj_cm55* > *gfx_task.c* file performs the initialization of the graphics subsystem and the LCD display driver, it then calls the `render_image` function in which the drawing and displaying of the image takes place in the following way:

   The `render_image` function utilizes a single buffer or dual buffers to draw and display the image frames based on the buffering mode selected by you

   - **In single buffering mode:** First, one of the image frames will be drawn into the buffer and then this buffer will be displayed. Next, the second image frame will be drawn into the same buffer and will be displayed and this sequence repeats

   - **In double buffering mode:** Two buffers are used. One is used for drawing while the other is being displayed. One of the two images is drawn into the draw-buffer while the display-buffer gets displayed with the other image already drawn into it. Now, the buffers are swapped and the one that was drawn into previously gets displayed and the other buffer gets drawn with another image. The drawing and displaying of images takes place in `render_image` function and the buffer swapping takes place in the display controller interrupt service routine (ISR)

   You can select the buffering mode by pressing the 'Enter' key and the terminal displays the current buffer mode.