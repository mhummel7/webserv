CXX     := g++
CXXFLAGS := -std=c++17 -Wall -Werror -Wextra -O2 -Iinclude

DBGFLAGS := -g -O0 -DDEBUG

NAME := webserv

SRC_DIR := src
OBJ_DIR := obj

SRCS := \
	src/CGIHandler.cpp \
	src/config.cpp \
	src/HTTPHandler.cpp \
	src/main.cpp \
	src/Response.cpp \
	src/Server.cpp

OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

DEPFLAGS := -MMD -MP
CXXFLAGS += $(DEPFLAGS)

.PHONY: all debug clean fclean re run

all: $(NAME)

debug: CXXFLAGS += $(DBGFLAGS)
debug: $(NAME)

$(NAME): $(OBJS)
	@$(CXX) $(CXXFLAGS) $(SANFLAGS) $^ -o $@
	@echo "Linked -> $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@
	@echo "Compiled $<"

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

run: $(NAME)
	@./$(NAME)

clean:
	@rm -rf $(OBJ_DIR)

fclean: clean
	@rm -f $(NAME)

re: fclean all

-include $(OBJS:.o=.d)
