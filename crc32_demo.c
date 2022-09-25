/*
 * This file demonstrates how the CRC peripheral of the STM32 family of
 * microcontrollers could be used to calculate a 32-bit CRC on a string
 * of bytes, piecewise, processed in byte order.
 * 
 * There are three functions for calculating the CRC:
 * 1. crc32start() should be called to begin a new CRC calculation; then
 * 2. crc32process() should be called repeatedly to process the data into
 *     the CRC; then
 * 3. crc32finish() should be called to finish the calculation and get the
 *     final CRC value.
 * The function crc32lt4bytes() is a helper function needed by crc32process().
 * 
 * Additional code is included to test the above functions on a PC.  To
 * compile the test code, uncomment the line
 *  #define PC_TEST_CODE
 */

/*
    MIT License

    Copyright (c) 2022 Clinton J.S. Reddekop

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
 */


/* Compile with gcc -std=c99 */

#define PC_TEST_CODE
#define PC_TEST_CODE_VERBOSE /* Takes longer; prints a line for each test */

#if defined(PC_TEST_CODE)
    
    #include <stdio.h>
    #include <string.h>
    #include <inttypes.h>
    #include <stdlib.h>
    
    static uint32_t cleverCRC(uint32_t crcReg, uint8_t *pData, int numBytes);
    
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define __REV(word)             __builtin_bswap32(word)
    #else
        #define __REV(word)             (word)
    #endif
  
    /* Here we need to simulate the operations that we can do on the
     * CRC peripheral: reset, read CRC, write CRC.  periphCRC
     * represents the value of the CRC in the peripheral. */
    
    static uint32_t periphCRC;
    #define CRC_RESET()             { periphCRC = 0xffffffff; }
    #define CRC_READ()              (periphCRC)
    
    /* We have to reverse the word because cleverCRC() processes
     * data in byte order, and with CRC_PROCESS_WORD() we're trying
     * to simulate the STM32 CRC peripheral, which processes words
     * from most- to least-significant byte */
    #define CRC_PROCESS_WORD(word)                              \
    {                                                           \
        uint32_t temp = __REV(word);                            \
        periphCRC = cleverCRC(periphCRC, (uint8_t *)&temp, 4);  \
    }
    
#else
    
    #include <stdint.h>
    #include "cmsis_gcc.h" // __REV()
    
    #define CRC_RESET()             { CRC->CR |= CRC_CR_RESET; }
    #define CRC_READ()              (CRC->DR)
    #define CRC_PROCESS_WORD(word)  { CRC->DR = word; }
  
#endif

/* Sometimes I need to XOR some bits into the CRC that is in the peripheral to
 * create a new CRC.  I haven't found a nice way to do that, so instead I'll
 * keep those "extra" bits in 'extraCRC'.  This means that the current CRC
 * is actually the XOR of the value in the peripheral and of 'extraCRC'. */
static uint32_t extraCRC;

/* Local function prototype */
static void crc32lt4bytes(uint8_t *pData, int numBytes);

/* Reset CRC state.  initialValue is the initial value for the CRC */
void crc32start(uint32_t initialValue)
{
    // Take a mutex lock here if multiple tasks might use peripheral
    
    /* Reset CRC state */
    CRC_RESET();    /* Sets the CRC peripheral value to 0xffffffff */
    extraCRC = 0xffffffff;      /* Cancels the value in the peripheral */
    extraCRC ^= initialValue;
    /* extraCRC gets XORed into the first word of data so this will be
     * equivalent to having set the peripheral's initial value */
}

/* Process numBytes data bytes from memory at pData into CRC */
void crc32process(uint8_t *pData, int numBytes)
{
    /* If necessary, process 1, 2 or 3 bytes to get pData to a word address */
    int startBytes = (int)((uintptr_t)pData % 4);
    if (0 != startBytes)
    {
        /* pData is not at a word address */
        
        startBytes = 4 - startBytes; /* # bytes from pData to next word address */
        if (startBytes > numBytes)
        {
            startBytes = numBytes;
        }
        
        crc32lt4bytes(pData, startBytes); /* process that many bytes into CRC */
        numBytes -= startBytes;
        pData = &pData[startBytes];
    }
    /* now pData is at a word address, or numBytes is zero */
    
    /* process a word at a time into CRC */
    uint32_t *pData32 = (uint32_t *)pData;
    if (numBytes >= 4)
    {
        /* Recall that the current CRC is the XOR of the value
         * in the peripheral and the value in 'extraCRC'. */
        /* Unrolled first iteration of loop so we can XOR extraCRC into
         * the first data word - this is equivalent to having XORed it
         * into the CRC value in the peripheral (which we can't do
         * easily). */
        /* Note __REV() reverses byte order on little-endian machines */
        CRC_PROCESS_WORD(__REV(*pData32) ^ extraCRC);
        extraCRC = 0;
        ++pData32;
        numBytes -= 4;
        for ( ; numBytes >= 4; numBytes -= 4)
        {
            /* Note reverse byte order because we are on a little-endian machine */
            CRC_PROCESS_WORD(__REV(*pData32));
            ++pData32;
        }
    }
    
    /* process any remaining bytes */
    if (0 != numBytes)
    {
        pData = (uint8_t *)pData32;
        crc32lt4bytes(pData, numBytes);
    }
}

/* Get final CRC value */
uint32_t crc32finish(void)
{
    /* Recall that the current CRC is the XOR of the value
     * in the peripheral and the value in 'extraCRC'. */
    uint32_t crc32 = CRC_READ();
    crc32 ^= extraCRC;
    
    // Give back mutex lock here
    
    return crc32;
}

/* Process < 4 data bytes into the current CRC */
static void crc32lt4bytes(uint8_t *pData, int numBytes)
{
    if (numBytes == 0)
    {
        return;
    }
    
    /* The CRC peripheral is made to process one 4-byte word at a time.
     * We want to process numBytes, which is less than 4, bytes here.
     * We can determine the resulting CRC by doing this:
     *  1. Get oldCRC, the old CRC value
     *  2. Clear the CRC peripheral
     *  3. Let dataBytes be the next numBytes bytes from memory at pData,
     *      as the numBytes least-significant bytes of a word
     *  4. Let extraCRC = oldCRC << (8*numBytes);
     *  5. Let val = (oldCRC >> (32 - 8*numBytes)) ^ dataBytes;
     *  6. Process the 4-byte word 'val' into the CRC peripheral.
     * Then our new CRC is the XOR of the new value in the CRC peripheral
     * and the value in extraCRC.  (There isn't an easy way to XOR extraCRC
     * into the peripheral, which is why we keep the extraCRC variable.)
     * 
     * Reference: https://clinton-r.github.io/stm32crc_tricks/stm32crc.html
     */
    
    /* 1. Get oldCRC, the old CRC value
     * 2. Clear the CRC peripheral */
    uint32_t oldCRC = CRC_READ();
    /* Putting the value from the peripheral into val here will have the
     * same effect as if we cleared the peripheral here and initialised
     * val to 0 */
    uint32_t val = oldCRC;
    /* Recall that the current CRC is the XOR of the value currently
     * in the peripheral and the value in 'extraCRC'. */
    oldCRC ^= extraCRC;
    
    /* 3. Let dataBytes be the next numBytes bytes from memory at pData,
     *     as the numBytes least-significant bytes of a word
     * 4. Let extraCRC = oldCRC << (8*numBytes);
     * 5. Let val = (oldCRC >> (32 - 8*numBytes)) ^ dataBytes; */
    uint32_t dataBytes = 0;
    switch (numBytes)
    {
      case 1:
        dataBytes ^= ((uint32_t)pData[0] & 0x000000ff);
        extraCRC = oldCRC << 8;
        val ^= dataBytes;
        val ^= (oldCRC >> 24 & 0x000000ff);
        break;
        
      case 2:
        dataBytes ^= ((uint32_t)pData[0] << 8 & 0x0000ff00);
        dataBytes ^= ((uint32_t)pData[1] << 0 & 0x000000ff);
        extraCRC = oldCRC << 16;
        val ^= dataBytes;
        val ^= (oldCRC >> 16 & 0x0000ffff);
        break;
        
      case 3:
      default:
        val ^= ((uint32_t)pData[0] << 16 & 0x00ff0000);
        val ^= ((uint32_t)pData[1] <<  8 & 0x0000ff00);
        val ^= ((uint32_t)pData[2] <<  0 & 0x000000ff);
        extraCRC = oldCRC << 24;
        val ^= dataBytes;
        val ^= (oldCRC >> 8 & 0x00ffffff);
        break;
    }
    
    /* 6. Process the 4-byte word 'val' into the CRC peripheral. */
    CRC_PROCESS_WORD(val);
    /* The new CRC is the XOR of the value now in the peripheral
     * and the value in extraCRC. */
}

#if defined(PC_TEST_CODE)

 #define POLY           0x04c11db7
 #define CRC_INIT_VAL   0xaaaaaaaa

/* Shift register implementation to compare against */
static uint32_t cleverCRC(uint32_t crcReg, uint8_t *pData, int numBytes)
{
    for ( ; numBytes > 0; numBytes -= 1)
    {
        /* Process next byte */
        
        for (uint16_t bitMask = 0x080; bitMask; bitMask >>= 1)
        {
            uint32_t dataBit = ((uint16_t)*pData & bitMask) ? 1 : 0;
            uint32_t popCrc = ( (crcReg >> 31) & 1 ) ^ dataBit;

            crcReg <<= 1;
            if (popCrc)
            {
                crcReg ^= POLY;
            }
        }
        
        ++pData;
    }
    
    return crcReg;
}

/* Given 2 data buffers, calculate the CRC on (the data in the first concatenated
 * to the data in the second). */
static uint32_t getTrialCRC(uint8_t *pData1, int numBytes1,
                            uint8_t *pData2, int numBytes2)
{
    crc32start(CRC_INIT_VAL);
    crc32process(pData1, numBytes1);
    crc32process(pData2, numBytes2);
    return crc32finish();
}

/* main() for testing */
int main(void)
{
    /* Create buffers of uint32_t so we know they start at word addresses */
    #define TEST_BUF_SIZE_WORDS   5
    static uint32_t testBuf1Words[ TEST_BUF_SIZE_WORDS ];
    static uint32_t testBuf2Words[ TEST_BUF_SIZE_WORDS ];
    uint8_t *testBuf1 = (uint8_t *)testBuf1Words;
    uint8_t *testBuf2 = (uint8_t *)testBuf2Words;
    
    const int numIters = 1000;
    int numFails = 0;
    int numTests = 0;
    
    for (int iter = 0; iter < numIters; iter += 1)
    {
        #if RAND_MAX < 0xffff
            #warning "rand() return value isn't wide enough"
        #endif
        for (int i = 0; i < TEST_BUF_SIZE_WORDS; i += 1)
        {
            testBuf1[i] = (uint32_t)rand() << 16 ^ (uint32_t)rand();
            testBuf2[i] = (uint32_t)rand() << 16 ^ (uint32_t)rand();
        }
        
        int fail = 0;
        
        for (int start1 = 0; start1 < 4; start1 += 1)
        {
         for (int end1 = start1; end1 < sizeof(testBuf1Words); end1 += 1)
         {
          for (int start2 = 0; start2 < 4; start2 += 1)
          {
           for (int end2 = start2; end2 < sizeof(testBuf2Words); end2 += 1)
           {
                uint32_t crc32good = CRC_INIT_VAL;
                crc32good = cleverCRC(  crc32good,
                                        &testBuf1[start1], end1-start1);
                crc32good = cleverCRC(  crc32good,
                                        &testBuf2[start2], end2-start2);
                
                uint32_t crc32check = getTrialCRC(
                                        &testBuf1[start1], end1-start1,
                                        &testBuf2[start2], end2-start2);
                
                numTests += 1;
                
                if (crc32good != crc32check)
                {
                    fail = 1;
                }
              #if defined(PC_TEST_CODE_VERBOSE)
                printf("%4d start1=%2d end1=%2d start2=%2d end2=%2d exp=0x%08"PRIx32" act=0x%08"PRIx32"  ",
                    iter,
                    start1, end1, start2, end2, 
                    crc32good, crc32check);
                if (crc32good != crc32check)
                {
                    printf("FAIL\n");
                }
                else
                {
                    printf("\n");
                }
              #endif
           }
          }
         }
        }
        
        if (fail)
        {
            numFails += 1;
        }
    }
    
    printf("%d FAILS in %d tests\n", numFails, numTests);
    return 0;
}

#endif // #if defined(PC_TEST_CODE)
