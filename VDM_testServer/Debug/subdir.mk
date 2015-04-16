################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../commonFunctions.c \
../crc.c \
../protocol.c \
../servFunctions.c \
../server.c 

OBJS += \
./commonFunctions.o \
./crc.o \
./protocol.o \
./servFunctions.o \
./server.o 

C_DEPS += \
./commonFunctions.d \
./crc.d \
./protocol.d \
./servFunctions.d \
./server.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


