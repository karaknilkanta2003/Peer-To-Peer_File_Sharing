CXX = g++
CXXFLAGS = -Wall -pthread -std=c++11 -D_FILE_OFFSET_BITS=64
SRC_DIR = src
INC_DIR = include
COMMON_DIR = ../common/src

OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/client.o $(SRC_DIR)/commands.o \
       $(SRC_DIR)/downloader.o $(SRC_DIR)/uploader.o $(SRC_DIR)/uploader_server.o \
       $(SRC_DIR)/seeds.o \
       $(COMMON_DIR)/sha1.o

TARGET = client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) -lcrypto

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I../common/include -c $< -o $@

$(COMMON_DIR)/%.o: $(COMMON_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I../common/include -c $< -o $@

run: $(TARGET)
	./$(TARGET) 127.0.0.1:5000 ../tracker/tracker_info.txt

clean:
	rm -f $(SRC_DIR)/*.o $(COMMON_DIR)/*.o $(TARGET)
	rm -f ~/.p2p_seeds_*.txt