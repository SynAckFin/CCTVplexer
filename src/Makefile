#  
#   Copyright (C) 2020  Terry Sanders
# 
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
# 
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
# 
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.
# 

OBJS = main.o render.o config.o monitor.o rtsp.o
INCS = cctvplexer.h render.h monitor.h
TARGET = cctvplexer cecremote

# Not sure all these defines are needed.
CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS
CFLAGS+=-DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE
CFLAGS+=-D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2
CFLAGS+=-DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX
CFLAGS+=-DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM

LDFLAGS+= -lconfig -lcurl
LDFLAGS+=-L/opt/vc/lib/ -lopenmaxil -lbcm_host -llirc_client
INCLUDES+=-I/opt/vc/include/

all: $(TARGET)

$(OBJS): $(INCS)

%.o: %.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@

cctvplexer: $(OBJS)
	$(CC) -o $@  $(OBJS) $(LDFLAGS)

cecremote: cecremote.o
	$(CC) -o $@  $< -llirc_client -lcec -ldl

clean:
	@rm -f $(TARGET) *.o



