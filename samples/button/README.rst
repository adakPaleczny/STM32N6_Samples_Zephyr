.. _button_stm32n6570_dk:

Button
######

Overview
********

A simple button demo that shows the use of GPIO input and interrupt with the
:ref:`GPIO API<gpio_api>`.

The sample prints a message to the console whenever the user button is pressed.
Additionally, the green LED (LD1) is turned on while the button is held down.

On the STM32N6570-DK board:

- The user button is connected to GPIOC pin 13 (aliased as ``sw0``)
- The green LED (LD1) is connected to GPIOO pin 1 (aliased as ``led0``)

Building and Running
********************

This sample can be built and run for the STM32N6570-DK board using sysbuild
(required for STM32N6 boards that use First Stage Boot Loader):

.. code-block:: console

   west build -b stm32n6570_dk --sysbuild samples/button
   west flash

Sample Output
*************

When the button is pressed, the following message is printed to the console:

.. code-block:: console

   Set up button at GPIOC pin 13
   Set up LED at GPIOO pin 1
   Press the button
   Button pressed at 12345678
   Button pressed at 23456789
