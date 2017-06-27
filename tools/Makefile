TARGETS = eppub epsub

CC=gcc -g -O3 -Wall -Werror -fgnu89-inline -fPIC

UTIL_FLD = ./util
UTIL_INC = -I${UTIL_FLD}/include
UTIL_OBJ = ${UTIL_FLD}/cpu.o

MOS_INC = -I.
MOS_LIB = libmosquitto.so.1

INC = ${MOS_INC} ${UTIL_INC}
LIBS = -lpthread

all: util.o ${TARGETS}

util.o:
	${CC} -c ${UTIL_INC} ${UTIL_FLD}/cpu.c
	mv *.o ${UTIL_FLD}

eppub.o: eppub.c
	${CC} -c $< ${INC} ${MOS_INC}

eppub: eppub.o
	${CC} $< ${LIBS} ${UTIL_OBJ} ${MOS_LIB} -o $@

epsub.o: epsub.c
	${CC} -c $< ${INC} ${MOS_INC}

epsub: epsub.o
	${CC} $< ${LIBS} ${UTIL_OBJ} ${MOS_LIB} -o $@
	
clean:
	rm -f *~ *.o ${TARGETS} ${UTIL_FLD}/*.o log_*
