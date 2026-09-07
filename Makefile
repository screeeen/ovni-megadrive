ifeq ($(GDK),)
    $(error GDK environment variable is not set. Install SGDK and export GDK=/path/to/sgdk)
endif

include $(GDK)/makefile.gen
