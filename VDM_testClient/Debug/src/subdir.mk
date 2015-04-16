################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/client.c \
../src/clientFunctions.c \
../src/crc.c \
../src/protocol.c 

OBJS += \
./src/client.o \
./src/clientFunctions.o \
./src/crc.o \
./src/protocol.o 

C_DEPS += \
./src/client.d \
./src/clientFunctions.d \
./src/crc.d \
./src/protocol.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


