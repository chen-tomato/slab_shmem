BIN = test
LIB_NAME?= slabshmem
LIB = -L./lib/ -ldl
FLAG := -static
SHARED = -shared
Sharelibslabshmem =	libslabshmem.so

OBJ += shmem.o \
	   shmtx.o \
	   slab.o \
	   buddy.o \
	   slub.o \


VPATH := ./src

INC += -I./ \
	   -I./include \

OBJ_WITH_BUILD_DIR:=$(addprefix build/,$(OBJ))

all: mkbuilddir $(OBJ_WITH_BUILD_DIR)
	$(CC) $(SHARED) -fPIC $(OBJ_WITH_BUILD_DIR) -o $(Sharelibslabshmem)
	cp $(Sharelibslabshmem) /usr/lib
	mv $(Sharelibslabshmem) ./lib/
	$(CC) main.c -W -g -o $(BIN) $(LIB) -l$(LIB_NAME)

build/%.o:%.c
	$(CC) -fPIC -g -O0 -Wall -c $(INC) -o $@ $< $(LIB)

.PHONY:mkbuilddir
mkbuilddir:
	mkdir -p build
install:
	cp config/* /etc

.PHONY:clean
clean:
	rm -rf build $(BIN) ./lib/$(Sharelibslabshmem)


