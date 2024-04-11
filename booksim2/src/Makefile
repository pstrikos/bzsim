# $Id $

# Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

CXX = g++
LEX = flex
YACC   = bison -y
DEFINE = 
INCPATH = -I. -Iarbiters -Iallocators -Irouters -Inetworks -Ipower
CPPFLAGS_COMMON = -Wall $(INCPATH) $(DEFINE) -D_GLIBCXX_USE_CXX11_ABI=0 #comply with ABI needed for ZSIM 
CPPFLAGS = $(CPPFLAGS_COMMON) -O3
CPPFLAGS_D = $(CPPFLAGS_COMMON) -O0 -g -DTRACK_CREDITS=1 -DTRACK_FLOWS=1 
CPPFLAGS_P = $(CPPFLAGS_COMMON) -O0 -g -fprofile-generate --coverage -pg
LDFLAGS =
LDFLAGS_D =
LDFLAGS_P = -lgcov -fprofile-generate --coverage -pg
 
ifeq ($(EXTRA_BYPASS_STATS), 1)
CPPFLAGS += -DEXTRA_BYPASS_STATS=1
CPPFLAGS_P += -DEXTRA_BYPASS_STATS=1
CPPFLAGS_D += -DEXTRA_BYPASS_STATS=1
endif

ifeq ($(BYPASS_STATS), 1)
CPPFLAGS += -DBYPASS_STATS=1
CPPFLAGS_P += -DBYPASS_STATS=1
CPPFLAGS_D += -DBYPASS_STATS=1
endif

ifeq ($(EMPTY_STEP), 1)
CPPFLAGS += -D_EMPTY_STEP_=1
CPPFLAGS_P += -D_EMPTY_STEP_=1
CPPFLAGS_D += -D_EMPTY_STEP_=1
endif

ifeq ($(SKIP_STEP), 1)
CPPFLAGS += -D_SKIP_STEP_=1
CPPFLAGS_P += -D_SKIP_STEP_=1
CPPFLAGS_D += -D_SKIP_STEP_=1
endif

ifeq ($(NO_OPT), 1)
CPPFLAGS += -D_NO_OPT_=1
CPPFLAGS_P += -D_NO_OPT_=1
CPPFLAGS_D += -D_NO_OPT_=1
endif

PROG := simbook

# simulator source files
CPP_SRCS = $(wildcard *.cpp) $(wildcard */*.cpp)
CPP_HDRS = $(wildcard *.hpp) $(wildcard */*.hpp)
CPP_DEPS = $(CPP_SRCS:.cpp=.d)
CPP_OBJS = $(CPP_SRCS:.cpp=.o)
CPP_OBJSD = $(CPP_SRCS:.cpp=.os)
CPP_DEPS_D = $(CPP_SRCS:.cpp=_d.d)
CPP_OBJS_D = $(CPP_SRCS:.cpp=_d.o)
CPP_OBJSD_D = $(CPP_SRCS:.cpp=_d.os)
CPP_DEPS_P = $(CPP_SRCS:.cpp=_p.d)
CPP_OBJS_P = $(CPP_SRCS:.cpp=_p.o)
CPP_OBJSD_P = $(CPP_SRCS:.cpp=_p.os)

LEX_SRCS = lex.yy.c
LEX_OBJS = lex.yy.o

YACC_SRCS = y.tab.c
YACC_HDRS = y.tab.h
YACC_OBJS = y.tab.o

OBJS :=  $(CPP_OBJS) $(CPP_OBJSD) $(LEX_OBJS) $(YACC_OBJS)
OBJS_D :=  $(CPP_OBJS_D) $(CPP_OBJSD_D) $(LEX_OBJS) $(YACC_OBJS)
OBJS_P :=  $(CPP_OBJS_P) $(CPP_OBJSD_P) $(LEX_OBJS) $(YACC_OBJS)

.PHONY: clean


	
booksim: $(LEX_OBJS) $(YACC_OBJS) $(CPP_OBJS) $(CPP_OBJSD)
	$(CXX) $(LDFLAGS) -shared $(CPP_OBJSD) $(LEX_OBJS) $(YACC_OBJS) -o lib$(PROG).so
	ar rs lib$(PROG).a $(CPP_OBJS) $(LEX_OBJS) $(YACC_OBJS)

dbg: $(LEX_OBJS) $(YACC_OBJS) $(CPP_OBJS_D) $(CPP_OBJSD_D)
	$(CXX) $(LDFLAGS_D) -shared $(CPP_OBJSD_D) $(LEX_OBJS) $(YACC_OBJS) -o lib$(PROG)dbg.so
	ar rs lib$(PROG)dbg.a $(CPP_OBJS_D) $(LEX_OBJS) $(YACC_OBJS)

perf: $(LEX_OBJS) $(YACC_OBJS) $(CPP_OBJS_P) $(CPP_OBJSD_P)
	$(CXX) $(LDFLAGS_P) -shared $(CPP_OBJSD_P) $(LEX_OBJS) $(YACC_OBJS) -o lib$(PROG)perf.so
	ar rs lib$(PROG)perf.a $(CPP_OBJS_P) $(LEX_OBJS) $(YACC_OBJS)

all: $(LEX_OBJS) $(YACC_OBJS) $(CPP_OBJS) $(CPP_OBJSD) $(CPP_OBJS_D) $(CPP_OBJSD_D) $(CPP_OBJS_P) $(CPP_OBJSD_P) booksim dbg perf

$(LEX_SRCS): config.l
	$(LEX) $<

$(YACC_SRCS) $(YACC_HDRS): config.y
	$(YACC) -d $<

$(LEX_OBJS): $(LEX_SRCS) $(YACC_HDRS)
	$(CC) $(CPPFLAGS) -c $< -o $@
	gcc $(CPPFLAGS) -c -fPIC $< -o $@

$(YACC_OBJS): $(YACC_SRCS)
	$(CC) $(CPPFLAGS) -c $< -o $@
	gcc $(CPPFLAGS) -c -fPIC $< -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -MMD -c $< -o $@

%_d.o: %.cpp
	$(CXX) $(CPPFLAGS_D) -MMD -c $< -o $@

%_p.o: %.cpp
	$(CXX) $(CPPFLAGS_P) -MMD -c $< -o $@

%.os: %.cpp
	$(CXX) $(CPPFLAGS) -fPIC -MMD -c $< -o $@

%_d.os: %.cpp
	$(CXX) $(CPPFLAGS_D) -fPIC -MMD -c $< -o $@

%_p.os: %.cpp
	$(CXX) $(CPPFLAGS_P) -fPIC -MMD -c $< -o $@

clean:
	rm -f $(CPP_DEPS)
	rm -f $(OBJS)
	rm -f $(PROG)
	rm -f $(PROG).a
	rm -f $(PROG).so

cleandbg:
	rm -f $(CPP_DEPS_D)
	rm -f $(OBJS_D)
	rm -f $(PROG)dbg.a
	rm -f $(PROG)dbg.so

cleanperf:
	rm -f $(CPP_DEPS_P)
	rm -f $(OBJS_P)
	rm -f $(PROG)perf.a
	rm -f $(PROG)perf.so

cleanall:
	rm -f $(YACC_SRCS) $(YACC_HDRS)
	rm -f $(LEX_SRCS)
	rm -f $(CPP_DEPS)
	rm -f $(CPP_DEPS_D)
	rm -f $(CPP_DEPS_P)
	rm -f $(OBJS)
	rm -f $(OBJS_D)
	rm -f $(OBJS_P)
	rm -f $(PROG)
	rm -f $(PROG)*.a
	rm -f $(PROG)*.so
	rm -f lib$(PROG)*
	rm -f *.gcda
	rm -f *.gcno

distclean: cleanall
	rm -f *~ */*~
	rm -f *.o */*.o
	rm -f *.d */*.d

-include $(CPP_DEPS)
-include $(CPP_DEPS_D)
