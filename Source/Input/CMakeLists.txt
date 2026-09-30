add_library(Input OBJECT PSP/InputManagerPSP.cpp)

if(DAEDALUS_PROFILE_INPUT)
target_compile_options(Input PRIVATE -pg)
endif(DAEDALUS_PROFILE_INPUT)

list(APPEND daed_libs "Input")
set(daed_libs ${daed_libs} PARENT_SCOPE)
