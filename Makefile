CC=avr-gcc
AS=$(CC)
LD=$(CC)
PROGNAME=dc_usb
CPU=atmega328p

CFLAGS=-Wall -O3 -Iusbdrv -I. -mmcu=$(CPU) -DF_CPU=16000000L #-DDEBUG_LEVEL=1
LDFLAGS=-Wl,-Map=$(PROGNAME).map -mmcu=$(CPU) 
AVRDUDE=avrdude -p m328p -P usb -c avrispmkII

OBJS=usbdrv/usbdrv.o usbdrv/usbdrvasm.o main.o maplebus.o dc_pad.o

HEXFILE=$(PROGNAME).hex
ELFFILE=$(PROGNAME).elf

# symbolic targets:
all: $(HEXFILE)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.S.o:
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

rxcode.asm: generate_rxcode.sh
	./generate_rxcode.sh > rxcode.asm

maplebus.o: maplebus.c rxcode.asm
	$(CC) $(CFLAGS) -c $< -o $@

.c.s:
	$(CC) $(CFLAGS) -S $< -o $@


clean:
	rm -f $(HEXFILE) $(PROGNAME).map $(PROGNAME).elf $(PROGNAME).hex *.o usbdrv/*.o main.s usbdrv/oddebug.s usbdrv/usbdrv.s

# file targets:
$(ELFFILE): $(OBJS)
	$(LD) $(LDFLAGS) -o $(ELFFILE) $(OBJS)

$(HEXFILE):	$(ELFFILE)
	rm -f $(HEXFILE) 
	avr-objcopy -j .text -j .data -O ihex $(ELFFILE) $(HEXFILE)
	avr-size $(ELFFILE)


flash: $(HEXFILE)
	$(AVRDUDE) -Uflash:w:$(HEXFILE) -B 1.0

# Extended fuse byte
EFUSE=0xfd

# Fuse high byte
HFUSE=0xdb

# Fuse low byte
LFUSE=0xd7

fuse:
	$(AVRDUDE) -e -Uefuse:w:$(EFUSE):m -Uhfuse:w:$(HFUSE):m -Ulfuse:w:$(LFUSE):m -B 20.0 -v

chip_erase:
	$(AVRDUDE) -e -B 1.0 -F

reset:
	$(AVRDUDE) -B 1.0 -F
	
