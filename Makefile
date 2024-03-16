OBJ=subprocess.o subprocess_tests.o
LDFLAGS=-lgtest -lgtest_main

test: $(OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@
	./test

.PHONY:
clean:
	rm -f $(OBJ) test
