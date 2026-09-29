file(GLOB SRC_FILES mips/*.cpp mips/*.S)

add_library(DynaRec OBJECT
                ${SRC_FILES}
                BranchType.cpp
                DynaRecProfile.cpp
                Fragment.cpp
                FragmentCache.cpp 
                IndirectExitMap.cpp
                StaticAnalysis.cpp
                TraceRecorder.cpp
)

if(DAEDALUS_PROFILE_DEBUG)
target_compile_options(DynaRec PRIVATE -pg)
endif(DAEDALUS_PROFILE_DEBUG)

list(APPEND daed_libs "DynaRec")
set(daed_libs ${daed_libs} PARENT_SCOPE)
