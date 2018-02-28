#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

#UGFX_STANDALONE := 1

ifdef CONFIG_UGFX_GUI_ENABLE
	ifdef UGFX_STANDALONE
        COMPONENT_SRCDIRS += ./adapter
    	COMPONENT_ADD_INCLUDEDIRS += .  ./adapter
        ifdef CONFIG_UGFX_LCD_DRIVER_API_MODE
        COMPONENT_SRCDIRS += ./lcd_raw
        COMPONENT_ADD_INCLUDEDIRS += ./lcd_raw
        endif
        
        ifdef CONFIG_UGFX_LCD_DRIVER_FRAMEBUFFER_MODE
        COMPONENT_SRCDIRS += ./framebuffer 
        COMPONENT_ADD_INCLUDEDIRS += ./framebuffer
        endif
        
        ifdef CONFIG_UGFX_DRIVER_TOUCH_SCREEN_ENABLE
        COMPONENT_SRCDIRS += ./touch_screen
        COMPONENT_ADD_INCLUDEDIRS += ./touch_screen
        endif
	else
        ifdef CONFIG_UGFX_USE_CUSTOM_DRIVER
            COMPONENT_SRCDIRS := 
            COMPONENT_ADD_INCLUDEDIRS := 
        else
            COMPONENT_SRCDIRS += ./adapter
        	COMPONENT_ADD_INCLUDEDIRS += .  ./adapter
            ifdef CONFIG_UGFX_LCD_DRIVER_API_MODE
            COMPONENT_SRCDIRS += ./lcd_raw
            COMPONENT_ADD_INCLUDEDIRS += ./lcd_raw
            endif
            
            ifdef CONFIG_UGFX_LCD_DRIVER_FRAMEBUFFER_MODE
            COMPONENT_SRCDIRS += ./framebuffer 
            COMPONENT_ADD_INCLUDEDIRS += ./framebuffer
            endif
            
            ifdef CONFIG_UGFX_DRIVER_TOUCH_SCREEN_ENABLE
            COMPONENT_SRCDIRS += ./touch_screen
            COMPONENT_ADD_INCLUDEDIRS += ./touch_screen
            endif
        endif
    endif

else
COMPONENT_SRCDIRS := 
COMPONENT_ADD_INCLUDEDIRS := 
endif 
