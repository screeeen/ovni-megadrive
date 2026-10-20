ifeq ($(GDK),)
    $(error GDK environment variable is not set. Install SGDK and export GDK=/path/to/sgdk)
endif

include $(GDK)/makefile.gen

# Comprueba que detras de la BSS queda heap para lo que SGDK pide al arrancar.
# Si no, el ROM compila y enlaza pero se muere en el primer frame.
.PHONY: check
check:
	@python3 tools/heap_check.py
