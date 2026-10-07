NAME := Matt_daemon
CXX := c++
CPPFLAGS := -Iinc
CXXFLAGS := -std=c++11 -Wall -Wextra -Werror
SRC := src/main.cpp src/MattDaemon.cpp src/Tintin_reporter.cpp
OBJ := $(SRC:src/%.cpp=build/%.o)
DEP := $(OBJ:.o=.d)

.PHONY: all clean fclean re

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf build

fclean: clean
	rm -f $(NAME)

re: fclean
	$(MAKE) all

-include $(DEP)
