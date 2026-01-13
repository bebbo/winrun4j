# ------------------------------------------------------------
# Common compiler flags (all targets, all architectures)
# ------------------------------------------------------------

# No exceptions, no RTTI, no GS, no SEH
add_compile_options(
    /GR-
    /GS-
)

# Warning level 4
add_compile_options(/W4)

# Compile as C++
add_compile_options(/TP)

# Optimize for size
add_compile_options(/O1 /Os /GL)

# Link-time optimizations
add_link_options(/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE)
