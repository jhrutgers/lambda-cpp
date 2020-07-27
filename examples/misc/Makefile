SHELL = /bin/bash
LAMBDA_ROOT = ../..

ifneq ($(BIN),)
include $(LAMBDA_ROOT)/Makefile
else

EXAMPLES := $(patsubst %.cc,%,$(wildcard *.cc))
.PHONY: $(EXAMPLES)

all: $(addprefix all-,$(EXAMPLES))
run: $(addprefix run-,$(EXAMPLES))
test: $(addprefix test-,$(EXAMPLES))
clean: $(addprefix clean-,$(EXAMPLES))

all-%:
	$(MAKE) BIN=$* all

run-%:
	$(MAKE) BIN=$* run

test-%:
	$(MAKE) BIN=$* test

clean-%:
	$(MAKE) BIN=$* clean

.SECONDEXPANSION:
$(EXAMPLES): run-$$@

endif
