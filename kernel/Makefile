TARGET = kernel.elf
ELFLIBDIR = ../devenv/x86_64-elf
EDK2DIR = ../devenv/edk2/

RESOURCE_OBJ = hankaku.font

CXX = clang++
CC = clang
LD = ld.lld
RM = rm -f

CFLAGS   += -O2 -Wall -g --target=x86_64-elf -ffreestanding -mno-red-zone
CXXFLAGS += -O2 -Wall -g --target=x86_64-elf -ffreestanding -mno-red-zone \
            -fno-exceptions -fno-rtti -std=c++17

CPPFLAGS += -I$(ELFLIBDIR)/include/c++/v1 -I$(ELFLIBDIR)/include -I$(ELFLIBDIR)/include/freetype2 \
            -I$(EDK2DIR)/MdePkg/Include -I$(EDK2DIR)/MdePkg/Include/X64 \
            -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS \
            -DEFIAPI='__attribute__((ms_abi))'

LDFLAGS += --entry KernelMain -z norelro --image-base 0x100000 --static
LDFLAGS += -L$(ELFLIBDIR)/lib -lc

KERNEL_CFILE = newlib_support
KERNEL_CSRC = $(addsuffix .c, $(KERNEL_CFILE))
KERNEL_COBJ = $(KERNEL_CSRC:.c=.o)

KERNEL_FILE = main font graphics console
KERNEL_SRC = $(addsuffix .cpp, $(KERNEL_FILE))
KERNEL_OBJ = $(KERNEL_SRC:.cpp=.o)

SRCS = $(KERNEL_SRC) $(KERNEL_CSRC)
OBJS = $(KERNEL_OBJ) $(KERNEL_COBJ)

.PHONY: all clean re fclean
all: $(TARGET)

clean:
	$(RM) $(OBJS)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ $(RESOURCE_OBJ) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

fclean: clean
	$(RM) $(TARGET)

re: fclean all
