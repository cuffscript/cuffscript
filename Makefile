CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2

TARGET = cuffc
SOURCES = main.cpp

$(TARGET): $(SOURCES) $(wildcard engine/**/*.h engine/*.h)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

.PHONY: clean
