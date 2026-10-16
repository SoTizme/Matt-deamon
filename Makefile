NAME = Matt_daemon

FILES = main.cpp

IFILES = Tintin_reporter.cpp
IHFILES = Tintin_reporter.hpp

DFILES = MattDaemon.cpp Server.cpp
DHFILES = MattDaemon.hpp Server.hpp

IFILES := $(addprefix include/, $(IFILES))
IHFILES := $(addprefix include/, $(IHFILES))

DFILES := $(addprefix daemon/, $(DFILES))
DHFILES := $(addprefix daemon/, $(DHFILES))

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98

OBJS = $(FILES:.cpp=.o) $(IFILES:.cpp=.o) $(DFILES:.cpp=.o)

all: $(NAME)

%.o: %.cpp $(IHFILES) $(DHFILES) Makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

clean:
	@echo "Cleaning object files"
	@rm -f $(OBJS)

fclean: clean
	@echo "Cleaning executable file"
	@rm -f $(NAME)

re: fclean
	$(MAKE) all

.PHONY: all clean fclean re
