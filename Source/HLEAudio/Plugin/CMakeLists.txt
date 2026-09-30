add_library(Audio_Plugin OBJECT PSP/AudioPluginPSP.cpp)

if(DAEDALUS_PROFILE_AUDIOPLUGIN)
target_compile_options(Audio_Plugin PRIVATE -pg)
endif(DAEDALUS_PROFILE_AUDIOPLUGIN)

list(APPEND daed_libs "Audio_Plugin")
set(daed_libs ${daed_libs} PARENT_SCOPE)
