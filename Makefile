all:
	$(MAKE) -C src
check: all
	python3 tests/test_lab4.py
report:
	$(MAKE) -C report
clean:
	$(MAKE) -C src clean
.PHONY: all check report clean
