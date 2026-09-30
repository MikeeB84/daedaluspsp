add_library(HLEGraphics OBJECT 
                            BaseRenderer.cpp 
                            CachedTexture.cpp 
                            ConvertImage.cpp 
                            DLDebug.cpp 
                            DLParser.cpp 
                            Microcode.cpp 
                            RDP.cpp 
                            RDPStateManager.cpp 
                            TextureCache.cpp 
                            TextureInfo.cpp 
                            uCodes/Ucode.cpp

            )

if(DAEDALUS_PROFILE_HLEGRAPHICS)
target_compile_options(HLEGraphics PRIVATE -pg)
endif(DAEDALUS_PROFILE_HLEGRAPHICS)

list(APPEND daed_libs "HLEGraphics")
set(daed_libs ${daed_libs} PARENT_SCOPE)
