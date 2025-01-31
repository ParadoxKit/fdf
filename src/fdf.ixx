
module;
export module fdf;
import std;
import std.compat;

extern "C++"
{
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
    #elif defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable : 5244) // Including header in the purview of module 'fastgltf' appears erroneous.
    #endif
    


    #define FDF_EXPORT export
    #include "fdf.h"
    
    

    #if defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(_MSC_VER)
        #pragma warning(pop)
    #endif
}
