SHELL = /bin/bash
DIRS := $(shell find . -maxdepth 1 -mindepth 1 -type d | sed 's|^./||' | sort)

all: $(addprefix all-,$(DIRS))
run: $(addprefix run-,$(DIRS))
test: $(addprefix test-,$(DIRS))
clean: $(addprefix clean-,$(DIRS))

all-%:
	$(MAKE) -C $* all

run-%:
	$(MAKE) -C $* run

test-%:
	$(MAKE) -C $* test
	
clean-%:
	$(MAKE) -C $* clean
	
test-flow:
	$(MAKE) test ARGS="1 1" PRF_ARGS="1 1" $(shell p=`grep '^processor\>' /proc/cpuinfo | wc -l`; if [ $$p -gt 1 ]; then echo -j$$p; fi)

