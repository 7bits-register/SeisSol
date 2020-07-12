# Library settings:
option(HDF5 "Use HDF5 library for data output" ON)
option(NETCDF "Use netcdf library for mesh input" ON)
option(METIS "Use metis for partitioning" ON)
option(MPI "Use MPI parallelization" ON)
option(OPENMP "Use OpenMP parallelization" ON)
option(ASAGI "Use asagi for material input" OFF)

# todo:
option(SIONLIB "Use sionlib for checkpointing" OFF)
option(MEMKIND "Use memkind library for hbw memory support" OFF)

#Seissol specific
set(ORDER 6 CACHE STRING "Convergence order")  # must be INT type, by cmake-3.16 accepts only STRING
set(ORDER_OPTIONS 2 3 4 5 6 7 8)
set_property(CACHE ORDER PROPERTY STRINGS ${ORDER_OPTIONS})

set(NUMBER_OF_MECHANISMS 0 CACHE STRING "Number of mechanisms")

set(EQUATIONS "elastic" CACHE STRING "Equation set used")
set(EQUATIONS_OPTIONS elastic viscoelastic viscoelastic2)
set_property(CACHE EQUATIONS PROPERTY STRINGS ${EQUATIONS_OPTIONS})

set(HOST_ARCH "hsw" CACHE STRING "Type of the target host architecture")
set(HOST_ARCH_OPTIONS noarch wsm snb hsw knc knl skx power9)
set(HOST_ARCH_ALIGNMENT   16  16  32  32  64  64  64     16)  # size of a vector registers in bytes for a given architecture
set_property(CACHE HOST_ARCH PROPERTY STRINGS ${HOST_ARCH_OPTIONS})

set(COMPUTE_ARCH "same" CACHE STRING "Type of the target compute architecture")
set(COMPUTE_ARCH_OPTIONS    same nvidia amd_gpu)
set(COMPUTE_ARCH_ALIGNMENT  same     64     128)
set_property(CACHE COMPUTE_ARCH PROPERTY STRINGS ${COMPUTE_ARCH_OPTIONS})

set(COMPUTE_SUB_ARCH "same" CACHE STRING "Type of the target GPU architecture")
set(COMPUTE_SUB_ARCH_OPTIONS same sm_60 sm_61 sm_62 sm_70 sm_71 sm_75)
set_property(CACHE COMPUTE_SUB_ARCH PROPERTY STRINGS ${COMPUTE_SUB_ARCH_OPTIONS})

set(PRECISION "double" CACHE STRING "type of floating point precision, namely: double/float")
set(PRECISION_OPTIONS double float)
set_property(CACHE PRECISION PROPERTY STRINGS ${PRECISION_OPTIONS})

set(DYNAMIC_RUPTURE_METHOD "quadrature" CACHE STRING "Dynamic rupture method")
set(RUPTURE_OPTIONS quadrature cellaverage)
set_property(CACHE DYNAMIC_RUPTURE_METHOD PROPERTY STRINGS ${RUPTURE_OPTIONS})

option(PLASTICITY "Use plasticity")
set(PLASTICITY_METHOD "nb" CACHE STRING "Dynamic rupture method")
set(PLASTICITY_OPTIONS nb ip)
set_property(CACHE PLASTICITY_METHOD PROPERTY STRINGS ${PLASTICITY_OPTIONS})

set(NUMBER_OF_FUSED_SIMULATIONS 1 CACHE STRING "A number of fused simulations")
set(MEMORY_LAYOUT "auto" CACHE FILEPATH "A file with a specific memory layout or auto")

option(COMMTHREAD "Use a communication thread for MPI+MP." OFF)

set(LOG_LEVEL "info" CACHE STRING "Log level for the code")
set(LOG_LEVEL_OPTIONS "debug" "info" "warning" "error")
set_property(CACHE LOG_LEVEL PROPERTY STRINGS ${LOG_LEVEL_OPTIONS})

set(LOG_LEVEL_MASTER "info" CACHE STRING "Log level for the code")
set(LOG_LEVEL_MASTER_OPTIONS "debug" "info" "warning" "error")
set_property(CACHE LOG_LEVEL_MASTER PROPERTY STRINGS ${LOG_LEVEL_MASTER_OPTIONS})

set(GEMM_TOOLS_LIST "LIBXSMM,PSpaMM" CACHE STRING "choose a gemm tool(s) for the code generator")
set(GEMM_TOOLS_OPTIONS "LIBXSMM,PSpaMM" "LIBXSMM" "MKL" "OpenBLAS" "BLIS" "Eigen,GemmForge" "LIBXSMM,PSpaMM,GemmForge")
set_property(CACHE GEMM_TOOLS_LIST PROPERTY STRINGS ${GEMM_TOOLS_OPTIONS})


option(ANALYZE_SRC_CODE "use clang-tidy to analyze the source code" OFF)

#-------------------------------------------------------------------------------
# ------------------------------- ERROR CHECKING -------------------------------
#-------------------------------------------------------------------------------
function(check_parameter parameter_name value options)

    list(FIND options ${value} INDEX)

    set(WRONG_PARAMETER -1)
    if (${INDEX} EQUAL ${WRONG_PARAMETER})
        message(FATAL_ERROR "${parameter_name} is wrong. Specified \"${value}\". Allowed: ${options}")
    endif()

endfunction()

check_parameter("ORDER" ${ORDER} "${ORDER_OPTIONS}")
check_parameter("HOST_ARCH" ${HOST_ARCH} "${HOST_ARCH_OPTIONS}")
check_parameter("COMPUTE_ARCH" ${COMPUTE_ARCH} "${COMPUTE_ARCH_OPTIONS}")
check_parameter("COMPUTE_SUB_ARCH" ${COMPUTE_SUB_ARCH} "${COMPUTE_SUB_ARCH_OPTIONS}")
check_parameter("EQUATIONS" ${EQUATIONS} "${EQUATIONS_OPTIONS}")
check_parameter("PRECISION" ${PRECISION} "${PRECISION_OPTIONS}")
check_parameter("DYNAMIC_RUPTURE_METHOD" ${DYNAMIC_RUPTURE_METHOD} "${RUPTURE_OPTIONS}")
check_parameter("PLASTICITY_METHOD" ${PLASTICITY_METHOD} "${PLASTICITY_OPTIONS}")
check_parameter("LOG_LEVEL" ${LOG_LEVEL} "${LOG_LEVEL_OPTIONS}")
check_parameter("LOG_LEVEL_MASTER" ${LOG_LEVEL_MASTER} "${LOG_LEVEL_MASTER_OPTIONS}")

# check compute sub architecture (relevant only for GPU)
if (${COMPUTE_ARCH} STREQUAL "same")
    if (NOT ${COMPUTE_SUB_ARCH} STREQUAL "same")
        message(FATAL_ERROR "COMPUTE_SUB_ARCH must be the same as HOST_ARCH and COMPUTE_ARCH. Please, choose \"same\"")
    endif()
    set(COMPUTE_ARCH ${HOST_ARCH})
    list(FIND HOST_ARCH_OPTIONS ${COMPUTE_ARCH} INDEX)
    list(GET HOST_ARCH_ALIGNMENT ${INDEX} ALIGNMENT)
    set(DEVICE_BACKEND "NONE")
else()
    if (${COMPUTE_ARCH} STREQUAL "nvidia")
        list(FIND COMPUTE_ARCH_OPTIONS ${COMPUTE_ARCH} INDEX)
        list(GET COMPUTE_ARCH_ALIGNMENT ${INDEX} ALIGNMENT)
        set(DEVICE_BACKEND "CUDA")
    elseif(${COMPUTE_ARCH} STREQUAL "amd_gpu")
        list(FIND COMPUTE_ARCH_OPTIONS ${COMPUTE_ARCH} INDEX)
        list(GET COMPUTE_ARCH_ALIGNMENT ${INDEX} ALIGNMENT)
        set(DEVICE_BACKEND "HIP")
        # amd_gpu will be supported in some near future
        message(FATAL_ERROR "amd_gpu currently is not supported")
    else()
        message(FATAL_ERROR "Unknown compute arch. provided: ${COMPUTE_ARCH}. nvidia and amd_gpu are currently supported")
    endif()
    if (${COMPUTE_SUB_ARCH} STREQUAL "same")
        message(FATAL_ERROR "Please, provide a specific COMPUTE_SUB_ARCH. Given: \"same\"")
    endif()
endif()

# check NUMBER_OF_MECHANISMS
if ("${EQUATIONS}" STREQUAL "elastic" AND ${NUMBER_OF_MECHANISMS} GREATER 0)
    message(FATAL_ERROR "${EQUATIONS} does not support a NUMBER_OF_MECHANISMS > 0.")
endif()

if ("${EQUATIONS}" MATCHES "viscoelastic.?" AND ${NUMBER_OF_MECHANISMS} LESS 1)
    message(FATAL_ERROR "${EQUATIONS} needs a NUMBER_OF_MECHANISMS > 0.")
endif()

# derive a byte representation of real numbers
if ("${PRECISION}" STREQUAL "double")
    set(REAL_SIZE_IN_BYTES 8)
elseif ("${PRECISION}" STREQUAL "float")
    set(REAL_SIZE_IN_BYTES 4)
endif()

# check NUMBER_OF_FUSED_SIMULATIONS
math(EXPR IS_ALIGNED_MULT_SIMULATIONS 
          "${NUMBER_OF_FUSED_SIMULATIONS} % (${ALIGNMENT} / ${REAL_SIZE_IN_BYTES})")

if (NOT ${NUMBER_OF_FUSED_SIMULATIONS} EQUAL 1 AND NOT ${IS_ALIGNED_MULT_SIMULATIONS} EQUAL 0)
    math(EXPR FACTOR "${ALIGNMENT} / ${REAL_SIZE_IN_BYTES}")
    message(FATAL_ERROR "a number of fused must be multiple of ${FACTOR}")
endif()

#-------------------------------------------------------------------------------
# -------------------- COMPUTE/ADJUST ADDITIONAL PARAMETERS --------------------
#-------------------------------------------------------------------------------
# PDE-Settings
MATH(EXPR NUMBER_OF_QUANTITIES "9 + 6 * ${NUMBER_OF_MECHANISMS}" )

# generate an internal representation of an architecture type which is used in seissol
string(SUBSTRING ${PRECISION} 0 1 PRECISION_PREFIX)
if (${PRECISION} STREQUAL "double")
    set(COMPUTE_ARCH_STR "d${COMPUTE_ARCH}")
    set(HOST_ARCH_STR "d${HOST_ARCH}")
elseif(${PRECISION} STREQUAL "float")
    set(COMPUTE_ARCH_STR "s${COMPUTE_ARCH}")
    set(HOST_ARCH_STR "s${HOST_ARCH}")
endif()

function(cast_log_level_to_int log_level_str log_level_int)
  if (${log_level_str} STREQUAL "debug")
    set(${log_level_int} 3 PARENT_SCOPE)
  elseif (${log_level_str} STREQUAL "info")
    set(${log_level_int} 2 PARENT_SCOPE)
  elseif (${log_level_str} STREQUAL "warning")
    set(${log_level_int} 1 PARENT_SCOPE)
  elseif (${log_level_str} STREQUAL "error")
    set(${log_level_int} 0 PARENT_SCOPE)
  endif()
endfunction()

cast_log_level_to_int(LOG_LEVEL LOG_LEVEL)
cast_log_level_to_int(LOG_LEVEL_MASTER LOG_LEVEL_MASTER)
