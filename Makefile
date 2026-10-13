include scripts/build.mk
HOST_CXX ?= c++
.PHONY: all dependencies check-deps overlay backend test package clean
all: overlay backend
dependencies:
	python3 scripts/dependencies.py
check-deps:
	python3 scripts/dependencies.py --check
overlay backend: check-deps
	$(MAKE) -C $@
test:
	@mkdir -p $(BUILD_ROOT)/tests
	$(HOST_CXX) -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Icommon tests/core.cpp -o $(BUILD_ROOT)/tests/core
	$(BUILD_ROOT)/tests/core
	python3 -m unittest discover -s tests -p 'test_*.py' -v
package: all test
	python3 scripts/package.py
clean:
	$(MAKE) -C overlay clean
	$(MAKE) -C backend clean
	rm -rf $(BUILD_ROOT)/tests
