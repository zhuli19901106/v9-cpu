No.:   Addr:    Value:  Intr  Operand  (Dec Format) #Label_id #Meaning
=======================================================================
  1: 00000000: 0000080e: LL   0x000008 (D   8) # label1 
  2: 00000004: 00001026: LBL  0x000010 (D  16)          
  3: 00000008: 0000009a: BOUT 0x000000 (D   0)          
  4: 0000000c: 00000002: LEV  0x000000 (D   0)          
  5: 00000010: 00000811: LLC  0x000008 (D   8) # label14 
  6: 00000014: 0000009d: PSHA 0x000000 (D   0)          
  7: 00000018: 0000019e: PSHI 0x000001 (D   1)          
  8: 0000001c: ffffe005: JSR  0xffffe0 (D -32)           # Call label1 0xfffffc=pc+-32
  9: 00000020: 00001001: ENT  0x000010 (D  16)          
 10: 00000024: 00000002: LEV  0x000000 (D   0)          
 11: 00000028: fffff801: ENT  0xfffff8 (D  -8) # label12 
 12: 0000002c: 00000023: LI   0x000000 (D   0)          
 13: 00000030: 00000440: SL   0x000004 (D   4)          
 14: 00000034: 00002403: JMP  0x000024 (D  36)           # Jmp label2 0x58=pc+36
 15: 00000038: 0000040e: LL   0x000004 (D   4) # label4 
 16: 0000003c: ffffff57: SUBI 0xffffff (D  -1)          
 17: 00000040: 00000440: SL   0x000004 (D   4)          
 18: 00000044: ffffff54: ADDI 0xffffff (D  -1)          
 19: 00000048: 00001055: ADDL 0x000010 (D  16)          
 20: 0000004c: 0000001f: LXC  0x000000 (D   0)          
 21: 00000050: 0000009d: PSHA 0x000000 (D   0)          
 22: 00000054: ffffb805: JSR  0xffffb8 (D -72)           # Call label3 0x100000c=pc+-72
 23: 00000058: 00000801: ENT  0x000008 (D   8)          
 24: 0000005c: 0000100e: LL   0x000010 (D  16) # label2 
 25: 00000060: 00000455: ADDL 0x000004 (D   4)          
 26: 00000064: 0000001f: LXC  0x000000 (D   0)          
 27: 00000068: 0000003b: LBI  0x000000 (D   0)          
 28: 0000006c: ffffc88a: BNE  0xffffc8 (D -56)           # Cond goto label4
 29: 00000070: 00000802: LEV  0x000008 (D   8)          
 30: 00000074: fffff801: ENT  0xfffff8 (D  -8) # label13 
 31: 00000078: 00003b23: LI   0x00003b (D  59)          
 32: 0000007c: 9aca0024: LHI  0x9aca00 (D-6632960)          
 33: 00000080: 00000440: SL   0x000004 (D   4)          
 34: 00000084: 0000100e: LL   0x000010 (D  16)          
 35: 00000088: 0000003b: LBI  0x000000 (D   0)          
 36: 0000008c: 0000108a: BNE  0x000010 (D  16)           # Cond goto label5
 37: 00000090: 0000309e: PSHI 0x000030 (D  48)          
 38: 00000094: ffff7805: JSR  0xffff78 (D-136)           # Call label6 0x100000c=pc+-136
 39: 00000098: 00000801: ENT  0x000008 (D   8)          
 40: 0000009c: 00000802: LEV  0x000008 (D   8)          
 41: 000000a0: 00000c03: JMP  0x00000c (D  12) # label5  # Jmp label7 0xac=pc+12
 42: 000000a4: 0000040e: LL   0x000004 (D   4) # label8 
 43: 000000a8: 00000a5d: DIVI 0x00000a (D  10)          
 44: 000000ac: 00000440: SL   0x000004 (D   4)          
 45: 000000b0: 0000100e: LL   0x000010 (D  16) # label7 
 46: 000000b4: 0000045e: DIVL 0x000004 (D   4)          
 47: 000000b8: 0000003b: LBI  0x000000 (D   0)          
 48: 000000bc: ffffe488: BE   0xffffe4 (D -28)           # Cond goto label8
 49: 000000c0: 00003003: JMP  0x000030 (D  48)           # Jmp label9 0xf0=pc+48
 50: 000000c4: 0000100e: LL   0x000010 (D  16) # label11 
 51: 000000c8: 0000045e: DIVL 0x000004 (D   4)          
 52: 000000cc: 00003054: ADDI 0x000030 (D  48)          
 53: 000000d0: 0000009d: PSHA 0x000000 (D   0)          
 54: 000000d4: ffff3805: JSR  0xffff38 (D-200)           # Call label10 0x100000c=pc+-200
 55: 000000d8: 00000801: ENT  0x000008 (D   8)          
 56: 000000dc: 0000100e: LL   0x000010 (D  16)          
 57: 000000e0: 00000464: MODL 0x000004 (D   4)          
 58: 000000e4: 00001040: SL   0x000010 (D  16)          
 59: 000000e8: 0000040e: LL   0x000004 (D   4)          
 60: 000000ec: 00000a5d: DIVI 0x00000a (D  10)          
 61: 000000f0: 00000440: SL   0x000004 (D   4)          
 62: 000000f4: 00000023: LI   0x000000 (D   0) # label9 
 63: 000000f8: 00000426: LBL  0x000004 (D   4)          
 64: 000000fc: ffffc48c: BLT  0xffffc4 (D -60)           # Cond goto label11
 65: 00000100: 00000802: LEV  0x000008 (D   8)          
 66: 00000104: 0000080e: LL   0x000008 (D   8) # label16 
 67: 00000108: 000000a4: IVEC 0x000000 (D   0)          
 68: 0000010c: 00000002: LEV  0x000000 (D   0)          
 69: 00000110: 0000080e: LL   0x000008 (D   8) # label15 
 70: 00000114: 000000a7: TIME 0x000000 (D   0)          
 71: 00000118: 00000002: LEV  0x000000 (D   0)          
 72: 0000011c: 0000080e: LL   0x000008 (D   8) # label19 
 73: 00000120: 00000000: HALT 0x000000 (D   0)          
 74: 00000124: 00000002: LEV  0x000000 (D   0)          
 75: 00000128: 0000009d: PSHA 0x000000 (D   0)          
 76: 0000012c: 000000a0: PSHB 0x000000 (D   0)          
 77: 00000130: 00008408: LEAG 0x000084 (D 132)          
 78: 00000134: 0000009d: PSHA 0x000000 (D   0)          
 79: 00000138: fffeec05: JSR  0xfffeec (D-276)           # Call label12 0x1000024=pc+-276
 80: 0000013c: 00000801: ENT  0x000008 (D   8)          
 81: 00000140: 00008c15: LG   0x00008c (D 140)          
 82: 00000144: 0000009d: PSHA 0x000000 (D   0)          
 83: 00000148: ffff2805: JSR  0xffff28 (D-216)           # Call label13 0x1000070=pc+-216
 84: 0000014c: 00000801: ENT  0x000008 (D   8)          
 85: 00000150: 00000a9e: PSHI 0x00000a (D  10)          
 86: 00000154: fffeb805: JSR  0xfffeb8 (D-328)           # Call label14 0x100000c=pc+-328
 87: 00000158: 00000801: ENT  0x000008 (D   8)          
 88: 0000015c: 00007015: LG   0x000070 (D 112)          
 89: 00000160: ffffff57: SUBI 0xffffff (D  -1)          
 90: 00000164: 00006845: SG   0x000068 (D 104)          
 91: 00000168: 000000a1: POPB 0x000000 (D   0)          
 92: 0000016c: 000000a3: POPA 0x000000 (D   0)          
 93: 00000170: 00000098: RTI  0x000000 (D   0)          
 94: 00000174: 00000002: LEV  0x000000 (D   0)          
 95: 00000178: 00000023: LI   0x000000 (D   0) # <=ENTRY          
 96: 0000017c: 00005045: SG   0x000050 (D  80)          
 97: 00000180: 2625a09e: PSHI 0x2625a0 (D2500000)          
 98: 00000184: ffff8805: JSR  0xffff88 (D-120)           # Call label15 0x100010c=pc+-120
 99: 00000188: 00000801: ENT  0x000008 (D   8)          
100: 0000018c: ffff9808: LEAG 0xffff98 (D-104)          
101: 00000190: 0000009d: PSHA 0x000000 (D   0)          
102: 00000194: ffff6c05: JSR  0xffff6c (D-148)           # Call label16 0x1000100=pc+-148
103: 00000198: 00000801: ENT  0x000008 (D   8)          
104: 0000019c: 00000097: STI  0x000000 (D   0)          
105: 000001a0: 00000003: JMP  0x000000 (D   0)           # Jmp label17 0x1a0=pc+0
106: 000001a4: fffffc03: JMP  0xfffffc (D  -4) # label18  # Jmp label18 0x10001a0=pc+-4
107: 000001a8: 0000009e: PSHI 0x000000 (D   0)          
108: 000001ac: ffff6c05: JSR  0xffff6c (D-148)           # Call label19 0x1000118=pc+-148
109: 000001b0: 00000801: ENT  0x000008 (D   8)          
110: 000001b4: 00000002: LEV  0x000000 (D   0)          
=======================================================================
Data Segment
Address     Hex									 | Char
0x000001b8	61 20 74 72	61 70 20 69	73 20 63 61	70 74 75 72	 | a trap is captur
0x000001c8	65 64 2e 0a	00 00 00 00	                   	                   	 | ed......        
