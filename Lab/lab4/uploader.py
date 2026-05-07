import serial
import struct
import os
import sys

# get the file path
file_path = sys.argv[1]

# read the data
with open(file_path, 'rb') as f:
    kernel_data = f.read()

# define header 
header = struct.pack('<II', # <: little endian | I: unsigned integer
    0x544F4F42,           # "BOOT" in hex , 告訴 orangepi 現在傳的這個東西是什麼
    len(kernel_data),     # size
)

magic = struct.pack('<I', 0x544F4F42)
size = struct.pack('<I', len(kernel_data))

print(f"Sending kernel: {file_path} ({len(kernel_data)} bytes)")

# write to uart stream to Orange Pi ttyUSB0 pts/2
with open('/dev/ttyUSB0', "wb", buffering = 0) as tty:
    tty.write(header)
    tty.write(kernel_data)

print("Transfer Done!")
