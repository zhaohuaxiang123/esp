#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

# componet standalone mode
ifndef CONFIG_IOT_SOLUTION_EMBED   

COMPONENT_ADD_INCLUDEDIRS := ./ir_learn/include
COMPONENT_SRCDIRS := ./ir_learn

else

ifdef CONFIG_IOT_IR_LEARN_ENABLE
COMPONENT_ADD_INCLUDEDIRS := ./ir_learn/include
COMPONENT_SRCDIRS := ./ir_learn
else
# Disable component
COMPONENT_ADD_INCLUDEDIRS :=
COMPONENT_ADD_LDFLAGS :=
COMPONENT_SRCDIRS :=
endif

endif