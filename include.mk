CC?=gcc
CFLAGS=-m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -Wno-incompatible-pointer-types -Werror=implicit-function-declaration
