.macro function name
    .macro endfunc
        .size   ass_\name, . - ass_\name
        .endfunc
        .purgem endfunc
    .endm
        .global ass_\name
        .type   ass_\name, %function
        .func   ass_\name
ass_\name:
.endm
