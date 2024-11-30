cmake_minimum_required(VERSION 3.12)
project(ULC C)

set(CMAKE_C_STANDARD 11)               # 设置 C 标准为 C11
set(CMAKE_C_STANDARD_REQUIRED ON)       # 强制使用指定的 C 标准
# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall")

# 包含目录
include_directories(
    include
    libfourier/include
    tools
)

# 源文件目录
set(COMMON_SRCDIR libulc)
set(TOOLS_SRCDIR tools)
set(ENCODETOOL_SRCDIR encode)
set(DECODETOOL_SRCDIR decode)
set(FOURIER_SECDIR libfourier)

# 源文件收集
file(GLOB COMMON_SRC 
    "${COMMON_SRCDIR}/*.c"
)

file(GLOB FOURIER_SECDIR 
    "${FOURIER_SECDIR}/source/*.c"
    "${FOURIER_SECDIR}/source/x86/*.c"
)

file(GLOB TOOLS_SRCDIR 
    "${TOOLS_SRCDIR}/*.c"
)

file(GLOB ENCODETOOL_SRC 
    "${ENCODETOOL_SRCDIR}/*.c"
)

file(GLOB DECODETOOL_SRC 
    "${DECODETOOL_SRCDIR}/*.c"
)



# 可执行文件
add_executable(ulcencodetool 
    ${COMMON_SRC} 
    ${TOOLS_SRCDIR} 
    ${ENCODETOOL_SRC}
    ${FOURIER_SECDIR}
)

add_executable(ulcdecodetool 
    ${COMMON_SRC} 
    ${TOOLS_SRCDIR} 
    ${DECODETOOL_SRC}
    ${FOURIER_SECDIR}
)


