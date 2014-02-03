all: carpi

.PHONY: all clean clean_tmp clear deps

SRCS = $(shell find . -type f -name '*.cpp')
OBJS = $(SRCS:.cpp=.o)

CPPFLAGS +=  -std=c++0x 
LDFLAGS += -ludev -lpthread -lbcm2835
LIBS += 
CC = 'g++'
LINKER = 'g++'

DEPS := $(patsubst %.o,%.d,$(OBJS))

clear: clean

clean: clean_tmp
	@$(RM) -rf carpi
	@echo "all cleaned"
	
clean_tmp: 
	@$(RM) -rf $(OBJS) $(DEPS)
	@echo "temporary files cleaned"

%.o: %.cpp 
	$(CC) -ggdb -D_REENTRANT -c $(CPPFLAGS) -o $@ $<
	
carpi: $(OBJS)
	$(LINKER) -ggdb -o $@ $^ $(LIBS) $(LDFLAGS)

deps: $(SOURCES)
	$(CC) -MD -E $(SOURCES) > /dev/null

-include $(DEPS)
