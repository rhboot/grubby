#ifndef NASHNET_H
#define NASHNET_H

/* type definitions so that the kernel-ish includes can be shared */
#ifndef uint8_t
#  define uint8_t       unsigned char
#endif
#ifndef uint16_t
#  define uint16_t      unsigned short int
#endif
#ifndef uint32_t
#  define uint32_t      unsigned int
#endif
#ifndef uint64_t
#  define uint64_t      unsigned long long int
#endif
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

int nashNetworkCommand(char * cmd);

#endif
