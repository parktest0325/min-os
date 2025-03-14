TARGET = kernel.elf
ELFLIBDIR = ../devenv/x86_64-elf/
EDK2DIR = ../devenv/edk2/

CXX = clang++
LD = ld.lld
RM = rm -f

CXXFLAGS += -O2 -Wall -g --target=x86_64-elf -ffreestanding -mno-red-zone \
            -fno-exceptions -fno-rtti -std=c++17

CPPFLAGS += -I$(ELFLIBDIR)/include/c++/v1 -I$(ELFLIBDIR)/include -I$(ELFLIBDIR)/include/freetype2 \
            -I$(EDK2DIR)/MdePkg/Include -I$(EDK2DIR)/MdePkg/Include/X64 \
            -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS \
            -DEFIAPI='__attribute__((ms_abi))'

LDFLAGS += --entry KernelMain -z norelro --image-base 0x100000 --static
LDFLAGS += -L$(ELFLIBDIR)

KERNEL_FILE = main
KERNEL_SRC = $(addsuffix .cpp, $(KERNEL_FILE))
KERNEL_OBJ = $(KERNEL_SRC:.cpp=.o)

SRCS = $(KERNEL_SRC)
OBJS = $(KERNEL_OBJ)

.PHONY: all clean re fclean
all: $(TARGET)

clean:
	$(RM) $(OBJS)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

fclean: clean
	$(RM) $(TARGET)


re: fclean all

