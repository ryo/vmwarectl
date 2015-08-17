
PROG=	vmwarectl
SRCS=	vmwarectl.c vmport.c


${PROG}: ${SRCS}
	cc -o ${PROG} -O ${SRCS}

clean:
	rm -f ${PROG} ${SRCS:.c=.o}
