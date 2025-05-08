TARGET = kernel.elf
ELFLIBDIR = ../devenv/x86_64-elf
EDK2DIR = ../devenv/edk2/

RESOURCE_OBJ = hankaku.font

CXX = clang++
CC = clang
ASM = nasm
LD = ld.lld
RM = rm -f

ifeq ($(DEBUG),1)
	DDEBUG = -DDEBUG -O2 -g
endif

CFLAGS   += -O2 -Wall --target=x86_64-elf -ffreestanding -mno-red-zone
CXXFLAGS += -O2 -Wall --target=x86_64-elf -ffreestanding -mno-red-zone \
            -fno-exceptions -fno-rtti -std=c++17

CPPFLAGS += -I.
CPPFLAGS += -I$(ELFLIBDIR)/include/c++/v1 -I$(ELFLIBDIR)/include -I$(ELFLIBDIR)/include/freetype2 \
            -I$(EDK2DIR)/MdePkg/Include -I$(EDK2DIR)/MdePkg/Include/X64 \
            -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS \
            -DEFIAPI='__attribute__((ms_abi))'

LDFLAGS += --entry KernelMain -z norelro --image-base 0x100000 --static
LDFLAGS += -L$(ELFLIBDIR)/lib -lc -lc++ -lc++abi

MODULE_USB_SRC = $(shell find usb -name '*.cpp')
MODULE_USB_OBJ = $(MODULE_USB_SRC:.cpp=.o)

KERNEL_ASMFILE = asmfunc
KERNEL_ASMSRC = $(addsuffix .asm, $(KERNEL_ASMFILE))
KERNEL_ASMOBJ = $(KERNEL_ASMSRC:.asm=.o)

KERNEL_CFILE = newlib_support
KERNEL_CSRC = $(addsuffix .c, $(KERNEL_CFILE))
KERNEL_COBJ = $(KERNEL_CSRC:.c=.o)

KERNEL_FILE = main font graphics console pci logger libcxx_support mouse interrupt \
							segment paging memory_manager window layer timer frame_buffer acpi \
							keyboard
KERNEL_SRC = $(addsuffix .cpp, $(KERNEL_FILE))
KERNEL_OBJ = $(KERNEL_SRC:.cpp=.o)

SRCS = $(KERNEL_SRC) $(KERNEL_CSRC) $(KERNEL_ASMSRC) $(MODULE_USB_SRC)
OBJS = $(KERNEL_OBJ) $(KERNEL_COBJ) $(KERNEL_ASMOBJ) $(MODULE_USB_OBJ)

.PHONY: all clean re fclean
all: $(TARGET)

clean:
	$(RM) $(OBJS)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ $(RESOURCE_OBJ) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DDEBUG) -c $< -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DDEBUG) -c $< -o $@

%.o: %.asm
	$(ASM) -f elf64 $< -o $@


fclean: clean
	$(RM) $(TARGET)

re: fclean all
