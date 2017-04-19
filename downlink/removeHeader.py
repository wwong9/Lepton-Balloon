#!/usr/bin/python

import sys
import struct
import os
import time
import hashlib

# bytes to read/transmit at once
CHUNKSIZE = 32
PACKETSIZE = 228
HEADERSIZE = 36

# check number of arguments
if len(sys.argv) != 3:
    print("Expected input and output file")
    sys.exit()

# extract file name for syntactic clarity
inFile = sys.argv[1]
outFile = sys.argv[2]

# open binary file
try:
    inf = open(inFile,"rb")
    of = open(outFile,"wb")
    fileSize = os.path.getsize(inFile)
    header = inf.read(HEADERSIZE)
    rawData = inf.read()
    fileHash = hashlib.md5(rawData).hexdigest()
#    print (fileSize - HEADERSIZE)
#    print fileHash
except IOError:
    print("Could not open file")
    sys.exit()

# print header for debug purposes
#print header

# decode control packet
packetNum = (ord(header[0]) << 8) | ord(header[1])
if packetNum != 0:
    print "Incorrect Packet Number!"
    print packetNum
    inf.close()
    of.close()
    sys.exit()

fileSize_control = (ord(header[2]) << 8) | ord(header[3])
if fileSize_control != fileSize-HEADERSIZE:
    print "Incorrect File Size!"
    print (fileSize - HEADERSIZE)
    print fileSize_control
    inf.close()
    of.close()
    sys.exit()

fileHash_control = ""
for i in range(0,32):
    fileHash_control += header[i+4]
if(fileHash != fileHash_control):
    print "Incorrect Packet Hash!"
    print fileHash
    print fileHash_control
    inf.close()
    of.close()
    sys.exit()

# rewrite out file as infile - header
of.write(rawData)

# close files and exit
inf.close()
of.close()
sys.exit()