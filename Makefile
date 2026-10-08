NAME = Matt_daemon

FILES = main.cpp

IFILES = Tintin_reporter.cpp
IHFILES = Tintin_reporter.hpp

DFILES = MattDaemon.cpp Server.cpp
DHFILES = MattDaemon.hpp

IFILES := $(addprefix include/, $(IFILES))
IHFILES := $(addprefix include/, $(IHFILES))

DFILES := $(addprefix daemon/, $(DFILES))
DHFILES := $(addprefix daemon/, $(DHFILES))

CC = c++

FLAGS = -Wall -Wextra -Werror -std=c++98

OBJS = $(FILES:.cpp=.o) $(IFILES:.cpp=.o) $(DFILES:.cpp=.o)
DEPS = $(OBJS:.o=.d)

all: $(NAME)

%.o: %.cpp Makefile
	$(CC) $(FLAGS) -MMD -MP -c $< -o $@

$(NAME): $(OBJS)
	$(CC) $(FLAGS) $(OBJS) -o $(NAME)

clean:
	@echo "Cleaning object files"
	@rm -f $(OBJS) $(DEPS)

fclean: clean
	@echo "Cleaning executable file"
	@rm -f $(NAME)

re: fclean
	$(MAKE) all

-include $(DEPS)

.PHONY: all clean fclean re
