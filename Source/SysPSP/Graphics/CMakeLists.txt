add_library(Graphics_PSP OBJECT 

                                 GraphicsContextPSP.cpp 
                                NativeTexturePSP.cpp 
                                VideoMemoryManager.cpp
                                UIContextPSP.cpp
                                DrawTextPSP.cpp
            )

if(DAEDALUS_PROFILE_GRAPHICS_PSP)
    target_compile_options(Graphics_PSP PRIVATE -pg)
endif(DAEDALUS_PROFILE_GRAPHICS_PSP)
            
list(APPEND daed_libs "Graphics_PSP")
set(daed_libs ${daed_libs} PARENT_SCOPE)
