CXX = g++
CXXFLAGS = -Wall -pthread -std=c++11
SRC_DIR = src
INC_DIR = include
COMMON_DIR = ../common/src

OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/tracker.o $(SRC_DIR)/sync.o  $(SRC_DIR)/SystemState.o \
       $(COMMON_DIR)/sha1.o

TARGET = tracker

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) -lcrypto

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp $(INC_DIR)/%.h
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I../common/include -c $< -o $@

$(COMMON_DIR)/%.o: $(COMMON_DIR)/%.cpp ../common/include/%.h
	$(CXX) $(CXXFLAGS) -I../common/include -c $< -o $@

run: $(TARGET)
	./$(TARGET) tracker_info.txt 1

clean:
	rm -f $(SRC_DIR)/*.o $(COMMON_DIR)/*.o $(TARGET)
