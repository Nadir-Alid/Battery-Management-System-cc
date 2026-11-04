# Application-owned integration. Do not generate over these sources from CubeMX.
set(FREERTOS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source")
target_sources(${PROJECT_NAME} PRIVATE
    "${FREERTOS_SOURCE}/tasks.c"
    "${FREERTOS_SOURCE}/queue.c"
    "${FREERTOS_SOURCE}/list.c"
    "${FREERTOS_SOURCE}/portable/GCC/ARM_CM4F/port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bms_rtos.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32f4xx_hal_timebase_tim.c"
)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${FREERTOS_SOURCE}/include"
    "${FREERTOS_SOURCE}/portable/GCC/ARM_CM4F"
)
