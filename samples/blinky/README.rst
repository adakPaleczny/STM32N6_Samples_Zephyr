.. _blinky_stm32n6570_dk:

Blinky
######

Overview
********

The Blinky sample blinks an LED forever using the :ref:`GPIO API<gpio_api>`.

The source code shows how to:

#. Get a pin specification from the :ref:`devicetree <dt-guide>` as a
   :c:struct:`gpio_dt_spec`
#. Configure the GPIO pin as an output
#. Toggle the pin in a loop

On the STM32N6570-DK board, the green LED (LD1) connected to GPIOO pin 1 is
used. It is aliased as ``led0`` in the board's device tree.

Building and Running
********************

This sample can be built and run for the STM32N6570-DK board using sysbuild
(required for STM32N6 boards that use First Stage Boot Loader):

.. code-block:: console

   west build -b stm32n6570_dk --sysbuild samples/blinky
   west flash

After flashing, the green LED (LD1) on the STM32N6570-DK board will start
blinking with a 1 second period.
