SHELL:=/bin/bash
CXX:=g++
SRC_DIR:=.
OBJ_DIR:=bin
CXXFLAGS:=-std=c++17 -g -pthread -O3
# CXXFLAGS:=-std=c++17 -g -pthread 
LIBS:=-I.
HEADERS:=
SRCS:=$(shell find $(SRC_DIR) -name "*.cc")
OBJS:=$(shell sed -rn 's/\s+/\n/gp' <<< "${SRCS}" | sed -rn "s/(.*)\/(.*)\.(.*)\s*/\1\/${OBJ_DIR}\/\2.o/p")
SUB_DIRS:=$(shell find . -type d -not -name "${OBJ_DIR}")

$(shell sed -r 's/\s+/\n/gp' <<< "${SUB_DIRS}" | xargs -I{} mkdir -p {}/${OBJ_DIR})

all: main

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(HEADERS) $(LIBS) $(OBJS) -o main

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc
	${CXX} ${CXXFLAGS} $(LIBS) -c $< -o $@

ssd_emu/$(OBJ_DIR)/%.o: ssd_emu/$(SRC_DIR)/%.cc
	${CXX} ${CXXFLAGS} $(LIBS) -c $< -o $@

.phony: clean

clean:
	sed -r 's/\s+/\n/gp' <<< "${SUB_DIRS}" | xargs -I@ find @/${OBJ_DIR}/ -type f | xargs rm -f
	rm -f main

