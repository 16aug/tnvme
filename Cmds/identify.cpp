/*
 * Copyright (c) 2011, Intel Corporation.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <string.h>
#include <math.h>
#include "identify.h"
#include "../Utils/buffers.h"
#include "../Utils/fileSystem.h"


#define CNS_BITMASK         0x01

SharedIdentifyPtr Identify::NullIdentifyPtr;
const uint8_t Identify::Opcode = 0x06;
const uint16_t Identify::IDEAL_DATA_SIZE =  4096;


// Register metrics (ID Cmd Ctrlr Cap struct) to aid interfacing with the dnvme
#define ZZ(a, b, c, d)         { b, c, d },
IdentifyDataType Identify::mIdCtrlrCapMetrics[] =
{
    IDCTRLRCAP_TABLE
};
#undef ZZ

// Register metrics (ID Cmd namespace struct) to aid interfacing with the dnvme
#define ZZ(a, b, c, d)         { b, c, d },
IdentifyDataType Identify::mIdNamespcType[] =
{
    IDNAMESPC_TABLE
};
#undef ZZ


Identify::Identify() : AdminCmd(0, Trackable::OBJTYPE_FENCE)
{
    // This constructor will throw
}


Identify::Identify(int fd) : AdminCmd(fd, Trackable::OBJ_IDENTIFY)
{
    Init(Opcode, DATADIR_FROM_DEVICE);
    SetCNS(true);
}


Identify::~Identify()
{
}


void
Identify::SetCNS(bool ctrlr)
{
    LOG_NRM("Setting CNS");
    uint8_t curVal = GetByte(10, 0);
    if (ctrlr)
        curVal |= CNS_BITMASK;
    else
        curVal &= ~CNS_BITMASK;
    SetByte(curVal, 10, 0);
}


bool
Identify::GetCNS() const
{
    uint8_t curVal = GetByte(10, 0);
    if (curVal & CNS_BITMASK) {
        LOG_NRM("Getting CNS=1");
        return true;
    }
    LOG_NRM("Getting CNS=0");
    return false;
}


uint64_t
Identify::GetValue(IdCtrlrCap field) const
{
    if (field >= IDCTRLRCAP_FENCE) {
        LOG_DBG("Unknown ctrlr cap field: %d", field);
        throw exception();
    }

    return GetValue(field, mIdCtrlrCapMetrics);
}


uint64_t
Identify::GetValue(IdNamespc field) const
{
    if (field >= IDNAMESPC_FENCE) {
        LOG_DBG("Unknown namespace field: %d", field);
        throw exception();
    }
    return GetValue(field, mIdNamespcType);
}


uint64_t
Identify::GetValue(int field, IdentifyDataType *idData) const
{
    uint8_t byte;
    uint64_t value = 0;

    if (idData[field].length > sizeof(uint64_t)) {
        LOG_DBG("sizeof(%s) > %ld bytes", idData[field].desc, sizeof(uint64_t));
        throw exception();
    } else if ((idData[field].length + idData[field].offset) >=
        GetPrpBufferSize()) {
        LOG_DBG("Detected illegal def in IDxxxxx_TABLE or buffer is to small");
        LOG_DBG("Reference calc (%d): %d + %d >= %ld", field,
            idData[field].length, idData[field].offset, GetPrpBufferSize());
        throw exception();
    }

    for (int i = 0; i < idData[field].length; i++) {
        byte = (GetROPrpBuffer())[idData[field].offset + i];
        value |= ((uint64_t)byte << (i*8));
    }
    LOG_NRM("%s = 0x%08lX", idData[field].desc, value);
    return value;
}


void
Identify::Dump(LogFilename filename, string fileHdr) const
{
    FILE *fp;

    Cmd::Dump(filename, fileHdr);

    // Reopen the file and append the same data in a different format
    if ((fp = fopen(filename.c_str(), "a")) == NULL) {
        LOG_DBG("Failed to open file: %s", filename.c_str());
        throw exception();
    }

    fprintf(fp, "\n------------------------------------------------------\n");
    fprintf(fp, "----Detailed decoding of the cmd payload as follows---\n");
    fprintf(fp, "------------------------------------------------------");

    // How do we interpret the data contained herein?
    if (GetCNS()) {
        for (int i = 0; i < IDCTRLRCAP_FENCE; i++)
            Dump(fp, i, mIdCtrlrCapMetrics);
    } else {
        for (int i = 0; i < IDNAMESPC_FENCE; i++)
            Dump(fp, i, mIdNamespcType);
    }
    fclose(fp);
}


void
Identify::Dump(FILE *fp, int field, IdentifyDataType *idData) const
{
    const uint8_t *data;
    const int BUF_SIZE = 20;
    char work[BUF_SIZE];
    string output;
    unsigned long dumpLen = idData[field].length;

    fprintf(fp, "\n%s\n", idData[field].desc);

    data = &((GetROPrpBuffer())[idData[field].offset]);
    if ((idData[field].length + idData[field].offset) > GetPrpBufferSize()) {
        LOG_DBG("Detected illegal definition in IDxxxxx_TABLE");
        LOG_DBG("Reference calc (%d): %d + %d >= %ld", field,
            idData[field].length, idData[field].offset, GetPrpBufferSize());
        throw exception();
    }

    unsigned long addr = idData[field].offset;
    for (unsigned long j = 0; j < dumpLen; j++, addr++) {
        if ((j % 16) == 15) {
            snprintf(work, BUF_SIZE, " %02X\n", *data++);
            output += work;
            fprintf(fp, "%s", output.c_str());
            output.clear();
        } else if ((j % 16) == 0) {
            snprintf(work, BUF_SIZE, "0x%08X: %02X",
                (uint32_t)addr, *data++);
            output += work;
        } else {
            snprintf(work, BUF_SIZE, " %02X", *data++);
            output += work;
        }
    }
    if (output.length() != 0)
        fprintf(fp, "%s\n", output.c_str());
}


LBAFormat
Identify::GetLBAFormat() const
{
    LBAFormat lbaFormat;

    if (GetCNS()) {
        LOG_DBG("This cmd does not contain a namespace data struct");
        throw exception();
    }

    uint64_t flbas = GetValue(IDNAMESPC_FLBAS);
    uint8_t formatIdx = (uint8_t)(flbas & 0x0f);
    uint64_t work = GetValue((IDNAMESPC_LBAF0 + formatIdx), mIdNamespcType);
    memcpy(&lbaFormat, &work, sizeof(lbaFormat));

    LOG_NRM("Active LBA format:");
    LOG_NRM("  MS (Metadata Size)        = 0x%04X", lbaFormat.MS);
    LOG_NRM("  LBADS (LBA Data Size)     = 0x%02X", lbaFormat.LBADS);
    LOG_NRM("  RP (Relative Performance) = 0x%01X", lbaFormat.RP);
    return lbaFormat;
}


uint64_t
Identify::GetLBADataSize() const
{
    LBAFormat lbaFormat = GetLBAFormat();
    uint64_t lbaDataSize = (uint64_t)pow(2.0, lbaFormat.LBADS);
    LOG_NRM("Active logical blk size = 0x%016llX",
        (long long unsigned int)lbaDataSize);
    return lbaDataSize;
}
