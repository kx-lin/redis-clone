# Variables
CXX = g++
CXXFLAGS = -Wall -Wextra -O2
LDFLAGS = 

# Target names
SERVER = server
CLIENT = client

# Source files
UTILS_SRC = utils.cpp
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# Object files (automatically generated from .cpp names)
UTILS_OBJ = $(UTILS_SRC:.cpp=.o)
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

# Default rule: build both
all: $(SERVER) $(CLIENT)

# Link server
$(SERVER): $(SERVER_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Link client
$(CLIENT): $(CLIENT_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Pattern rule for object files
# This says: to create a .o file, look for the corresponding .cpp file
%.o: %.cpp utils.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(SERVER) $(CLIENT) *.o

# Phony targets (not actual files)
.PHONY: all clean
