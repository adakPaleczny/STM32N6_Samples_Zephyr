.. _hello_world_stm32n6570_dk:

Hello World
###########

Overview
********

A simple Hello World application that prints a greeting to the console.

Building and Running
********************

This sample can be built and run for the STM32N6570-DK board using sysbuild
(required for STM32N6 boards that use First Stage Boot Loader):

.. code-block:: console

   west build -b stm32n6570_dk --sysbuild samples/hello_world
   west flash

Sample Output
*************

.. code-block:: console

   Hello World! stm32n6570_dk
